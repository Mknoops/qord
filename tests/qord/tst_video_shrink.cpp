#include "Qord/Video/VideoShrink.hpp"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <cstdint>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

using namespace Qord::Video;

namespace {

// A large test clip: noisy frames in MPEG-4 Part 2 (ffmpeg's own encoder, so
// it's always there) at a high bitrate, with a sine tone in AAC when there is
// an AAC encoder
bool writeClip(const QString &path, int width, int height, int fps, int seconds)
{
    AVFormatContext *out = nullptr;
    const QByteArray name = QFile::encodeName(path);
    if (avformat_alloc_output_context2(&out, nullptr, "mp4", name.constData()) < 0)
        return false;
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    AVCodecContext *enc = codec ? avcodec_alloc_context3(codec) : nullptr;
    if (!enc)
        return false;
    enc->width = width;
    enc->height = height;
    enc->pix_fmt = AV_PIX_FMT_YUV420P;
    enc->time_base = AVRational { 1, fps };
    enc->framerate = AVRational { fps, 1 };
    enc->bit_rate = 20'000'000;
    enc->qmin = enc->qmax = 2; // noise at the best quality: large
    if (out->oformat->flags & AVFMT_GLOBALHEADER)
        enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(enc, codec, nullptr) < 0)
        return false;
    AVStream *stream = avformat_new_stream(out, nullptr);
    avcodec_parameters_from_context(stream->codecpar, enc);
    stream->time_base = enc->time_base;

    const AVCodec *aac = avcodec_find_encoder(AV_CODEC_ID_AAC);
    AVCodecContext *audio = aac ? avcodec_alloc_context3(aac) : nullptr;
    AVStream *audioStream = nullptr;
    if (audio) {
        audio->sample_fmt = AV_SAMPLE_FMT_FLTP;
        audio->sample_rate = 44100;
        AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
        av_channel_layout_copy(&audio->ch_layout, &mono);
        audio->bit_rate = 128'000;
        audio->time_base = AVRational { 1, 44100 };
        if (out->oformat->flags & AVFMT_GLOBALHEADER)
            audio->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(audio, aac, nullptr) < 0) {
            avcodec_free_context(&audio);
        } else {
            audioStream = avformat_new_stream(out, nullptr);
            avcodec_parameters_from_context(audioStream->codecpar, audio);
            audioStream->time_base = audio->time_base;
        }
    }

    if (avio_open(&out->pb, name.constData(), AVIO_FLAG_WRITE) < 0 || avformat_write_header(out, nullptr) < 0)
        return false;
    AVFrame *frame = av_frame_alloc();
    frame->format = enc->pix_fmt;
    frame->width = width;
    frame->height = height;
    av_frame_get_buffer(frame, 0);
    AVPacket *packet = av_packet_alloc();
    const auto drain = [&](AVCodecContext *c, AVStream *s) {
        while (avcodec_receive_packet(c, packet) == 0) {
            av_packet_rescale_ts(packet, c->time_base, s->time_base);
            packet->stream_index = s->index;
            av_interleaved_write_frame(out, packet);
        }
    };
    quint32 seed = 7;
    for (int i = 0; i < fps * seconds; ++i) {
        av_frame_make_writable(frame);
        for (int plane = 0; plane < 3; ++plane) {
            const int h = plane ? height / 2 : height;
            const int w = plane ? width / 2 : width;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    seed = seed * 1664525u + 1013904223u;
                    frame->data[plane][y * frame->linesize[plane] + x] = uint8_t(seed >> 24);
                }
        }
        frame->pts = i;
        avcodec_send_frame(enc, frame);
        drain(enc, stream);
    }
    avcodec_send_frame(enc, nullptr);
    drain(enc, stream);
    if (audio) {
        AVFrame *samples = av_frame_alloc();
        const int total = 44100 * seconds;
        for (int done = 0; done < total; done += audio->frame_size) {
            samples->nb_samples = audio->frame_size;
            samples->format = audio->sample_fmt;
            samples->sample_rate = 44100;
            av_channel_layout_copy(&samples->ch_layout, &audio->ch_layout);
            av_frame_get_buffer(samples, 0);
            auto *data = reinterpret_cast<float *>(samples->data[0]);
            for (int i = 0; i < audio->frame_size; ++i)
                data[i] = float(0.3 * std::sin(2 * 3.14159265358979 * 440 * (done + i) / 44100.0));
            samples->pts = done;
            avcodec_send_frame(audio, samples);
            drain(audio, audioStream);
            av_frame_unref(samples);
        }
        avcodec_send_frame(audio, nullptr);
        drain(audio, audioStream);
        av_frame_free(&samples);
    }
    av_write_trailer(out);
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&enc);
    avcodec_free_context(&audio);
    avio_closep(&out->pb);
    avformat_free_context(out);
    return true;
}

} // namespace

class TestVideoShrink : public QObject
{
    Q_OBJECT

private slots:
    void plans()
    {
        // 60 s of 1080p60 into 10 MB: about 1.3 Mbit/s, so 30 fps at a smaller size
        auto plan = planShrink(1920, 1080, 60, 60, true, 10 * 1024 * 1024);
        QVERIFY(plan);
        QCOMPARE(plan->fps, 30.0);
        QVERIFY(plan->height < 1080);
        QCOMPARE(plan->width % 2, 0);
        QCOMPARE(plan->height % 2, 0);
        QVERIFY((plan->videoBitrate + plan->audioBitrate) * 60 / 8 <= 10 * 1024 * 1024);
        QCOMPARE(plan->width * 1080, plan->height * 1920);

        // Short clips keep their size
        plan = planShrink(640, 360, 30, 5, false, 10 * 1024 * 1024);
        QVERIFY(plan);
        QCOMPARE(plan->height, 360);
        QCOMPARE(plan->audioBitrate, 0);

        // An hour can't fit in 10 MB
        QVERIFY(!planShrink(1920, 1080, 30, 3600, true, 10 * 1024 * 1024));
        QVERIFY(!planShrink(0, 0, 30, 5, true, 1000));
    }

    void shrinksAClip()
    {
        QTemporaryDir dir;
        const QString input = dir.filePath(QStringLiteral("in.mp4"));
        if (!writeClip(input, 640, 360, 30, 4))
            QSKIP("ffmpeg's MPEG-4 encoder isn't available to make the test clip");
        const qint64 inputSize = QFileInfo(input).size();
        QVERIFY(inputSize > 2 * 1024 * 1024);
        const qint64 limit = inputSize / 5;

        QString error;
        const std::optional<QByteArray> out = shrinkVideo(input, limit, &error);
        if (!out && error.contains(QLatin1String("encoder")))
            QSKIP("No H.264 encoder in this ffmpeg");
        QVERIFY2(out, qPrintable(error));
        QVERIFY(out->size() <= limit);

        // It's an MP4 with H.264 (and AAC when the clip had sound)
        const QString result = dir.filePath(QStringLiteral("out.mp4"));
        QFile file(result);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(*out);
        file.close();
        AVFormatContext *probe = nullptr;
        QVERIFY(avformat_open_input(&probe, QFile::encodeName(result).constData(), nullptr, nullptr) == 0);
        QVERIFY(avformat_find_stream_info(probe, nullptr) >= 0);
        const int v = av_find_best_stream(probe, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        QVERIFY(v >= 0);
        QCOMPARE(probe->streams[v]->codecpar->codec_id, AV_CODEC_ID_H264);
        QVERIFY(probe->duration > 3 * AV_TIME_BASE);
        if (avcodec_find_encoder(AV_CODEC_ID_AAC)) {
            const int a = av_find_best_stream(probe, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
            QVERIFY(a >= 0);
            QCOMPARE(probe->streams[a]->codecpar->codec_id, AV_CODEC_ID_AAC);
        }
        avformat_close_input(&probe);
    }

    void refusesWhatIsNotVideo()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("x.mp4"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not a video");
        file.close();
        QString error;
        QVERIFY(!shrinkVideo(path, 1000, &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestVideoShrink)
#include "tst_video_shrink.moc"
