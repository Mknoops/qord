#include "Camera.hpp"

#include "Core/Audio/VoiceManager.hpp"
#include "Core/Logging.hpp"
#include "Discord/Client.hpp"
#include "Discord/Voice/VoiceClient.hpp"
#include "Qord/Video/VideoSender.hpp"

#include <QThread>

namespace Qord {
namespace Video {

using Acheron::Core::Snowflake;
using Acheron::Discord::Voice::VoiceClient;

void CameraReceiver::onFrame(quint64 userId, quint32 ssrc, const QByteArray &annexB, bool)
{
    auto &decoder = decoders[ssrc];
    if (!decoder)
        decoder = std::make_unique<H264Decoder>();
    const QImage image = decoder->decode(annexB);
    if (!image.isNull())
        emit frameDecoded(userId, image);
}

void CameraReceiver::drop(quint32 ssrc)
{
    decoders.erase(ssrc);
}

Camera::Camera(Acheron::Discord::Client *client, Acheron::Core::Audio::VoiceManager *voice, Snowflake accountId,
               QObject *parent)
    : QObject(parent), client(client), voice(voice), accountId(accountId)
{
    decodeThread = new QThread(this);
    decodeThread->setObjectName(QStringLiteral("CameraDecode"));
    receiver = new CameraReceiver;
    receiver->moveToThread(decodeThread);
    connect(receiver, &CameraReceiver::frameDecoded, this, [this](quint64 userId, const QImage &frame) {
        // A frame still in flight after the camera went off
        if (remote.contains(Snowflake(userId)))
            emit remoteFrame(Snowflake(userId), frame);
    });
    decodeThread->start();

    if (!voice)
        return;
    connect(voice, &Acheron::Core::Audio::VoiceManager::voiceClientCreated, this, &Camera::attach);
    connect(voice, &Acheron::Core::Audio::VoiceManager::voiceClientStopping, this, &Camera::detach);
    connect(voice, &Acheron::Core::Audio::VoiceManager::voiceDisconnected, this, [this]() {
        // Leaving the call turns the camera off; moving keeps it on
        stop();
        for (const Snowflake &userId : remote.keys())
            endRemote(userId);
    });
    if (VoiceClient *existing = voice->callClient()) {
        attach(existing);
        if (voice->isConnected())
            onConnected();
    }
}

Camera::~Camera()
{
    detach();
    QThread *main = thread();
    CameraReceiver *r = receiver;
    QMetaObject::invokeMethod(r, [r, main]() { r->moveToThread(main); }, Qt::BlockingQueuedConnection);
    decodeThread->quit();
    decodeThread->wait(3000);
    delete receiver;
}

void Camera::attach(VoiceClient *vc)
{
    detach();
    call = vc;
    callConnected = false;
    // vc lives on the voice thread: everything comes over queued
    callConnections << connect(vc, &VoiceClient::connected, this, &Camera::onConnected, Qt::QueuedConnection);
    callConnections << connect(vc, &VoiceClient::videoFrameReceived, receiver, &CameraReceiver::onFrame,
                               Qt::QueuedConnection);
    callConnections << connect(
            vc, &VoiceClient::videoStreamsChanged, this,
            [this](quint64 uid, quint32 ssrc, bool active) {
                const Snowflake userId(uid);
                if (!uid || userId == accountId)
                    return;
                const quint32 previous = remote.value(userId, 0);
                if (active && ssrc) {
                    if (previous && previous != ssrc)
                        QMetaObject::invokeMethod(receiver, [r = receiver, previous]() { r->drop(previous); });
                    remote.insert(userId, ssrc);
                } else if (remote.contains(userId)) {
                    endRemote(userId);
                }
            },
            Qt::QueuedConnection);
    callConnections << connect(
            vc, &VoiceClient::clientDisconnected, this,
            [this](Snowflake userId) {
                if (remote.contains(userId))
                    endRemote(userId);
            },
            Qt::QueuedConnection);
}

void Camera::detach()
{
    if (!call)
        return;
    stopSending();
    for (const auto &c : std::as_const(callConnections))
        disconnect(c);
    callConnections.clear();
    call = nullptr;
    callConnected = false;
    // A new connection (a move) announces the cameras again
    for (const Snowflake &userId : remote.keys())
        endRemote(userId);
}

void Camera::onConnected()
{
    if (!call)
        return;
    callConnected = true;
    // Have the server forward everyone's camera to us
    QMetaObject::invokeMethod(call, [vc = call]() { vc->requestVideo(100); });
    if (wanted)
        startSending();
}

bool Camera::start(const CameraDevice &device)
{
    if (!voice || !voice->isConnected() || !voice->currentChannelId().isValid()) {
        emit error(tr("Join a voice channel to turn on your camera"));
        return false;
    }
    if (!Acheron::Core::Audio::VoiceManager::videoSupported() || !call) {
        emit error(tr("Video isn't available in this build"));
        return false;
    }
    stopSending();
    wanted = device;
    if (callConnected)
        startSending();
    client->setSelfVideo(true);
    qCInfo(LogVoice) << "Camera: on," << device.name;
    emit cameraChanged(true);
    return true;
}

void Camera::stop()
{
    if (!wanted)
        return;
    wanted.reset();
    stopSending();
    if (call)
        QMetaObject::invokeMethod(call, [vc = call]() { vc->setVideoActive(false, 0, 0, 0, 0); });
    client->setSelfVideo(false);
    encoder.clear();
    qCInfo(LogVoice) << "Camera: off";
    emit cameraChanged(false);
}

void Camera::startSending()
{
    if (sender || !wanted || !call)
        return;
    H264Encoder::Settings settings;
    settings.width = Width;
    settings.height = Height;
    settings.fps = Fps;
    settings.bitrateKbps = BitrateKbps;
    senderThread = new QThread(this);
    senderThread->setObjectName(QStringLiteral("CameraCapture"));
    sender = new VideoSender(std::make_unique<CameraCapture>(*wanted, QSize(Width, Height), Fps), settings,
                             QStringLiteral("Camera"));
    sender->setPreviewSize(QSize(480, 270));
    sender->moveToThread(senderThread);
    connect(sender, &VideoSender::frameEncoded, call, &VoiceClient::sendVideoFrame);
    connect(call, &VoiceClient::keyframeRequested, sender, &VideoSender::requestKeyframe);
    connect(sender, &VideoSender::previewFrame, this, &Camera::previewFrame);
    connect(sender, &VideoSender::started, this, [this](const QString &name) { encoder = name; });
    connect(sender, &VideoSender::failed, this, [this](const QString &reason) {
        emit error(reason);
        stop();
    });
    senderThread->start();
    QMetaObject::invokeMethod(call, [vc = call]() {
        vc->setVideoActive(true, Width, Height, Fps, BitrateKbps * 1000);
    });
    QMetaObject::invokeMethod(sender, &VideoSender::begin);
}

void Camera::stopSending()
{
    if (!sender)
        return;
    VideoSender *s = sender;
    QThread *main = thread();
    sender = nullptr;
    QMetaObject::invokeMethod(s, [s, main]() { s->end(); s->moveToThread(main); }, Qt::BlockingQueuedConnection);
    senderThread->quit();
    senderThread->wait(3000);
    delete s;
    delete senderThread;
    senderThread = nullptr;
}

void Camera::endRemote(Snowflake userId)
{
    const quint32 ssrc = remote.take(userId);
    if (ssrc)
        QMetaObject::invokeMethod(receiver, [r = receiver, ssrc]() { r->drop(ssrc); });
    emit remoteVideoEnded(userId);
}

} // namespace Video
} // namespace Qord
