#pragma once

// Camera video in voice calls. Unlike Go Live, cameras ride on the call's own
// voice connection: it asks for a video stream when it identifies (see
// VoiceManager::setVideoSupported), and turning the camera on announces that
// stream (op 12), sets self_video in the voice state and sends H.264 from a
// capture/encode thread. Everyone else's camera arrives on the same
// connection and is decoded on a thread of its own, one decoder per SSRC.

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointer>

#include <memory>
#include <optional>
#include <unordered_map>

#include "Core/Snowflake.hpp"
#include "Qord/Video/CameraCapture.hpp"
#include "Qord/Video/VideoCodec.hpp"

class QThread;

namespace Acheron {
namespace Discord {
class Client;
namespace Voice {
class VoiceClient;
}
} // namespace Discord
namespace Core {
namespace Audio {
class VoiceManager;
}
} // namespace Core
} // namespace Acheron

namespace Qord {
namespace Video {

class VideoSender;

// Decodes the cameras of a call, on its own thread
class CameraReceiver : public QObject
{
    Q_OBJECT
public slots:
    void onFrame(quint64 userId, quint32 ssrc, const QByteArray &annexB, bool keyframe);
    void drop(quint32 ssrc);

signals:
    void frameDecoded(quint64 userId, const QImage &frame);

private:
    std::unordered_map<quint32, std::unique_ptr<H264Decoder>> decoders;
};

class Camera : public QObject
{
    Q_OBJECT
public:
    // Discord sends cameras at 720p30 at most, about 2.5 Mbit/s
    static constexpr int Width = 1280;
    static constexpr int Height = 720;
    static constexpr int Fps = 30;
    static constexpr int BitrateKbps = 2500;

    Camera(Acheron::Discord::Client *client, Acheron::Core::Audio::VoiceManager *voice,
           Acheron::Core::Snowflake accountId, QObject *parent = nullptr);
    ~Camera() override;

    // Needs a voice connection. False (with error) when there is none.
    bool start(const CameraDevice &device);
    void stop();
    [[nodiscard]] bool isOn() const { return wanted.has_value(); }
    [[nodiscard]] QString deviceName() const { return wanted ? wanted->name : QString(); }
    [[nodiscard]] QString encoderName() const { return encoder; }
    // Users whose camera this call is showing
    [[nodiscard]] QList<Acheron::Core::Snowflake> remoteVideos() const { return remote.keys(); }

signals:
    void cameraChanged(bool on);
    void error(const QString &message);
    // Your own camera, small, for the self view
    void previewFrame(const QImage &frame);
    void remoteFrame(Acheron::Core::Snowflake userId, const QImage &frame);
    void remoteVideoEnded(Acheron::Core::Snowflake userId);

private:
    void attach(Acheron::Discord::Voice::VoiceClient *client);
    void detach();
    void onConnected();
    void startSending();
    void stopSending();
    void endRemote(Acheron::Core::Snowflake userId);

    Acheron::Discord::Client *client;
    QPointer<Acheron::Core::Audio::VoiceManager> voice;
    Acheron::Core::Snowflake accountId;
    Acheron::Discord::Voice::VoiceClient *call = nullptr;
    bool callConnected = false;
    QList<QMetaObject::Connection> callConnections;
    std::optional<CameraDevice> wanted;
    QString encoder;

    QThread *senderThread = nullptr;
    VideoSender *sender = nullptr;
    QThread *decodeThread = nullptr;
    CameraReceiver *receiver = nullptr;
    QHash<Acheron::Core::Snowflake, quint32> remote; // user -> video SSRC
};

} // namespace Video
} // namespace Qord
