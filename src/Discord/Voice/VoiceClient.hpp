#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Core/ProxyConfig.hpp"
#include "Qord/Video/H264Rtp.hpp"
#include "Qord/Video/Pacer.hpp"
#include "Core/Snowflake.hpp"
#include "VoiceEnums.hpp"
#include "VoiceEntities.hpp"

namespace Acheron {
namespace Discord {
namespace Voice {

class VoiceGateway;
class UdpTransport;
class VoiceEncryption;
class DaveSession;

class VoiceClient : public QObject
{
    Q_OBJECT
public:
    enum class State {
        Disconnected,
        Connecting,
        Identifying,
        WaitingForReady,
        DiscoveringIP,
        SelectingProtocol,
        WaitingForSession,
        Connected,
    };
    Q_ENUM(State)

    VoiceClient(const QString &endpoint, const QString &token, Core::Snowflake serverId,
                Core::Snowflake channelId, Core::Snowflake userId, const QString &sessionId,
                const Core::ProxyConfig &proxy, QObject *parent = nullptr);
    ~VoiceClient() override;

    void start();
    void stop();

    [[nodiscard]] State state() const { return currentState; }
    [[nodiscard]] quint32 ssrc() const { return localSsrc; }
    [[nodiscard]] const QString &encryptionMode() const { return selectedMode; }
    [[nodiscard]] const QByteArray &secretKey() const { return sessionKey; }

    void seedConnectedUsers(const QList<Core::Snowflake> &userIds);

    void sendAudio(const QByteArray &opusData);

    void setSpeaking(bool speaking);

    bool isDaveEnabled() const;

    // Qord: video. Set the stream type ("screen" for Go Live, "video" for
    // cameras) before start() to get video SSRCs.
    void setVideoStreamType(const QString &streamType) { videoStreamType = streamType; }
    [[nodiscard]] quint32 videoSsrc() const { return localVideoSsrc; }
    // Announces our video (op 12) or stops it
    void setVideoActive(bool active, int width, int height, int fps, int maxBitrate);
    // One H.264 access unit (Annex B) with its 90 kHz timestamp
    void sendVideoFrame(const QByteArray &annexB, quint32 timestamp);
    // Asks the server to forward video to us
    void requestVideo(int quality);
    void requestVerificationCode(Core::Snowflake targetUserId, std::function<void(const QString &)> callback);

signals:
    void stateChanged(State newState);
    void connected();
    void disconnected();

    void speakingReceived(const SpeakingData &data);
    void clientConnected(const ClientConnectData &data);
    void clientDisconnected(Core::Snowflake userId);

    void audioReceived(quint32 ssrc, uint16_t sequence, uint32_t timestamp, const QByteArray &opusData);

    void privacyCodeChanged(const QString &code);

    // Qord: video
    void videoFrameReceived(quint64 userId, quint32 ssrc, const QByteArray &annexB, bool keyframe);
    void videoStreamsChanged(quint64 userId, quint32 videoSsrc, bool active);
    // Someone asked us for a keyframe (RTCP PLI/FIR)
    void keyframeRequested();

private slots:
    void onGatewayConnected();
    void onGatewayDisconnected(VoiceCloseCode code, const QString &reason);
    void onGatewayReady(const VoiceReady &data);
    void onSessionDescription(const SessionDescription &desc);
    void onSpeaking(const SpeakingData &data);
    void onClientsConnect(const QStringList &userIds);
    void onClientConnect(const ClientConnectData &data);
    void onClientDisconnect(Core::Snowflake userId);
    void onGatewayResumed();
    void onIpDiscovered(const QString &ip, int port);
    void onIpDiscoveryFailed(const QString &error);
    void onDatagram(const QByteArray &data);

private:
    void setState(State state);
    void sendSilence();
    void cleanupTransport();
    void ensureDaveSession(uint16_t protocolVersion);
    // Qord
    void onVideoDatagram(const QByteArray &data);
    void onRtcpDatagram(const QByteArray &data);
    void onVideoOp(const QJsonObject &data);
    void sendPictureLossIndication(quint32 mediaSsrc);

private:
    VoiceGateway *gateway = nullptr;
    UdpTransport *udpTransport = nullptr;

    QString endpoint;
    QString token;
    Core::ProxyConfig proxy;
    Core::Snowflake serverId;
    Core::Snowflake channelId;
    Core::Snowflake userId;
    QString sessionId;

    std::atomic<State> currentState{ State::Disconnected };

    // ready
    quint32 localSsrc = 0;
    QString serverIp;
    int serverPort = 0;
    QStringList serverModes;

    // protocol selection
    QString selectedMode;
    QByteArray sessionKey;

    std::unique_ptr<VoiceEncryption> encryption;
    QTimer *keepaliveTimer = nullptr;
    uint16_t rtpSequence = 0;
    uint32_t rtpTimestamp = 0;

    std::chrono::steady_clock::time_point rtpEpoch;
    std::chrono::steady_clock::time_point lastAudioSendTime;
    bool newTalkspurt = false;

    std::unique_ptr<DaveSession> daveSession;
    std::set<std::string> connectedUserIds;
    QHash<quint32, uint64_t> ssrcToUserIdMap;

    static constexpr int KEEPALIVE_INTERVAL_MS = 10000;

    // Qord: video
    QString videoStreamType;
    quint32 localVideoSsrc = 0;
    quint32 localRtxSsrc = 0;
    uint16_t videoSequence = 0;
    bool videoCodecAssigned = false;
    QHash<quint32, Qord::Video::Depacketizer> depacketizers;
    QHash<quint32, qint64> lastPliMs;
    // Video packets leave through the pacer, not in one burst per frame
    Qord::Video::Pacer pacer;
    QTimer *pacerTimer = nullptr;
    QElapsedTimer pacerClock;
    void pumpPacer();
};

} // namespace Voice
} // namespace Discord
} // namespace Acheron
