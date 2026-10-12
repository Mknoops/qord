#include "VideoCodec.hpp"

#include "H264Rtp.hpp"

#include <QDebug>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#ifndef AV_PROFILE_H264_CONSTRAINED_BASELINE
#define AV_PROFILE_H264_CONSTRAINED_BASELINE FF_PROFILE_H264_CONSTRAINED_BASELINE
#endif

namespace Qord {
namespace Video {

namespace {

void setOption(AVCodecContext *ctx, const char *key, const char *value)
{
    // Options an encoder does not have are ignored
    av_opt_set(ctx->priv_data, key, value, 0);
}

AVPixelFormat pickPixelFormat(const AVCodec *codec, AVCodecContext *ctx)
{
    const AVPixelFormat *formats = nullptr;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    int count = 0;
    avcodec_get_supported_config(ctx, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0,
                                 reinterpret_cast<const void **>(&formats), &count);
#else
    Q_UNUSED(ctx);
    formats = codec->pix_fmts;
#endif
    if (!formats)
        return AV_PIX_FMT_YUV420P;
    // 4:2:0 is what every decoder takes
    for (const AVPixelFormat *f = formats; *f != AV_PIX_FMT_NONE; ++f)
        if (*f == AV_PIX_FMT_NV12 || *f == AV_PIX_FMT_YUV420P)
            return *f;
    return AV_PIX_FMT_NONE;
}

} // namespace

H264Encoder::~H264Encoder()
{
    close();
}

QStringList H264Encoder::defaultEncoders()
{
    // Media Foundation last: it may not honour forced keyframes (the
    // periodic ones still come), so receivers that lost packets wait longer
    return { QStringLiteral("h264_nvenc"), QStringLiteral("h264_amf"), QStringLiteral("h264_qsv"),
             QStringLiteral("libopenh264"), QStringLiteral("h264_mf") };
}

void H264Encoder::close()
{
    if (scaler)
        sws_freeContext(scaler);
    scaler = nullptr;
    scalerSource = {};
    scalerSourceFormat = -1;
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&context);
    parameterSets.clear();
    name.clear();
    pts = 0;
}

bool H264Encoder::open(const Settings &settings, const QStringList &preference)
{
    close();
    current = settings;
    // 4:2:0 needs even sizes
    current.width = qMax(2, settings.width & ~1);
    current.height = qMax(2, settings.height & ~1);
    current.fps = qBound(1, settings.fps, 60);
    for (const QString &encoder : preference.isEmpty() ? defaultEncoders() : preference) {
        if (openWith(encoder)) {
            name = encoder;
            return true;
        }
        close();
        current.width = qMax(2, settings.width & ~1);
        current.height = qMax(2, settings.height & ~1);
        current.fps = qBound(1, settings.fps, 60);
    }
    return false;
}

bool H264Encoder::openWith(const QString &encoder)
{
    const AVCodec *codec = avcodec_find_encoder_by_name(encoder.toLatin1().constData());
    if (!codec)
        return false;
    context = avcodec_alloc_context3(codec);
    if (!context)
        return false;
    const AVPixelFormat format = pickPixelFormat(codec, context);
    if (format == AV_PIX_FMT_NONE)
        return false;

    context->width = current.width;
    context->height = current.height;
    context->pix_fmt = format;
    context->time_base = AVRational { 1, current.fps };
    context->framerate = AVRational { current.fps, 1 };
    context->bit_rate = qint64(current.bitrateKbps) * 1000;
    context->rc_max_rate = context->bit_rate;
    context->rc_buffer_size = int(context->bit_rate); // about a second
    context->gop_size = current.fps * qMax(1, current.keyframeIntervalSeconds);
    context->max_b_frames = 0;
    context->flags |= AV_CODEC_FLAG_LOW_DELAY;
    context->thread_count = 0;
    context->profile = AV_PROFILE_H264_CONSTRAINED_BASELINE;

    if (encoder == QLatin1String("h264_nvenc")) {
        setOption(context, "preset", "p2");
        setOption(context, "tune", "ll");
        setOption(context, "rc", "cbr");
        setOption(context, "zerolatency", "1");
        setOption(context, "forced-idr", "1");
        setOption(context, "profile", "baseline");
    } else if (encoder == QLatin1String("h264_amf")) {
        setOption(context, "usage", "lowlatency");
        setOption(context, "quality", "speed");
        setOption(context, "rc", "cbr");
        setOption(context, "profile", "constrained_baseline");
        setOption(context, "forced_idr", "1");
        setOption(context, "header_spacing", QByteArray::number(context->gop_size).constData());
    } else if (encoder == QLatin1String("h264_qsv")) {
        setOption(context, "preset", "veryfast");
        setOption(context, "profile", "baseline");
    } else if (encoder == QLatin1String("h264_mf")) {
        setOption(context, "rate_control", "cbr");
        setOption(context, "scenario", "display_remoting");
    } else if (encoder == QLatin1String("libopenh264")) {
        setOption(context, "profile", "constrained_baseline");
        setOption(context, "allow_skip_frames", "0");
    }

    if (avcodec_open2(context, codec, nullptr) < 0)
        return false;

    if (context->extradata && context->extradata_size > 4) {
        const QByteArray extra(reinterpret_cast<const char *>(context->extradata), context->extradata_size);
        // Annex B extradata can be put in front of keyframes as it is
        if (extra.startsWith(QByteArray("\0\0\1", 3)) || extra.startsWith(QByteArray("\0\0\0\1", 4)))
            parameterSets = extra;
    }

    frame = av_frame_alloc();
    packet = av_packet_alloc();
    if (!frame || !packet)
        return false;
    frame->format = format;
    frame->width = current.width;
    frame->height = current.height;
    if (av_frame_get_buffer(frame, 0) < 0)
        return false;
    return true;
}

QList<QByteArray> H264Encoder::drain()
{
    QList<QByteArray> out;
    while (avcodec_receive_packet(context, packet) == 0) {
        QByteArray unit(reinterpret_cast<const char *>(packet->data), packet->size);
        const bool key = packet->flags & AV_PKT_FLAG_KEY;
        av_packet_unref(packet);
        if (key && !parameterSets.isEmpty()) {
            // Receivers joining late need SPS/PPS with each keyframe
            bool hasSps = false;
            for (const QByteArray &nal : splitAnnexB(unit))
                hasSps = hasSps || (!nal.isEmpty() && (uint8_t(nal[0]) & 0x1F) == 7);
            if (!hasSps)
                unit.prepend(parameterSets);
        }
        out.append(unit);
    }
    return out;
}

QList<QByteArray> H264Encoder::encode(const QImage &image, bool forceKeyframe)
{
    if (!context || image.isNull())
        return {};
    QImage source = image;
    if (source.format() != QImage::Format_RGB32 && source.format() != QImage::Format_ARGB32 &&
        source.format() != QImage::Format_ARGB32_Premultiplied)
        source = source.convertToFormat(QImage::Format_RGB32);

    if (!scaler || scalerSource != source.size()) {
        if (scaler)
            sws_freeContext(scaler);
        scaler = sws_getContext(source.width(), source.height(), AV_PIX_FMT_BGRA, current.width, current.height,
                                AVPixelFormat(frame->format), SWS_BILINEAR, nullptr, nullptr, nullptr);
        scalerSource = source.size();
        if (!scaler)
            return {};
    }
    if (av_frame_make_writable(frame) < 0)
        return {};
    const uint8_t *src[1] = { source.constBits() };
    const int srcStride[1] = { int(source.bytesPerLine()) };
    sws_scale(scaler, src, srcStride, 0, source.height(), frame->data, frame->linesize);

    frame->pts = pts++;
    frame->pict_type = forceKeyframe ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
#ifdef AV_FRAME_FLAG_KEY
    if (forceKeyframe)
        frame->flags |= AV_FRAME_FLAG_KEY;
    else
        frame->flags &= ~AV_FRAME_FLAG_KEY;
#endif
    int sent = avcodec_send_frame(context, frame);
    QList<QByteArray> out;
    if (sent == AVERROR(EAGAIN)) {
        // Asynchronous encoders (Media Foundation) want their output taken first
        out = drain();
        sent = avcodec_send_frame(context, frame);
    }
    if (sent < 0)
        return out;
    return out + drain();
}

H264Decoder::~H264Decoder()
{
    if (scaler)
        sws_freeContext(scaler);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&context);
}

bool H264Decoder::ensureOpen()
{
    if (context)
        return true;
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec)
        return false;
    context = avcodec_alloc_context3(codec);
    if (!context)
        return false;
    context->flags |= AV_CODEC_FLAG_LOW_DELAY;
    context->thread_count = 2;
    if (avcodec_open2(context, codec, nullptr) < 0) {
        avcodec_free_context(&context);
        return false;
    }
    frame = av_frame_alloc();
    packet = av_packet_alloc();
    return frame && packet;
}

QImage H264Decoder::decode(const QByteArray &annexB)
{
    if (annexB.isEmpty() || !ensureOpen())
        return {};
    if (av_new_packet(packet, int(annexB.size())) < 0)
        return {};
    memcpy(packet->data, annexB.constData(), size_t(annexB.size()));
    const int sent = avcodec_send_packet(context, packet);
    av_packet_unref(packet);
    if (sent < 0)
        return {};

    QImage latest;
    while (avcodec_receive_frame(context, frame) == 0) {
        if (!scaler || scalerWidth != frame->width || scalerHeight != frame->height || scalerFormat != frame->format) {
            if (scaler)
                sws_freeContext(scaler);
            scaler = sws_getContext(frame->width, frame->height, AVPixelFormat(frame->format), frame->width,
                                    frame->height, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            scalerWidth = frame->width;
            scalerHeight = frame->height;
            scalerFormat = frame->format;
        }
        if (scaler) {
            QImage image(frame->width, frame->height, QImage::Format_RGB32);
            uint8_t *dst[1] = { image.bits() };
            const int dstStride[1] = { int(image.bytesPerLine()) };
            sws_scale(scaler, frame->data, frame->linesize, 0, frame->height, dst, dstStride);
            latest = image;
        }
        av_frame_unref(frame);
    }
    return latest;
}

} // namespace Video
} // namespace Qord
