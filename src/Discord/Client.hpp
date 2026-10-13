#pragma once

#include <QObject>
#include <QNetworkReply>
#include <QHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QUrlQuery>

#include <optional>

#include "Core/Result.hpp"
#include "Core/Snowflake.hpp"
#include "Core/Enums.hpp"
#include "Core/ProxyConfig.hpp"
#include "Core/PendingAttachment.hpp"

#include "Proto/FrecencySettings.hpp"
#include "Proto/UserSettings.hpp"

#include "Gateway.hpp"
#include "GuildRequests.hpp"
#include "HttpClient.hpp"
#include "ClientIdentity.hpp"

using Acheron::Core::Snowflake;

namespace Acheron {
namespace Discord {

class Client : public QObject
{
    Q_OBJECT
public:
    enum class MessageLoadType {
        Latest,
        History,
        Future,
        Jump,
        Created,
    };
    Q_ENUM(MessageLoadType)

    explicit Client(const QString &token, const QString &gatewayUrl, const QString &baseUrl,
                    const Core::ProxyConfig &proxy, CaptchaResolver *captchaResolver = nullptr,
                    QObject *parent = nullptr);
    ~Client() override;

    void start();
    void stop();

    [[nodiscard]] Core::ConnectionState getState() const;
    [[nodiscard]] const Core::ProxyConfig &getProxy() const { return proxyConfig; }

    using MessagesCallback = std::function<void(const Core::Result<QList<Message>> &)>;
    void fetchLatestMessages(Snowflake channelId, int limit, MessagesCallback callback);
    void fetchHistory(Snowflake channelId, Snowflake beforeId, int limit, MessagesCallback callback);
    void fetchMessagesAfter(Snowflake channelId, Snowflake afterId, int limit, MessagesCallback callback);
    void fetchMessagesAround(Snowflake channelId, Snowflake messageId, int limit, MessagesCallback callback);

    using ProfileCallback = std::function<void(const Core::Result<UserProfile> &)>;
    void fetchUserProfile(Snowflake userId, Snowflake guildId, ProfileCallback callback);

    // empty until its actually fetched
    [[nodiscard]] QString applicationIconHash(Snowflake applicationId);

    void setUserNote(Snowflake userId, const QString &note);

    using DmChannelCallback = std::function<void(const Core::Result<ChannelCreate> &)>;
    void openDmChannel(Snowflake userId, DmChannelCallback callback);

    struct ForumThreadSearchResult
    {
        QList<Channel> threads;
        QHash<Snowflake, Message> firstMessages; // (thread id, starter message)
        bool hasMore = false;
        bool indexNotReady = false; // 202
        int retryAfterSeconds = 0;
    };
    using ForumThreadsCallback = std::function<void(const Core::Result<ForumThreadSearchResult> &)>;
    // "last_message_time" / "creation_time"
    void searchForumThreads(Snowflake forumId, int offset, const QString &sortBy, ForumThreadsCallback callback);

    struct CreatedForumThread
    {
        Channel thread;
        std::optional<Message> starterMessage;
    };
    using ForumThreadCallback = std::function<void(const Core::Result<CreatedForumThread> &)>;
    void createForumThread(Snowflake forumId, const QString &name,
                           const QList<Snowflake> &appliedTags, const QString &content,
                           const QString &nonce,
                           const QList<Core::PendingAttachment> &attachments,
                           ForumThreadCallback callback);

    using ForumPostDataCallback = std::function<void(const Core::Result<QHash<Snowflake, Message>> &)>;
    void fetchForumPostData(Snowflake forumId, const QList<Snowflake> &threadIds, ForumPostDataCallback callback);

    // Qord: a thread started from a message (Create Thread); the result is
    // the new thread channel
    using ThreadCallback = std::function<void(const Core::Result<Channel> &)>;
    void createThreadFromMessage(Snowflake channelId, Snowflake messageId, const QString &name,
                                 ThreadCallback callback);
    void joinThread(Snowflake threadId);
    void leaveThread(Snowflake threadId);

    struct ThreadListResult
    {
        QList<Channel> threads;
        QList<ThreadMember> members;
        bool hasMore = false;
        bool indexNotReady = false; // 202
        int retryAfterSeconds = 0;
    };
    using ThreadListCallback = std::function<void(const Core::Result<ThreadListResult> &)>;
    void searchThreads(Snowflake channelId, bool archived, int offset, ThreadListCallback callback);

    using RefreshedUrlsCallback = std::function<void(const Core::Result<QHash<QUrl, QUrl>> &)>;
    void refreshAttachmentUrls(const QList<QUrl> &urls, RefreshedUrlsCallback callback);

    void sendMessage(Snowflake channelId, const QString &content, const QString &nonce,
                     Snowflake replyToMessageId = Snowflake::Invalid,
                     const QList<Core::PendingAttachment> &attachments = {},
                     bool mentionRepliedUser = true, int flags = 0);
    bool cancelMessageSend(const QString &nonce);
    void editMessage(Snowflake channelId, Snowflake messageId, const QString &content);
    void deleteMessage(Snowflake channelId, Snowflake messageId);
    void pinMessage(Snowflake channelId, Snowflake messageId);
    void unpinMessage(Snowflake channelId, Snowflake messageId);

    void addReaction(Snowflake channelId, Snowflake messageId, const QString &emoji,
                     bool isBurst = false);
    void removeReaction(Snowflake channelId, Snowflake messageId, const QString &emoji,
                        bool isBurst = false);

    struct AckEntry
    {
        Snowflake channelId;
        Snowflake messageId;
        int readStateType = 0;
    };

    void ackMessage(Snowflake channelId, Snowflake messageId, int flags, int lastViewed);
    void ackBulk(const QList<AckEntry> &entries, std::function<void(bool success)> onFinished);

    void sendVoiceStateUpdate(Snowflake guildId, Snowflake channelId, bool selfMute, bool selfDeaf);
    // Qord: Go Live
    void sendGatewayOp(int op, const Core::OrderedJson &d);
    // Qord: camera on/off in the voice state (self_video)
    void setSelfVideo(bool on);
    void setVoiceConnected(bool connected);

    void restoreHeartbeatSession(const std::optional<HeartbeatSession> &stored);

    void leaveGuild(Snowflake guildId);

    void debugForceReconnect();

    void subscribeToGuildChannel(Snowflake guildId, Snowflake channelId,
                                 const QList<QPair<int, int>> &ranges);
    void ensureSubscriptionByChannel(Snowflake channelId);
    void requestForumUnreads(Snowflake forumId, const QList<QPair<Snowflake, Snowflake>> &threads);
    void requestGuildMembers(Snowflake guildId, const QList<Snowflake> &userIds, bool presences = true);
    void queryGuildMembers(Snowflake guildId, const QString &query, int limit);

    [[nodiscard]] Snowflake getGuildIdForChannel(Snowflake channelId) const;
    void registerChannelGuild(Snowflake channelId, Snowflake guildId);

    [[nodiscard]] PremiumTier getGuildPremiumTier(Snowflake guildId) const;
    [[nodiscard]] qint64 getMaxUploadSize(Snowflake channelId) const;

    using FrecencyCallback = std::function<void(const Core::Result<Proto::FrecencyUserSettings> &)>;
    void fetchFrecencySettings(FrecencyCallback callback);

    struct FrecencyPatchResult
    {
        bool success = false;
        bool rateLimited = false;
        bool outOfDate = false;
        QString error;
        std::optional<Proto::FrecencyUserSettings> settings; // server state after the patch
    };
    using FrecencyPatchCallback = std::function<void(const FrecencyPatchResult &)>;
    void patchFrecencySettings(const QByteArray &partialProto,
                               std::optional<uint32_t> requiredDataVersion,
                               FrecencyPatchCallback callback);

    [[nodiscard]] const Proto::PreloadedUserSettings &getSettings() const;
    [[nodiscard]] const User &getMe() const;
    [[nodiscard]] bool isPremium() const;

    template <typename T>
    using ResultCallback = std::function<void(const Core::Result<T> &)>;
    using ActionCallback = ResultCallback<void>;

    void fetchGuildProfile(Snowflake guildId, ResultCallback<GuildProfileEdit> callback);
    void modifyGuildProfile(Snowflake guildId, const GuildProfileEdit &edit, ResultCallback<GuildProfileEdit> callback);
    void modifyGuild(Snowflake guildId, const GuildEdit &edit, const QMap<QString, QByteArray> &originalMd5s, ActionCallback callback);
    void deleteGuild(Snowflake guildId, ActionCallback callback);

    void createRole(Snowflake guildId, ResultCallback<Role> callback);
    void modifyRole(Snowflake guildId, Snowflake roleId, const RoleEdit &edit, ActionCallback callback);
    void modifyRolePositions(Snowflake guildId, const QList<QPair<Snowflake, int>> &positions, ActionCallback callback);
    void deleteRole(Snowflake guildId, Snowflake roleId, ActionCallback callback);
    void fetchRoleMemberCounts(Snowflake guildId, ResultCallback<QHash<Snowflake, int>> callback);
    void fetchRoleMemberIds(Snowflake guildId, Snowflake roleId, ResultCallback<QList<Snowflake>> callback);
    void addRoleMembers(Snowflake guildId, Snowflake roleId, const QList<Snowflake> &userIds, ResultCallback<QList<Snowflake>> callback);

    struct MemberSearchPage
    {
        QList<MemberSearchResult> members;
        int totalResultCount = 0;
        std::optional<int> indexingRetryAfterSeconds;
    };
    void searchGuildMembers(Snowflake guildId, const MemberSearchQuery &query, ResultCallback<MemberSearchPage> callback);
    void setMemberRoles(Snowflake guildId, Snowflake userId, const QList<Snowflake> &roleIds,
                        const QList<Snowflake> &added, const QList<Snowflake> &removed, ActionCallback callback);
    void setMemberNickname(Snowflake guildId, Snowflake userId, const QString &nick, ActionCallback callback);
    void setMemberTimeout(Snowflake guildId, Snowflake userId, const QDateTime &until, const std::optional<QString> &reason, ActionCallback callback);
    void kickMember(Snowflake guildId, Snowflake userId, const QString &reason, ActionCallback callback);
    void banMember(Snowflake guildId, Snowflake userId, int deleteMessageSeconds, const QString &reason, ActionCallback callback);
    void unbanMember(Snowflake guildId, Snowflake userId, ActionCallback callback);
    void requestPruneCount(Snowflake guildId, int days, const QList<Snowflake> &includeRoles, ActionCallback callback);
    void pruneMembers(Snowflake guildId, int days, const QList<Snowflake> &includeRoles, ActionCallback callback);
    void setMemberUpdatesSubscription(Snowflake guildId, bool subscribed);

    void fetchBans(Snowflake guildId, Snowflake after, ResultCallback<QList<Ban>> callback);
    void searchBans(Snowflake guildId, const QString &query, const QList<Snowflake> &userIds, ResultCallback<QList<Ban>> callback);

    void fetchGuildInvites(Snowflake guildId, ResultCallback<QList<Invite>> callback);
    void createInvite(Snowflake channelId, int maxAgeSeconds, int maxUses, bool temporary, ResultCallback<Invite> callback);
    void revokeInvite(const QString &code, ActionCallback callback);
    void setIncidentActions(Snowflake guildId, const QDateTime &invitesDisabledUntil, const QDateTime &dmsDisabledUntil,
                            std::optional<int> lockdownHours, ActionCallback callback);

    void fetchGuildEmojis(Snowflake guildId, ResultCallback<QList<Emoji>> callback);
    void createGuildEmoji(Snowflake guildId, const QString &name, const QString &imageDataUri, const QByteArray &originalMd5, ResultCallback<Emoji> callback);
    void renameGuildEmoji(Snowflake guildId, Snowflake emojiId, const QString &name, ActionCallback callback);
    void deleteGuildEmoji(Snowflake guildId, Snowflake emojiId, ActionCallback callback);

    void fetchGuildStickers(Snowflake guildId, ResultCallback<QList<Sticker>> callback);
    // Qord: Discord's own sticker packs, which anyone can send; fetched once
    struct StickerPack
    {
        Snowflake id;
        QString name;
        QList<Sticker> stickers;
    };
    void fetchStickerPacks(ResultCallback<QList<StickerPack>> callback);
    // Qord: GET /users/{id} (ValidUser); `user` is set on success
    void fetchUser(Snowflake userId, std::function<void(const HttpResponse &response, const std::optional<User> &user)> callback);
    // Qord: GET /users/@me/affinities/v2/users, the people Discord ranks you
    // as talking to most (ImplicitRelationships): user id and communication
    // rank, lowest first
    void fetchUserAffinities(
            std::function<void(const HttpResponse &response, const QList<QPair<Snowflake, double>> &affinities)> callback);
    // Qord: the first 100 users of a reaction (emoji: "name" or "name:id";
    // type 0 normal, 1 burst)
    void fetchReactionUsers(Snowflake channelId, Snowflake messageId, const QString &emoji, int type,
                            ResultCallback<QList<User>> callback);
    void createGuildSticker(Snowflake guildId, const QString &name, const QString &tags, const QString &description,
                            const FileUpload &file, const QByteArray &originalMd5, ResultCallback<Sticker> callback);
    void modifyGuildSticker(Snowflake guildId, Snowflake stickerId, const QString &name, const QString &tags,
                            const QString &description, ActionCallback callback);
    void deleteGuildSticker(Snowflake guildId, Snowflake stickerId, ActionCallback callback);

    struct AuditLogQuery
    {
        static constexpr int PageSize = 50;
        Snowflake before;
        Snowflake userId;
        std::optional<AuditLogAction> actionType;
    };
    void fetchAuditLog(Snowflake guildId, const AuditLogQuery &query, ResultCallback<AuditLog> callback);

    // --- Qord additions: core Discord features Acheron lacked (src/Qord/ClientExtras.cpp) ---
    using JsonCallback = std::function<void(const Core::Result<QJsonDocument> &)>;
    // POST /channels/{id}/typing (callers throttle; Discord expects one per ~8s)
    void sendTyping(Snowflake channelId);
    // status: online/idle/dnd/invisible. Sent over the gateway and saved to settings.
    void setStatus(const QString &status, const QString &customText, const QString &customEmoji);
    // Qord: Discord's "Share your detected activities with others"
    // (status.show_current_game, synced; true unless turned off)
    [[nodiscard]] bool showCurrentGame() const;
    void setShowCurrentGame(bool show);
    // Qord: your connected accounts (GET /users/@me/connections) and whether
    // one shares its activity (PATCH .../connections/{type}/{id})
    struct OwnConnection
    {
        QString type;
        QString id;
        QString name;
        bool showActivity = false;
        bool revoked = false;
    };
    void fetchOwnConnections(std::function<void(bool ok, const QList<OwnConnection> &connections)> callback);
    void setConnectionShowActivity(const QString &type, const QString &id, bool show,
                                   std::function<void(bool ok)> callback);
    // Qord: the devices logged in to the account (GET /auth/sessions), the
    // one Qord is (from READY), and logging some out
    struct AuthSession
    {
        QString idHash;
        QDateTime lastUsed;
        QString os;
        QString platform;
        QString location;
    };
    void fetchAuthSessions(std::function<void(bool ok, const QList<AuthSession> &sessions)> callback);
    void logoutAuthSessions(const QStringList &idHashes, std::function<void(bool ok, const QString &error)> callback);
    [[nodiscard]] QString currentAuthSessionHash() const { return authSessionIdHash; }
    void fetchPinnedMessages(Snowflake channelId, MessagesCallback callback);
    // Guild-wide when guildId is valid, else within channelId (DMs)
    void searchMessages(Snowflake guildId, Snowflake channelId, const QUrlQuery &query, JsonCallback callback);
    void fetchRecentMentions(int limit, bool everyone, MessagesCallback callback);
    void sendFriendRequest(const QString &username, ActionCallback callback);
    // type 1 = friend (accepts an incoming request), 2 = block
    void putRelationship(Snowflake userId, int type, ActionCallback callback);
    void removeRelationship(Snowflake userId, ActionCallback callback);
    void resolveInvite(const QString &code, JsonCallback callback);
    void joinGuildByInvite(const QString &code, JsonCallback callback);
    void createGuild(const QString &name, JsonCallback callback);
    void createGuildChannel(Snowflake guildId, const QString &name, int type, Snowflake parentId, JsonCallback callback);
    // PATCH /users/@me/guilds/{id}/settings (muted, message_notifications, channel_overrides)
    void updateGuildNotificationSettings(Snowflake guildId, const QJsonObject &patch, ActionCallback callback);
    // PATCH a message's flags (SUPPRESS_EMBEDS); the result arrives as MESSAGE_UPDATE
    void setMessageFlags(Snowflake channelId, Snowflake messageId, quint64 flags);
    // Discord's Tenor proxy: trending GIFs for an empty query, else a search
    void fetchGifs(const QString &query, JsonCallback callback);
    // The slash commands usable in a server, or in a DM/group DM channel
    void fetchCommandIndex(Snowflake guildId, Snowflake channelId, JsonCallback callback);
    // POST /interactions (type 2 = application command); replies arrive over the gateway
    void sendInteraction(const QJsonObject &payload, ActionCallback callback);
    [[nodiscard]] QString gatewaySessionId() const;
    // A message with just a sticker; it arrives as MESSAGE_CREATE
    void sendSticker(Snowflake channelId, Snowflake stickerId, Snowflake replyToMessageId, ActionCallback callback,
                     const QString &content = {});

signals:
    void stateChanged(Core::ConnectionState state);
    void ready(const Ready &data);
    void readySupplemental(const ReadySupplemental &data);
    void messageCreated(const Message &msg);
    void messageUpdated(const Message &msg);
    void messageDeleted(const MessageDelete &event);
    void typingStart(const TypingStart &event);
    void channelCreated(const ChannelCreate &event);
    void channelUpdated(const ChannelUpdate &event);
    void channelDeleted(const ChannelDelete &event);
    void threadCreated(const ChannelCreate &event);
    void threadUpdated(const ChannelUpdate &event);
    void threadDeleted(const ThreadDelete &event);
    void threadListSync(const ThreadListSync &event);
    void threadMemberUpdated(const ThreadMemberUpdate &event);
    void threadMembersUpdated(const ThreadMembersUpdate &event);
    void forumUnreads(const ForumUnreads &event);
    void guildCreated(const GatewayGuild &guild);
    void guildUpdated(const Guild &guild);
    void guildDeleted(const GuildDelete &event);
    void guildMembersChunk(const GuildMembersChunk &chunk);
    void guildMemberAdded(const GuildMemberAdd &event);
    void guildMemberUpdated(const GuildMemberUpdate &event);
    void guildMemberRemoved(const GuildMemberRemove &event);
    void guildRoleCreated(const GuildRoleCreate &event);
    void guildRoleUpdated(const GuildRoleUpdate &event);
    void guildRoleDeleted(const GuildRoleDelete &event);
    void roleMemberCountChanged(Snowflake guildId, Snowflake roleId, int delta);
    void guildEmojisUpdated(const GuildEmojisUpdate &event);
    void guildStickersUpdated(const GuildStickersUpdate &event);
    void guildBanAdded(const GuildBanEvent &event);
    void guildBanRemoved(const GuildBanEvent &event);
    void inviteCreated(const Invite &invite);
    void inviteRevoked(const QString &code);
    void guildPruneUpdated(const GuildPruneUpdate &event);
    void messageAcked(const MessageAck &event);
    void messageReactionAdd(const MessageReactionAdd &event);
    void messageReactionAddMany(const MessageReactionAddMany &event);
    void messageReactionRemove(const MessageReactionRemove &event);
    void messageReactionRemoveAll(const MessageReactionRemoveAll &event);
    void messageReactionRemoveEmoji(const MessageReactionRemoveEmoji &event);
    void userGuildSettingsUpdated(const UserGuildSettings &settings);
    void notificationSettingsUpdated(const NotificationSettings &settings);
    void guildMemberListUpdate(const GuildMemberListUpdate &event);
    void voiceStateUpdated(const VoiceState &event);
    void voiceServerUpdated(const VoiceServerUpdate &event);
    void streamEvent(const QString &type, const QJsonObject &data); // Qord: Go Live
    void autocompleteResponse(const QJsonObject &data); // Qord: slash command option suggestions
    void relationshipAdded(const Relationship &event);
    void relationshipUpdated(const RelationshipPartial &event);
    void relationshipRemoved(const RelationshipPartial &event);
    void userNoteUpdated(const UserNoteUpdate &event);
    void userSettingsProtoUpdated(const UserSettingsProtoUpdate &event);
    void settingsChanged();
    void applicationIconResolved(Snowflake applicationId);
    void presenceUpdated(const Presence &event);
    void presencesReplaced(const QList<Presence> &presences);
    void sessionsReplaced(const QList<UserSession> &sessions);
    void messageSendFailed(const QString &nonce, const QString &error);
    void guildLeaveFailed(Core::Snowflake guildId, const QString &error);
    void attachmentUploadProgress(const QString &nonce, int fileIndex, qint64 sent, qint64 total);

    void reconnecting(int attempt, int maxAttempts);
    void errorOccurred(const QString &errorStr);
    void authenticationFailed();
    void heartbeatSessionChanged(const HeartbeatSession &session);

private slots:
    void onConnected();
    void onHeartbeatSessionTimer();
    void onDisconnected(CloseCode code, const QString &reason);

    void onGatewayReady(const Ready &data);
    void onGatewayReadySupplemental(const ReadySupplemental &data);
    void onGatewayMessageCreate(const Message &msg);
    void onGatewayMessageUpdate(const Message &msg);
    void onGatewayMessageDelete(const MessageDelete &event);
    void onGatewayChannelCreate(const ChannelCreate &event);
    void onGatewayChannelUpdate(const ChannelUpdate &event);
    void onGatewayChannelDelete(const ChannelDelete &event);
    void onGatewayThreadCreate(const ChannelCreate &event);
    void onGatewayThreadUpdate(const ChannelUpdate &event);
    void onGatewayThreadDelete(const ThreadDelete &event);
    void onGatewayThreadListSync(const ThreadListSync &event);
    void onGatewayGuildCreate(const GatewayGuild &guild);
    void onGatewayGuildUpdate(const Guild &guild);
    void onGatewayGuildDelete(const GuildDelete &event);
    void onGatewayGuildRoleCreate(const GuildRoleCreate &event);
    void onGatewayGuildRoleUpdate(const GuildRoleUpdate &event);
    void onGatewayGuildRoleDelete(const GuildRoleDelete &event);
    void onGatewayUserSettingsProtoUpdate(const UserSettingsProtoUpdate &event);

private:
    void fetchMessages(Snowflake channelId, QUrlQuery query, int limit, MessagesCallback callback);
    void indexGuildMappings(const GatewayGuild &guild);
    void applyDiscordLocale();
    void updateActiveState();
    void syncHeartbeatSession();
    void removeGuildMappings(Snowflake guildId);

    struct UploadState
    {
        Snowflake channelId;
        QString nonce;
        QList<Core::PendingAttachment> attachments;
        QStringList uploadFilenames;
        QList<bool> uploaded;
        int remaining = 0;
        bool failed = false;
        std::shared_ptr<std::atomic<bool>> cancelFlag;

        std::function<void(const QJsonArray &attachmentsJson)> onUploaded;
        std::function<void(const QString &error)> onFailed;
    };

    void setState(Core::ConnectionState state);
    void uploadAttachmentsAndSend(const std::shared_ptr<UploadState> &state);
    void finishUpload(const std::shared_ptr<UploadState> &state);
    void failUpload(const std::shared_ptr<UploadState> &state, const QString &error);
    void postForumThread(Snowflake forumId, const QString &name,
                         const QList<Snowflake> &appliedTags,
                         const QJsonObject &message,
                         ForumThreadCallback callback);
    void cleanupUploadedSlots(const std::shared_ptr<UploadState> &state);
    void settleUpload(const std::shared_ptr<UploadState> &state);

    HttpCallback applyThen(std::function<void(const QJsonDocument &)> apply, HttpCallback finish);
    HttpCallback memberUpdateHandler(Snowflake guildId, const char *what, ActionCallback callback);
    HttpCallback memberRemovalHandler(Snowflake guildId, Snowflake userId, const char *what, ActionCallback callback);

private:
    Core::ConnectionState state = Core::ConnectionState::Disconnected;

    QString baseUrl;
    Core::ProxyConfig proxyConfig;
    QString token;

    ClientIdentity identity;
    HttpClient *httpClient;
    Gateway *gateway;

    QHash<Snowflake, Snowflake> channelToGuild; // todo prob move this somewhere or just a cache
    QHash<Snowflake, PremiumTier> guildPremiumTiers;
    QSet<Snowflake> subscribedGuilds;
    std::optional<QList<StickerPack>> stickerPacks; // Qord
    QHash<QString, std::shared_ptr<UploadState>> activeUploads; // by nonce

    QHash<Snowflake, QString> applicationIcons;
    QSet<Snowflake> applicationIconRequests;

    Proto::PreloadedUserSettings settings;
    QString authSessionIdHash;
    User me;

    bool appFocused = true;
    bool voiceConnected = false;
    QTimer *heartbeatSessionTimer;
};

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
inline uint qHash(Client::MessageLoadType type, uint seed = 0) noexcept
{
    return ::qHash(static_cast<int>(type), seed);
}
#endif

} // namespace Discord
} // namespace Acheron