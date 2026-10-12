#include "GoLive.hpp"

#include "Core/Audio/VoiceManager.hpp"
#include "Core/Logging.hpp"
#include "Core/OrderedJson.hpp"
#include "Discord/Client.hpp"
#include "Discord/Voice/VoiceClient.hpp"

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QThread>

namespace Qord {
namespace Video {

using Acheron::Core::Snowflake;
using Acheron::Discord::Voice::VoiceClient;

// Main gateway opcodes
constexpr int OpStreamCreate = 18;
constexpr int OpStreamDelete = 19;
constexpr int OpStreamWatch = 20;
constexpr int OpStreamSetPaused = 22;

VideoSender::VideoSender(const CaptureSource &source, const H264Encoder::Settings &settings)
    : source(source), settings(settings)
{
}

VideoSender::~VideoSender() = default;

void VideoSender::begin()
{
    if (!capturer.start(source)) {
        emit failed(tr("Couldn't capture %1").arg(source.name));
        return;
    }
    if (!encoder.open(settings)) {
        emit failed(tr("No H.264 encoder is available"));
        return;
    }
    emit started(encoder.encoderName());
    startMs = QDateTime::currentMSecsSinceEpoch();
    timer = new QTimer(this);
    timer->setTimerType(Qt::PreciseTimer);
    connect(timer, &QTimer::timeout, this, &VideoSender::tick);
    timer->start(1000 / qMax(1, settings.fps));
}

void VideoSender::end()
{
    if (timer)
        timer->stop();
    capturer.stop();
    encoder.close();
}

void VideoSender::requestKeyframe()
{
    keyframeWanted = true;
}

void VideoSender::tick()
{
    QElapsedTimer timing;
    timing.start();
    const QImage image = capturer.grab();
    const qint64 captureNs = timing.nsecsElapsed();
    if (image.isNull()) {
        if (capturer.sourceGone()) {
            end();
            emit failed(tr("The shared window was closed"));
        }
        return; // no picture yet
    }
    // Keep the source's aspect ratio inside the chosen size
    QSize target = image.size().scaled(QSize(settings.width, settings.height), Qt::KeepAspectRatio);
    if (target.width() > image.width() || target.height() > image.height())
        target = image.size();
    target = QSize(target.width() & ~1, target.height() & ~1);
    if (target != QSize(encoder.settings().width, encoder.settings().height)) {
        H264Encoder::Settings s = settings;
        s.width = target.width();
        s.height = target.height();
        if (!encoder.open(s, { encoder.encoderName() }) && !encoder.open(s)) {
            end();
            emit failed(tr("The encoder stopped"));
            return;
        }
        keyframeWanted = true;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const quint32 timestamp = quint32((now - startMs) * (VideoClockRate / 1000));
    // Periodic keyframes ourselves: not every encoder keeps to its GOP size
    // (AMF doesn't), and viewers joining or losing packets need them
    if (now - lastKeyframeMs >= qint64(settings.keyframeIntervalSeconds) * 1000)
        keyframeWanted = true;
    const bool key = keyframeWanted;
    keyframeWanted = false;
    if (key)
        lastKeyframeMs = now;
    for (const QByteArray &unit : encoder.encode(image, key))
        emit frameEncoded(unit, timestamp);

    // How the stream keeps up, every five seconds
    ++statFrames;
    statCaptureNs += captureNs;
    statTotalNs += timing.nsecsElapsed();
    if (now - statStartMs >= 5000) {
        if (statStartMs)
            qCInfo(LogVoice).nospace() << "Go Live: " << statFrames * 1000.0 / (now - statStartMs) << " fps, capture "
                                       << statCaptureNs / statFrames / 1000000.0 << " ms, capture+encode "
                                       << statTotalNs / statFrames / 1000000.0 << " ms (" << encoder.encoderName()
                                       << ", " << encoder.settings().width << "x" << encoder.settings().height << ")";
        statStartMs = now;
        statFrames = 0;
        statCaptureNs = statTotalNs = 0;
    }
}

void VideoReceiver::onFrame(quint64, quint32, const QByteArray &annexB, bool)
{
    const QImage image = decoder.decode(annexB);
    if (!image.isNull())
        emit frameDecoded(image);
}

GoLive::GoLive(Acheron::Discord::Client *client, Acheron::Core::Audio::VoiceManager *voice, Snowflake accountId,
               QObject *parent)
    : QObject(parent), client(client), voice(voice), accountId(accountId)
{
    connect(client, &Acheron::Discord::Client::streamEvent, this, &GoLive::onStreamEvent);
    if (voice)
        connect(voice, &Acheron::Core::Audio::VoiceManager::voiceDisconnected, this, [this]() {
            // Leaving the call ends your stream and the ones you watch
            stopStreaming();
            for (const QString &key : watched.keys())
                stopWatching(key);
        });
}

GoLive::~GoLive()
{
    if (own)
        closeConnection(own);
    own = nullptr;
    for (Connection *c : std::as_const(watched))
        closeConnection(c);
    watched.clear();
}

QList<GoLive::Quality> GoLive::allowedQualities(bool nitro, int guildBoostTier)
{
    QList<Quality> list { { 1280, 720, 15 }, { 1280, 720, 30 } };
    if (nitro || guildBoostTier >= 2)
        list += { { 1280, 720, 60 }, { 1920, 1080, 30 }, { 1920, 1080, 60 } };
    if (nitro)
        list += { { 2560, 1440, 30 }, { 2560, 1440, 60 }, { 3840, 2160, 30 }, { 3840, 2160, 60 } };
    return list;
}

int GoLive::bitrateKbps(const Quality &quality)
{
    const double pixelsPerSecond = double(quality.width) * quality.height * quality.fps;
    // About 0.09 bits per pixel, 2.5 Mbit/s for 720p30, capped at 10 Mbit/s
    return int(qBound(800.0, pixelsPerSecond * 0.09 / 1000.0, 10000.0));
}

QString GoLive::streamKey(Snowflake guildId, Snowflake channelId, Snowflake userId)
{
    if (guildId.isValid())
        return QStringLiteral("guild:%1:%2:%3").arg(quint64(guildId)).arg(quint64(channelId)).arg(quint64(userId));
    return QStringLiteral("call:%1:%2").arg(quint64(channelId)).arg(quint64(userId));
}

GoLive::Connection *GoLive::connectionFor(const QString &key)
{
    if (own && own->key == key)
        return own;
    return watched.value(key);
}

bool GoLive::startStreaming(const CaptureSource &source, const Quality &quality)
{
    if (!voice || !voice->isConnected() || !voice->currentChannelId().isValid()) {
        emit streamError(tr("Join a voice channel to share your screen"));
        return false;
    }
    stopStreaming();
    own = new Connection;
    own->ownStream = true;
    own->key = streamKey(voice->currentGuildId(), voice->currentChannelId(), accountId);
    own->source = source;
    own->quality = quality;

    Acheron::Core::OrderedJson create;
    create.insert("type", voice->currentGuildId().isValid() ? QStringLiteral("guild") : QStringLiteral("call"));
    create.insert("guild_id", voice->currentGuildId().isValid() ? QJsonValue(QString::number(quint64(voice->currentGuildId())))
                                                                  : QJsonValue(QJsonValue::Null));
    create.insert("channel_id", QString::number(quint64(voice->currentChannelId())));
    create.insert("preferred_region", QJsonValue(QJsonValue::Null));
    client->sendGatewayOp(OpStreamCreate, create);

    Acheron::Core::OrderedJson paused;
    paused.insert("stream_key", own->key);
    paused.insert("paused", false);
    client->sendGatewayOp(OpStreamSetPaused, paused);
    qCInfo(LogVoice) << "Go Live: creating stream" << own->key;
    return true;
}

void GoLive::stopStreaming()
{
    if (!own)
        return;
    Connection *c = own;
    own = nullptr;
    Acheron::Core::OrderedJson del;
    del.insert("stream_key", c->key);
    client->sendGatewayOp(OpStreamDelete, del);
    closeConnection(c);
    ownEncoder.clear();
    emit streamingChanged(false);
}

void GoLive::watch(Snowflake userId)
{
    if (!voice || !voice->currentChannelId().isValid()) {
        emit streamError(tr("Join the voice channel to watch the stream"));
        return;
    }
    const QString key = streamKey(voice->currentGuildId(), voice->currentChannelId(), userId);
    if (watched.contains(key))
        return;
    auto *c = new Connection;
    c->key = key;
    watched.insert(key, c);
    Acheron::Core::OrderedJson d;
    d.insert("stream_key", key);
    client->sendGatewayOp(OpStreamWatch, d);
    qCInfo(LogVoice) << "Go Live: watching" << key;
}

void GoLive::stopWatching(const QString &key)
{
    Connection *c = watched.take(key);
    if (!c)
        return;
    Acheron::Core::OrderedJson del;
    del.insert("stream_key", key);
    client->sendGatewayOp(OpStreamDelete, del);
    closeConnection(c);
    emit watchEnded(key, QString());
}

void GoLive::onStreamEvent(const QString &type, const QJsonObject &data)
{
    const QString key = data.value(QStringLiteral("stream_key")).toString();
    Connection *c = connectionFor(key);
    if (!c)
        return;
    if (type == QLatin1String("STREAM_CREATE")) {
        c->rtcServerId = data.value(QStringLiteral("rtc_server_id")).toString();
    } else if (type == QLatin1String("STREAM_SERVER_UPDATE")) {
        c->endpoint = data.value(QStringLiteral("endpoint")).toString();
        c->token = data.value(QStringLiteral("token")).toString();
    } else if (type == QLatin1String("STREAM_UPDATE")) {
        if (c->ownStream) {
            QList<Snowflake> viewers;
            for (const auto &v : data.value(QStringLiteral("viewer_ids")).toArray())
                viewers.append(Snowflake(v.toString().toULongLong()));
            emit viewersChanged(viewers);
        }
        return;
    } else if (type == QLatin1String("STREAM_DELETE")) {
        const QString reason = data.value(QStringLiteral("reason")).toString();
        if (c == own) {
            own = nullptr;
            closeConnection(c);
            emit streamingChanged(false);
            if (!reason.isEmpty() && reason != QLatin1String("user_requested"))
                emit streamError(tr("Your stream ended (%1)").arg(reason));
        } else {
            watched.remove(key);
            closeConnection(c);
            emit watchEnded(key, reason);
        }
        return;
    }
    if (!c->thread && !c->rtcServerId.isEmpty() && !c->endpoint.isEmpty() && !c->token.isEmpty())
        connectStream(c);
}

void GoLive::connectStream(Connection *c)
{
    if (!voice)
        return;
    c->thread = new QThread(this);
    c->thread->setObjectName(QStringLiteral("GoLiveThread"));
    c->client = new VoiceClient(c->endpoint, c->token, Snowflake(c->rtcServerId.toULongLong()),
                                voice->currentChannelId(), accountId, voice->sessionId(), voice->proxyConfig());
    c->client->setVideoStreamType(QStringLiteral("screen"));
    c->client->moveToThread(c->thread);
    const QString key = c->key;

    if (c->ownStream) {
        H264Encoder::Settings settings;
        settings.width = c->quality.width;
        settings.height = c->quality.height;
        settings.fps = c->quality.fps;
        settings.bitrateKbps = bitrateKbps(c->quality);
        c->senderThread = new QThread(this);
        c->senderThread->setObjectName(QStringLiteral("GoLiveCapture"));
        c->sender = new VideoSender(c->source, settings);
        c->sender->moveToThread(c->senderThread);
        connect(c->sender, &VideoSender::frameEncoded, c->client, &VoiceClient::sendVideoFrame);
        connect(c->client, &VoiceClient::keyframeRequested, c->sender, &VideoSender::requestKeyframe);
        connect(c->sender, &VideoSender::started, this, [this](const QString &name) { ownEncoder = name; });
        connect(c->sender, &VideoSender::failed, this, [this, key](const QString &reason) {
            if (own && own->key == key) {
                emit streamError(reason);
                stopStreaming();
            }
        });
        const Quality quality = c->quality;
        VoiceClient *vc = c->client;
        VideoSender *sender = c->sender;
        connect(c->client, &VoiceClient::connected, c->client, [vc, quality, sender]() {
            vc->setVideoActive(true, quality.width, quality.height, quality.fps, bitrateKbps(quality) * 1000);
            QMetaObject::invokeMethod(sender, &VideoSender::begin);
        });
        connect(c->client, &VoiceClient::connected, this, [this, key]() {
            if (own && own->key == key) {
                qCInfo(LogVoice) << "Go Live: streaming" << key;
                emit streamingChanged(true);
            }
        });
        c->senderThread->start();
    } else {
        c->receiver = new VideoReceiver;
        c->receiver->moveToThread(c->thread);
        connect(c->client, &VoiceClient::videoFrameReceived, c->receiver, &VideoReceiver::onFrame);
        connect(c->receiver, &VideoReceiver::frameDecoded, this,
                [this, key](const QImage &frame) { emit watchFrame(key, frame); });
        VoiceClient *vc = c->client;
        connect(c->client, &VoiceClient::connected, c->client, [vc]() { vc->requestVideo(100); });
    }
    connect(c->client, &VoiceClient::disconnected, this, [this, key]() {
        if (own && own->key == key) {
            emit streamError(tr("The stream connection was lost"));
            stopStreaming();
        } else if (watched.contains(key)) {
            stopWatching(key);
        }
    });
    c->thread->start();
    QMetaObject::invokeMethod(c->client, &VoiceClient::start);
}

void GoLive::closeConnection(Connection *c)
{
    QThread *main = thread();
    if (c->sender && c->senderThread) {
        VideoSender *sender = c->sender;
        QMetaObject::invokeMethod(
                sender, [sender, main]() { sender->end(); sender->moveToThread(main); }, Qt::BlockingQueuedConnection);
        c->senderThread->quit();
        c->senderThread->wait(3000);
        delete c->sender;
        delete c->senderThread;
    }
    if (c->client && c->thread) {
        VoiceClient *vc = c->client;
        VideoReceiver *receiver = c->receiver;
        if (c->ownStream)
            QMetaObject::invokeMethod(vc, [vc]() { vc->setVideoActive(false, 0, 0, 0, 0); }, Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(
                vc,
                [vc, receiver, main]() {
                    vc->stop();
                    vc->moveToThread(main);
                    if (receiver)
                        receiver->moveToThread(main);
                },
                Qt::BlockingQueuedConnection);
        c->thread->quit();
        c->thread->wait(5000);
        delete c->client;
        delete c->receiver;
        delete c->thread;
    } else {
        delete c->receiver;
    }
    delete c;
}

} // namespace Video
} // namespace Qord
