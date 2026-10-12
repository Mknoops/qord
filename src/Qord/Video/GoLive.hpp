#pragma once

// Go Live: streaming a screen or window to the voice channel you are in,
// and watching other people's streams.
//
// Starting a stream sends STREAM_CREATE (and STREAM_SET_PAUSED false) on the
// main gateway; Discord answers with STREAM_CREATE (the RTC server) and
// STREAM_SERVER_UPDATE (endpoint and token) for the stream key. Qord then
// opens a second voice connection for the stream, announces its video (op
// 12) and sends H.264 from a capture/encode thread. Watching sends
// STREAM_WATCH and opens a receive-only connection whose frames are decoded
// on that connection's thread.

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include <memory>

#include "Core/ProxyConfig.hpp"
#include "Core/Snowflake.hpp"
#include "Qord/Audio/StreamAudio.hpp"
#include "Qord/Video/ScreenCapture.hpp"
#include "Qord/Video/VideoCodec.hpp"
#include "Qord/Video/VideoSender.hpp"

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

// Decodes a watched stream on its connection's thread
class VideoReceiver : public QObject
{
    Q_OBJECT
public slots:
    void onFrame(quint64 userId, quint32 ssrc, const QByteArray &annexB, bool keyframe);

signals:
    void frameDecoded(const QImage &frame);

private:
    H264Decoder decoder;
};

class GoLive : public QObject
{
    Q_OBJECT
public:
    struct Quality
    {
        int width = 1280;
        int height = 720;
        int fps = 30;
    };

    GoLive(Acheron::Discord::Client *client, Acheron::Core::Audio::VoiceManager *voice,
           Acheron::Core::Snowflake accountId, QObject *parent = nullptr);
    ~GoLive() override;

    // Discord's limits: 720p 30 fps without Nitro; with Nitro (or a server
    // at boost level 2) up to 1080p 60 fps, and source resolution with Nitro
    static QList<Quality> allowedQualities(bool nitro, int guildBoostTier);
    static int bitrateKbps(const Quality &quality);
    static QString streamKey(Acheron::Core::Snowflake guildId, Acheron::Core::Snowflake channelId,
                             Acheron::Core::Snowflake userId);

    // Needs a voice connection. False (with streamError) when one is missing.
    // With `audio`, also what the computer plays (see StreamAudio); a
    // failure there leaves the picture streaming and says so in streamError
    bool startStreaming(const CaptureSource &source, const Quality &quality, bool audio = false);
    void stopStreaming();
    [[nodiscard]] bool isStreaming() const { return own != nullptr; }
    [[nodiscard]] QString encoderName() const { return ownEncoder; }

    void watch(Acheron::Core::Snowflake userId);
    void stopWatching(const QString &key);
    [[nodiscard]] bool isWatching(const QString &key) const { return watched.contains(key); }

signals:
    void streamingChanged(bool live);
    void streamError(const QString &message);
    void viewersChanged(const QList<Acheron::Core::Snowflake> &viewers);
    void watchFrame(const QString &key, const QImage &frame);
    void watchEnded(const QString &key, const QString &reason);

private:
    struct Connection
    {
        QString key;
        bool ownStream = false;
        QString rtcServerId;
        QString endpoint;
        QString token;
        QThread *thread = nullptr;
        Acheron::Discord::Voice::VoiceClient *client = nullptr;
        QThread *senderThread = nullptr;
        VideoSender *sender = nullptr;
        VideoReceiver *receiver = nullptr;
        CaptureSource source;
        Quality quality;
        bool audio = false;
        Qord::Audio::StreamAudio *streamAudio = nullptr;
    };

    void onStreamEvent(const QString &type, const QJsonObject &data);
    void connectStream(Connection *connection);
    void startStreamAudio(Connection *connection);
    void closeConnection(Connection *connection);
    Connection *connectionFor(const QString &key);

    Acheron::Discord::Client *client;
    QPointer<Acheron::Core::Audio::VoiceManager> voice;
    Acheron::Core::Snowflake accountId;
    Connection *own = nullptr;
    QString ownEncoder;
    QHash<QString, Connection *> watched;
};

} // namespace Video
} // namespace Qord
