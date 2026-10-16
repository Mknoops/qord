#include "VideoShrink.hpp"

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryFile>
#include <QtGlobal>

#include <cmath>
#include <cstring>
#include <limits>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace Qord {
namespace Video {

namespace {

// Bits per pixel each frame needs to look acceptable in H.264
constexpr double BitsPerPixel = 0.07;
// What the MP4 container takes beyond the streams, and some slack
constexpr double Overhead = 0.94;

void setOption(AVCodecContext *ctx, const char *key, const char *value)
{
    av_opt_set(ctx->priv_data, key, value, 0);
}

template <typename T>
const T *supported(const AVCodec *codec, AVCodecContext *ctx, int which, const T *legacy)
{
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    Q_UNUSED(legacy);
    const void *out = nullptr;
    int count = 0;
    avcodec_get_supported_config(ctx, codec, AVCodecConfig(which), 0, &out, &count);
    return static_cast<const T *>(out);
#else
    Q_UNUSED(codec);
    Q_UNUSED(ctx);
    Q_UNUSED(which);
    return legacy;
#endif
}

// Everything one attempt allocates, freed together
struct Job
{
    AVFormatContext *in = nullptr;
    AVFormatContext *out = nullptr;
    AVCodecContext *videoDec = nullptr;
    AVCodecContext *videoEnc = nullptr;
    AVCodecContext *audioDec = nullptr;
    AVCodecContext *audioEnc = nullptr;
    SwsContext *scaler = nullptr;
    SwrContext *resampler = nullptr;
    AVAudioFifo *fifo = nullptr;
    AVFrame *frame = nullptr;
    AVFrame *scaled = nullptr;
    AVFrame *audioFrame = nullptr;
    AVPacket *packet = nullptr;
    int videoIn = -1;
    int audioIn = -1;
    AVStream *videoOut = nullptr;
    AVStream *audioOut = nullptr;
    qint64 audioPts = 0;
    qint64 lastVideoPtsMs = std::numeric_limits<qint64>::min();
    double minFrameGapMs = 0;
    qint64 limit = 0;
    bool tooBig = false;
    qint64 endMs = 0; // frames after this are left out (0: none)
    qint64 videoStartMs = 0;
    qint64 audioStartMs = 0;

    ~Job()
    {
        if (out) {
            if (out->pb && !(out->oformat->flags & AVFMT_NOFILE))
                avio_closep(&out->pb);
            avformat_free_context(out);
        }
        avformat_close_input(&in);
        avcodec_free_context(&videoDec);
        avcodec_free_context(&videoEnc);
        avcodec_free_context(&audioDec);
        avcodec_free_context(&audioEnc);
        sws_freeContext(scaler);
        swr_free(&resampler);
        if (fifo)
            av_audio_fifo_free(fifo);
        av_frame_free(&frame);
        av_frame_free(&scaled);
        av_frame_free(&audioFrame);
        av_packet_free(&packet);
    }

    bool write(AVCodecContext *enc, AVStream *stream)
    {
        while (true) {
            const int r = avcodec_receive_packet(enc, packet);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
                return true;
            if (r < 0)
                return false;
            av_packet_rescale_ts(packet, enc->time_base, stream->time_base);
            packet->stream_index = stream->index;
            if (av_interleaved_write_frame(out, packet) < 0)
                return false;
            // Stop early once it can't fit any more
            if (out->pb && avio_tell(out->pb) > limit) {
                tooBig = true;
                return false;
            }
        }
    }

    bool encodeVideo(AVFrame *source)
    {
        if (source) {
            // Fewer frames when the source has more than the plan keeps
            const bool first = lastVideoPtsMs == std::numeric_limits<qint64>::min();
            const qint64 ptsMs = source->best_effort_timestamp == AV_NOPTS_VALUE
                                         ? (first ? 0 : lastVideoPtsMs + qint64(std::ceil(minFrameGapMs)))
                                         : av_rescale_q(source->best_effort_timestamp,
                                                        in->streams[videoIn]->time_base, AVRational { 1, 1000 });
            if (endMs > 0 && ptsMs - videoStartMs > endMs)
                return true;
            if (!first && ptsMs - lastVideoPtsMs < minFrameGapMs - 1)
                return true;
            lastVideoPtsMs = ptsMs;
            if (!scaler || source->width != scaled->width || source->format != scaled->format) {
                sws_freeContext(scaler);
                scaler = sws_getContext(source->width, source->height, AVPixelFormat(source->format), videoEnc->width,
                                        videoEnc->height, videoEnc->pix_fmt, SWS_BICUBIC, nullptr, nullptr, nullptr);
                if (!scaler)
                    return false;
            }
            if (av_frame_make_writable(scaled) < 0)
                return false;
            sws_scale(scaler, source->data, source->linesize, 0, source->height, scaled->data, scaled->linesize);
            scaled->pts = ptsMs;
            if (avcodec_send_frame(videoEnc, scaled) < 0)
                return false;
        } else if (avcodec_send_frame(videoEnc, nullptr) < 0) {
            return false;
        }
        return write(videoEnc, videoOut);
    }

    bool encodeAudio(bool flush)
    {
        const int frameSize = audioEnc->frame_size > 0 ? audioEnc->frame_size : 1024;
        while (av_audio_fifo_size(fifo) >= frameSize || (flush && av_audio_fifo_size(fifo) > 0)) {
            const int n = qMin(frameSize, av_audio_fifo_size(fifo));
            av_frame_unref(audioFrame);
            audioFrame->nb_samples = n;
            audioFrame->format = audioEnc->sample_fmt;
            audioFrame->sample_rate = audioEnc->sample_rate;
            if (av_channel_layout_copy(&audioFrame->ch_layout, &audioEnc->ch_layout) < 0 ||
                av_frame_get_buffer(audioFrame, 0) < 0)
                return false;
            if (av_audio_fifo_read(fifo, reinterpret_cast<void **>(audioFrame->data), n) < n)
                return false;
            audioFrame->pts = audioPts;
            audioPts += n;
            if (avcodec_send_frame(audioEnc, audioFrame) < 0 || !write(audioEnc, audioOut))
                return false;
        }
        if (flush) {
            if (avcodec_send_frame(audioEnc, nullptr) < 0)
                return false;
            return write(audioEnc, audioOut);
        }
        return true;
    }

    bool takeAudio(AVFrame *decoded)
    {
        if (decoded && endMs > 0 && decoded->best_effort_timestamp != AV_NOPTS_VALUE &&
            av_rescale_q(decoded->best_effort_timestamp, in->streams[audioIn]->time_base, AVRational { 1, 1000 }) -
                            audioStartMs >
                    endMs)
            return true;
        const int outSamples = swr_get_out_samples(resampler, decoded ? decoded->nb_samples : 0);
        if (outSamples <= 0)
            return true;
        uint8_t **buffer = nullptr;
        if (av_samples_alloc_array_and_samples(&buffer, nullptr, audioEnc->ch_layout.nb_channels, outSamples,
                                               audioEnc->sample_fmt, 0) < 0)
            return false;
        const int got = swr_convert(resampler, buffer, outSamples,
                                    decoded ? const_cast<const uint8_t **>(decoded->extended_data) : nullptr,
                                    decoded ? decoded->nb_samples : 0);
        bool ok = got >= 0;
        if (ok && got > 0)
            ok = av_audio_fifo_write(fifo, reinterpret_cast<void **>(buffer), got) == got;
        av_freep(&buffer[0]);
        av_freep(&buffer);
        return ok && encodeAudio(false);
    }

    // Decodes what `dec` has ready and hands it on
    bool drain(AVCodecContext *dec, bool video)
    {
        while (true) {
            const int r = avcodec_receive_frame(dec, frame);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
                return true;
            if (r < 0)
                return false;
            const bool ok = video ? encodeVideo(frame) : takeAudio(frame);
            av_frame_unref(frame);
            if (!ok)
                return false;
        }
    }
};

AVCodecContext *openDecoder(AVStream *stream)
{
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec)
        return nullptr;
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    if (!ctx)
        return nullptr;
    ctx->thread_count = 0;
    if (avcodec_parameters_to_context(ctx, stream->codecpar) < 0 || avcodec_open2(ctx, codec, nullptr) < 0) {
        avcodec_free_context(&ctx);
        return nullptr;
    }
    ctx->pkt_timebase = stream->time_base;
    return ctx;
}

AVCodecContext *openVideoEncoder(const ShrinkPlan &plan, bool globalHeader)
{
    // Software first: the result is a file, so quality per bit matters more than speed
    for (const char *name : { "libx264", "libopenh264", "h264_videotoolbox", "h264_nvenc", "h264_amf", "h264_qsv",
                              "h264_mf" }) {
        const AVCodec *codec = avcodec_find_encoder_by_name(name);
        if (!codec)
            continue;
        AVCodecContext *ctx = avcodec_alloc_context3(codec);
        if (!ctx)
            continue;
        const AVPixelFormat *formats =
                supported<AVPixelFormat>(codec, ctx, 0 /* AV_CODEC_CONFIG_PIX_FORMAT */, codec->pix_fmts);
        AVPixelFormat format = AV_PIX_FMT_YUV420P;
        if (formats) {
            format = formats[0];
            for (const AVPixelFormat *f = formats; *f != AV_PIX_FMT_NONE; ++f)
                if (*f == AV_PIX_FMT_YUV420P || *f == AV_PIX_FMT_NV12) {
                    format = *f;
                    break;
                }
        }
        ctx->width = plan.width;
        ctx->height = plan.height;
        ctx->pix_fmt = format;
        ctx->time_base = AVRational { 1, 1000 };
        ctx->framerate = av_d2q(plan.fps, 1000);
        ctx->bit_rate = plan.videoBitrate;
        ctx->rc_max_rate = qint64(plan.videoBitrate * 1.5);
        ctx->rc_buffer_size = int(qMin<qint64>(plan.videoBitrate * 2, std::numeric_limits<int>::max()));
        ctx->gop_size = int(std::lround(plan.fps * 5));
        ctx->thread_count = 0;
        if (globalHeader)
            ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (qstrcmp(name, "libx264") == 0) {
            setOption(ctx, "preset", "faster");
            setOption(ctx, "profile", "high");
        } else if (qstrcmp(name, "h264_nvenc") == 0) {
            setOption(ctx, "preset", "p5");
            setOption(ctx, "rc", "vbr");
        } else if (qstrcmp(name, "h264_mf") == 0) {
            setOption(ctx, "rate_control", "cbr");
        }
        if (avcodec_open2(ctx, codec, nullptr) >= 0)
            return ctx;
        avcodec_free_context(&ctx);
    }
    return nullptr;
}

AVCodecContext *openAudioEncoder(qint64 bitrate, bool globalHeader)
{
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec)
        return nullptr;
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    if (!ctx)
        return nullptr;
    ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    const AVSampleFormat *formats =
            supported<AVSampleFormat>(codec, ctx, 3 /* AV_CODEC_CONFIG_SAMPLE_FORMAT */, codec->sample_fmts);
    if (formats && formats[0] != AV_SAMPLE_FMT_NONE)
        ctx->sample_fmt = formats[0];
    ctx->sample_rate = 48000;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    av_channel_layout_copy(&ctx->ch_layout, &stereo);
    ctx->bit_rate = bitrate;
    ctx->time_base = AVRational { 1, ctx->sample_rate };
    if (globalHeader)
        ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(ctx, codec, nullptr) < 0) {
        avcodec_free_context(&ctx);
        return nullptr;
    }
    return ctx;
}

// Phone videos are stored sideways with a rotation to show them upright
void copyRotation(const AVStream *from, AVStream *to)
{
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(60, 31, 102)
    const AVPacketSideData *matrix = av_packet_side_data_get(from->codecpar->coded_side_data,
                                                             from->codecpar->nb_coded_side_data,
                                                             AV_PKT_DATA_DISPLAYMATRIX);
    if (!matrix)
        return;
    if (AVPacketSideData *copy = av_packet_side_data_new(&to->codecpar->coded_side_data,
                                                         &to->codecpar->nb_coded_side_data,
                                                         AV_PKT_DATA_DISPLAYMATRIX, matrix->size, 0))
        memcpy(copy->data, matrix->data, matrix->size);
#else
    size_t size = 0;
    const uint8_t *matrix = av_stream_get_side_data(from, AV_PKT_DATA_DISPLAYMATRIX, &size);
    if (!matrix)
        return;
    if (uint8_t *copy = av_stream_new_side_data(to, AV_PKT_DATA_DISPLAYMATRIX, size))
        memcpy(copy, matrix, size);
#endif
}

enum class Attempt { Done, TooBig, Failed };

qint64 startMs(const AVStream *stream)
{
    return stream->start_time == AV_NOPTS_VALUE ? 0
                                                : av_rescale_q(stream->start_time, stream->time_base, AVRational { 1, 1000 });
}

Attempt transcode(const QString &input, const QString &output, ShrinkPlan plan, qint64 limit, double seconds,
                  qint64 endMs, const std::function<bool(double)> &progress, QString *error)
{
    Job job;
    job.limit = limit;
    job.endMs = endMs;
    const QByteArray inPath = QFile::encodeName(input);
    if (avformat_open_input(&job.in, inPath.constData(), nullptr, nullptr) < 0 ||
        avformat_find_stream_info(job.in, nullptr) < 0) {
        if (error)
            *error = QStringLiteral("Couldn't read the video");
        return Attempt::Failed;
    }
    job.videoIn = av_find_best_stream(job.in, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    job.audioIn = plan.audioBitrate > 0 ? av_find_best_stream(job.in, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0) : -1;
    if (job.videoIn < 0 || !(job.videoDec = openDecoder(job.in->streams[job.videoIn]))) {
        if (error)
            *error = QStringLiteral("Couldn't decode the video");
        return Attempt::Failed;
    }
    if (job.audioIn >= 0 && !(job.audioDec = openDecoder(job.in->streams[job.audioIn])))
        job.audioIn = -1;
    job.videoStartMs = startMs(job.in->streams[job.videoIn]);
    if (job.audioIn >= 0)
        job.audioStartMs = startMs(job.in->streams[job.audioIn]);

    const QByteArray outPath = QFile::encodeName(output);
    if (avformat_alloc_output_context2(&job.out, nullptr, "mp4", outPath.constData()) < 0 || !job.out)
        return Attempt::Failed;
    const bool globalHeader = job.out->oformat->flags & AVFMT_GLOBALHEADER;

    job.videoEnc = openVideoEncoder(plan, globalHeader);
    if (!job.videoEnc) {
        if (error)
            *error = QStringLiteral("No H.264 encoder is available");
        return Attempt::Failed;
    }
    job.videoOut = avformat_new_stream(job.out, nullptr);
    if (!job.videoOut || avcodec_parameters_from_context(job.videoOut->codecpar, job.videoEnc) < 0)
        return Attempt::Failed;
    job.videoOut->time_base = job.videoEnc->time_base;
    copyRotation(job.in->streams[job.videoIn], job.videoOut);

    if (job.audioIn >= 0) {
        job.audioEnc = openAudioEncoder(plan.audioBitrate, globalHeader);
        if (job.audioEnc) {
            job.audioOut = avformat_new_stream(job.out, nullptr);
            if (!job.audioOut || avcodec_parameters_from_context(job.audioOut->codecpar, job.audioEnc) < 0)
                return Attempt::Failed;
            job.audioOut->time_base = job.audioEnc->time_base;
            if (swr_alloc_set_opts2(&job.resampler, &job.audioEnc->ch_layout, job.audioEnc->sample_fmt,
                                    job.audioEnc->sample_rate, &job.audioDec->ch_layout, job.audioDec->sample_fmt,
                                    job.audioDec->sample_rate, 0, nullptr) < 0 ||
                swr_init(job.resampler) < 0)
                return Attempt::Failed;
            job.fifo = av_audio_fifo_alloc(job.audioEnc->sample_fmt, job.audioEnc->ch_layout.nb_channels, 4096);
            if (!job.fifo)
                return Attempt::Failed;
        } else {
            job.audioIn = -1; // no AAC encoder: the video goes without sound
        }
    }

    job.frame = av_frame_alloc();
    job.scaled = av_frame_alloc();
    job.audioFrame = av_frame_alloc();
    job.packet = av_packet_alloc();
    if (!job.frame || !job.scaled || !job.audioFrame || !job.packet)
        return Attempt::Failed;
    job.scaled->format = job.videoEnc->pix_fmt;
    job.scaled->width = job.videoEnc->width;
    job.scaled->height = job.videoEnc->height;
    if (av_frame_get_buffer(job.scaled, 0) < 0)
        return Attempt::Failed;
    job.minFrameGapMs = 1000.0 / plan.fps;

    if (avio_open(&job.out->pb, outPath.constData(), AVIO_FLAG_WRITE) < 0)
        return Attempt::Failed;
    AVDictionary *options = nullptr;
    av_dict_set(&options, "movflags", "+faststart", 0);
    const int header = avformat_write_header(job.out, &options);
    av_dict_free(&options);
    if (header < 0)
        return Attempt::Failed;

    AVPacket *read = av_packet_alloc();
    bool ok = read != nullptr;
    bool stopped = false;
    int packets = 0;
    while (ok && av_read_frame(job.in, read) >= 0) {
        if (read->stream_index == job.videoIn && (endMs > 0 || progress)) {
            const qint64 ts = read->pts != AV_NOPTS_VALUE ? read->pts : read->dts;
            const qint64 atMs = ts == AV_NOPTS_VALUE ? 0
                                                     : av_rescale_q(ts, job.in->streams[job.videoIn]->time_base,
                                                                    AVRational { 1, 1000 }) -
                                                               job.videoStartMs;
            // Past the end (with room for frames stored out of order)
            if (endMs > 0 && atMs > endMs + 2000) {
                av_packet_unref(read);
                break;
            }
            if (progress && ++packets % 10 == 0 && !progress(qBound(0.0, atMs / (seconds * 1000.0), 1.0))) {
                stopped = true;
                av_packet_unref(read);
                break;
            }
        }
        if (read->stream_index == job.videoIn)
            ok = avcodec_send_packet(job.videoDec, read) >= 0 && job.drain(job.videoDec, true);
        else if (read->stream_index == job.audioIn)
            ok = avcodec_send_packet(job.audioDec, read) >= 0 && job.drain(job.audioDec, false);
        av_packet_unref(read);
    }
    av_packet_free(&read);
    if (stopped) {
        if (error)
            *error = QStringLiteral("Stopped");
        return Attempt::Failed;
    }
    if (ok)
        ok = avcodec_send_packet(job.videoDec, nullptr) >= 0 && job.drain(job.videoDec, true) &&
             job.encodeVideo(nullptr);
    if (ok && job.audioIn >= 0)
        ok = avcodec_send_packet(job.audioDec, nullptr) >= 0 && job.drain(job.audioDec, false) &&
             job.takeAudio(nullptr) && job.encodeAudio(true);
    if (job.tooBig)
        return Attempt::TooBig;
    if (!ok || av_write_trailer(job.out) < 0) {
        if (error)
            *error = QStringLiteral("Re-encoding the video failed");
        return Attempt::Failed;
    }
    return Attempt::Done;
}

} // namespace

std::optional<ShrinkPlan> planShrink(int width, int height, double fps, double seconds, bool hasAudio,
                                     qint64 limitBytes, Priority priority)
{
    if (width <= 0 || height <= 0 || seconds <= 0 || limitBytes <= 0)
        return std::nullopt;
    ShrinkPlan plan;
    const double totalBitrate = limitBytes * 8.0 * Overhead / seconds;
    plan.audioBitrate = hasAudio ? (totalBitrate > 600'000 ? 128'000 : 64'000) : 0;
    plan.videoBitrate = qint64(totalBitrate) - plan.audioBitrate;
    if (plan.videoBitrate < 60'000)
        return std::nullopt;
    const double maxFps = priority == Priority::Smoothness ? 60.0 : priority == Priority::Sharpness ? 15.0 : 30.0;
    plan.fps = qBound(1.0, fps > 0 ? fps : 30.0, maxFps);
    // The largest of these heights (never more than the source's) the bitrate can carry
    const QList<int> heights { height, 1080, 720, 540, 480, 360, 240, 144 };
    int chosen = 144;
    for (int h : heights) {
        if (h > height)
            continue;
        const double w = double(width) * h / height;
        if (w * h * plan.fps * BitsPerPixel <= plan.videoBitrate) {
            chosen = h;
            break;
        }
    }
    chosen = qMin(chosen, height);
    // Below 240 lines, fewer frames before fewer lines (unless smoothness comes first)
    if (chosen < 240 && plan.fps > 15 && priority != Priority::Smoothness) {
        plan.fps = 15;
        for (int h : heights) {
            if (h > height || h < chosen)
                continue;
            const double w = double(width) * h / height;
            if (w * h * plan.fps * BitsPerPixel <= plan.videoBitrate) {
                chosen = qMax(chosen, h);
                break;
            }
        }
    }
    plan.height = qMax(2, chosen & ~1);
    plan.width = qMax(2, int(std::lround(double(width) * plan.height / height)) & ~1);
    return plan;
}

double secondsThatFit(bool hasAudio, qint64 limitBytes)
{
    // planShrink's least: 60 kbit/s of video, 64 kbit/s of sound
    const double bitrate = 60'000.0 + (hasAudio ? 64'000.0 : 0.0);
    return std::floor(limitBytes * 8.0 * Overhead / bitrate * 0.98);
}

std::optional<ShrinkResult> shrinkVideo(const QString &path, qint64 limitBytes, const ShrinkOptions &options,
                                        QString *error)
{
    // Size and length, from a first look at the file
    int width = 0;
    int height = 0;
    double fps = 30;
    double seconds = 0;
    bool hasAudio = false;
    {
        AVFormatContext *probe = nullptr;
        const QByteArray inPath = QFile::encodeName(path);
        if (avformat_open_input(&probe, inPath.constData(), nullptr, nullptr) < 0 ||
            avformat_find_stream_info(probe, nullptr) < 0) {
            avformat_close_input(&probe);
            if (error)
                *error = QStringLiteral("Couldn't read the video");
            return std::nullopt;
        }
        const int v = av_find_best_stream(probe, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (v >= 0) {
            width = probe->streams[v]->codecpar->width;
            height = probe->streams[v]->codecpar->height;
            const AVRational rate = av_guess_frame_rate(probe, probe->streams[v], nullptr);
            if (rate.num > 0 && rate.den > 0)
                fps = av_q2d(rate);
        }
        hasAudio = av_find_best_stream(probe, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0) >= 0;
        if (probe->duration > 0)
            seconds = double(probe->duration) / AV_TIME_BASE;
        avformat_close_input(&probe);
        if (v < 0) {
            if (error)
                *error = QStringLiteral("There's no video in the file");
            return std::nullopt;
        }
    }

    // Only the start, when asked for or when the whole won't fit
    bool trimmed = false;
    if (options.maxSeconds > 0 && seconds > options.maxSeconds) {
        seconds = options.maxSeconds;
        trimmed = true;
    }
    std::optional<ShrinkPlan> plan = planShrink(width, height, fps, seconds, hasAudio, limitBytes, options.priority);
    if (!plan && options.trimToFit) {
        const double fit = secondsThatFit(hasAudio, limitBytes);
        if (fit >= 1 && fit < seconds) {
            seconds = fit;
            trimmed = true;
            plan = planShrink(width, height, fps, seconds, hasAudio, limitBytes, options.priority);
        }
    }
    if (!plan) {
        if (error)
            *error = QStringLiteral("The video is too long to fit");
        return std::nullopt;
    }
    const qint64 endMs = trimmed ? qint64(seconds * 1000) : 0;
    // Only ever forward: frames come out of order, and a retry starts over
    double reached = 0;
    std::function<bool(double)> progress;
    if (options.progress)
        progress = [&](double fraction) {
            reached = qMax(reached, fraction);
            return options.progress(reached);
        };

    QTemporaryFile output(QStringLiteral("%1/qord-shrink-XXXXXX.mp4").arg(QDir::tempPath()));
    if (!output.open())
        return std::nullopt;
    output.close();
    // Lower the bitrate and try again while the encoder overshoots
    for (int attempt = 0; attempt < 4; ++attempt) {
        const Attempt result = transcode(path, output.fileName(), *plan, limitBytes, seconds, endMs, progress,
                                         error);
        if (result == Attempt::Failed)
            return std::nullopt;
        if (result == Attempt::Done) {
            QFile file(output.fileName());
            if (file.open(QIODevice::ReadOnly) && file.size() <= limitBytes)
                return ShrinkResult { file.readAll(), seconds, trimmed };
        }
        plan = planShrink(width, height, fps, seconds, hasAudio, qint64(limitBytes * std::pow(0.85, attempt + 1)),
                          options.priority);
        if (!plan)
            break;
    }
    if (error)
        *error = QStringLiteral("The video can't be made small enough");
    return std::nullopt;
}

std::optional<QByteArray> shrinkVideo(const QString &path, qint64 limitBytes, QString *error)
{
    std::optional<ShrinkResult> result = shrinkVideo(path, limitBytes, ShrinkOptions(), error);
    if (!result)
        return std::nullopt;
    return result->data;
}

} // namespace Video
} // namespace Qord
