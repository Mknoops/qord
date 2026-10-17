#pragma once

// Client glue: the real services behind PluginContext and the app-wide
// PluginHost. Compiled into the client only (depends on Acheron's Core).

#include "Plugins/PluginHost.hpp"
#include "Plugins/Services.hpp"
#include "Plugins/SettingsWidgets.hpp"

#include <QHash>
#include <QObject>
#include <QPointer>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace Acheron {
namespace Core {
class Session;
}
} // namespace Acheron

namespace Qord {
namespace Plugins {

// HTTPS GETs for plugins with the network permission. The network manager
// is created on first use, so nothing is allocated unless a plugin needs it.
class QtNetworkService : public QObject, public NetworkService
{
    Q_OBJECT
public:
    static constexpr qint64 MaxResponseBytes = 16 * 1024 * 1024;

    explicit QtNetworkService(QObject *parent = nullptr);
    ~QtNetworkService() override;

    Handle get(const QUrl &url, Callback callback) override;
    Handle post(const QUrl &url, const Headers &headers, const QByteArray &body, Callback callback) override;
    Handle request(const QByteArray &method, const QUrl &url, const Headers &headers, const QByteArray &body,
                   Callback callback) override;
    void cancel(Handle handle) override;

private:
    Handle send(const QUrl &url, const Headers &headers, const QByteArray *body, Callback callback,
                bool keepErrorBodies = false, const QByteArray &method = {});

    QNetworkAccessManager *nam = nullptr;
    QHash<Handle, QPointer<QNetworkReply>> replies;
    Handle next = 0;
};

class SessionMemberDirectory : public MemberDirectory
{
public:
    explicit SessionMemberDirectory(Acheron::Core::Session *session)
        : session(session)
    {
    }
    std::optional<QList<Id>> memberRoles(Id accountId, Id guildId, Id userId) const override;

private:
    Acheron::Core::Session *session;
};

class QtClipboardService : public ClipboardService
{
public:
    void setText(const QString &text) override;
};

class SessionRelationshipDirectory : public RelationshipDirectory
{
public:
    explicit SessionRelationshipDirectory(Acheron::Core::Session *session)
        : session(session)
    {
    }
    bool isBlocked(Id accountId, Id userId) const override;
    bool isIgnored(Id accountId, Id userId) const override;
    QList<Id> relationshipIds(Id accountId, Kind kind) const override;
    bool hasRelationship(Id accountId, Id userId) const override;
    void fetchUserAffinities(Id accountId, AffinitiesCallback done) const override;

private:
    Acheron::Core::Session *session;
};

class QtUiService : public UiService
{
public:
    void showTextDialog(const QString &title, const QList<TextSection> &sections) override;
    void openUrl(const QUrl &url) override;
    void insertIntoMessageBox(const QString &text) override;
    void showToast(const QString &text, ToastType type) override;
    // Set by the main window
    static std::function<void(const QString &, ToastType)> &toastShower();
    void showNotification(const PluginNotification &notification, std::function<void()> clicked) override;
    static std::function<void(const PluginNotification &, std::function<void()>)> &notificationShower();
    void openDevicesSettings() override;
    static std::function<void()> &devicesOpener(); // set by the main window
    void authorizeApp(const OAuthRequest &request, std::function<void(std::optional<QString>)> done) override;
    quint64 openListDialog(const ListDialogView &view, ListDialogEvents events) override;
    void updateListDialog(quint64 handle, const ListDialogView &view) override;
    void closeListDialog(quint64 handle) override;
    struct ListDialogs // set by the main window
    {
        std::function<quint64(const ListDialogView &, ListDialogEvents)> open;
        std::function<void(quint64, const ListDialogView &)> update;
        std::function<void(quint64)> close;
    };
    static ListDialogs &listDialogs();
    static std::function<void(const OAuthRequest &, std::function<void(std::optional<QString>)>)> &appAuthorizer();
    void pickTimestamp(std::function<void(const QString &)> done) override;
    // Set by the main window, which owns the message box
    static std::function<void(const QString &)> &messageBoxInserter();
    void openPluginSettings(const QString &pluginId) override;
    void showForm(const QString &title, const QList<FormField> &fields,
                  std::function<void(const QString &, const QString &)> changed) override;
    void showFormDialog(const QString &title, const QList<FormField> &fields, const QString &confirmText,
                        std::function<void(const QHash<QString, QString> &)> done) override;
    void promptText(const QString &title, const QString &label, const QString &initial, int maxLength,
                    std::function<void(const QString &)> done) override;
    void promptTextWithReset(const QString &title, const QString &label, const QString &initial,
                             const QString &placeholder, const QString &resetLabel,
                             std::function<void(const QString &)> done) override;
    static std::function<void(const QString &)> &pluginSettingsOpener();
    void openImage(const QUrl &url) override;
    // Set by the main window, which owns the image viewer's parent
    static std::function<void(const QUrl &)> &imageOpener();
    void startEditing(const MessageContext &message) override;
    void startReply(const MessageContext &message, bool shiftHeld, std::optional<bool> mention) override;
    // Set by the main window (chat and message box)
    static std::function<void(const MessageContext &)> &editStarter();
    static std::function<void(const MessageContext &, bool, std::optional<bool>)> &replyStarter();
    bool confirm(Confirmation &confirmation) override;
    void showPermissions(const PermissionsView &view) override;
    void showMemberPermissions(const MemberPermissionsView &view) override;
    // Set by the main window, which can load images
    static std::function<void(const PermissionsView &)> &permissionsShower();
    static std::function<void(const MemberPermissionsView &)> &memberPermissionsShower();
    void refreshLockScreen() override;
    static std::function<void()> &lockScreenRefresher(); // set by the main window
    void refreshAttachments(Id accountId, Id channelId, Id messageId) override;
    void showServerInfo(const ServerInfoView &view) override;
    void cancelReply() override;
    void cancelEditing() override;
    void focusMessageBox() override;
    void revealMessage(Id messageId) override;
    // Set by the main window, which owns the chat and the message box
    struct ChatControls
    {
        std::function<void()> cancelReply;
        std::function<void()> cancelEditing;
        std::function<void()> focusMessageBox;
        std::function<void(Id)> revealMessage;
    };
    static ChatControls &chatControls();
    void openUserProfile(Id accountId, Id guildId, Id userId) override;
    static std::function<void(const ServerInfoView &)> &serverInfoShower(); // set by the main window
    static std::function<void(Id, Id, Id)> &profileOpener(); // set by the main window
    [[nodiscard]] Id currentAccountId() const override;
    static std::function<Id()> &currentAccountProvider(); // set by the main window
    void openChannel(Id accountId, Id channelId) override;
    static std::function<void(Id, Id)> &channelOpener(); // set by the main window
    void joinVoiceChannel(Id accountId, Id channelId) override;
    void leaveVoice(Id accountId) override;
    void showLocalMessage(Id accountId, Id channelId, const QString &content) override;
    static std::function<void(Id, Id)> &voiceJoiner(); // set by the main window
    static std::function<void(Id)> &voiceLeaver(); // set by the main window
    static std::function<void(Id, Id, const QString &)> &localMessageShower(); // set by the main window
    static std::function<void(Id, Id, Id)> &attachmentRefresher(); // set by the main window
    void attachFile(const QString &fileName, const QByteArray &data, const QString &mimeType) override;
    // Set by the main window, which owns the message box
    static std::function<void(const QString &, const QByteArray &, const QString &)> &fileAttacher();

private:
    static void showTextPrompt(const QString &title, const QString &label, const QString &initial, int maxLength,
                               const QString &placeholder, const QString &resetLabel,
                               std::function<void(const QString &)> done);
};

// Emojis, stickers, Nitro status and channel permissions from the session
// Reaction users from Discord (WhoReacted); the users are kept for names
// and avatars
class SessionReactions : public ReactionService
{
public:
    explicit SessionReactions(Acheron::Core::Session *session) : session(session) {}
    void fetchReactionUsers(Id accountId, Id channelId, Id messageId, const QString &emojiKey, int type,
                            Callback done) override;
    void reactionUsersChanged(Id accountId, Id channelId, Id messageId) override;
    // Set by the main window, which re-renders the message
    static std::function<void(Id accountId, Id channelId, Id messageId)> &onChanged();

private:
    Acheron::Core::Session *session;
};

// Roles, owners, channel overwrites and members from the session's cache
class SessionGuilds : public GuildDirectory
{
public:
    explicit SessionGuilds(Acheron::Core::Session *session) : session(session) {}
    [[nodiscard]] std::optional<GuildInfo> guild(Id accountId, Id guildId) const override;
    [[nodiscard]] std::optional<ChannelInfo> channel(Id accountId, Id channelId) const override;
    [[nodiscard]] std::optional<MemberInfo> member(Id accountId, Id guildId, Id userId) const override;
    void requestMembers(Id accountId, Id guildId, const QList<Id> &userIds) override;
    [[nodiscard]] QList<Id> guildIds(Id accountId) const override;
    [[nodiscard]] QList<Id> memberIds(Id accountId, Id guildId) const override;
    [[nodiscard]] std::optional<QString> rawJson(Id accountId, RawKind kind, Id id, Id guildId) const override;
    // Set by the main window, which lists the servers
    static std::function<QList<Id>(Id accountId)> &guildIdsProvider();

private:
    Acheron::Core::Session *session;
};

// Users on Discord (ValidUser)
class QtAudioOptions : public AudioOptionsService
{
public:
    void setUserVolumeMultiplier(double multiplier) override;
    PluginHost *host = nullptr;
};

// The DM list, as the main window's channel list has it
class QtPrivateChannels : public PrivateChannelDirectory
{
public:
    QList<PrivateChannelInfo> privateChannels(Id accountId) const override;
    static std::function<QList<PrivateChannelInfo>(Id accountId)> &provider(); // set by the main window
};

class SessionMessageLookup : public MessageLookupService
{
public:
    explicit SessionMessageLookup(Acheron::Core::Session *session) : session(session) {}
    std::optional<LinkedMessage> message(Id accountId, Id channelId, Id messageId) override;
    void fetchMessage(Id accountId, Id channelId, Id messageId, std::function<void(bool)> done) override;
    std::optional<LinkedChannel> channel(Id accountId, Id channelId) override;

private:
    Acheron::Core::Session *session;
};

class SessionVoiceStates : public VoiceStateDirectory
{
public:
    explicit SessionVoiceStates(Acheron::Core::Session *session) : session(session), lookup(session) {}
    std::optional<UserVoiceState> voiceState(Id accountId, Id userId) const override;
    QList<Id> channelUsers(Id accountId, Id channelId) const override;
    std::optional<LinkedChannel> channel(Id accountId, Id channelId) const override;
    QString displayName(Id accountId, Id guildId, Id userId) const override;

private:
    Acheron::Core::Session *session;
    mutable SessionMessageLookup lookup;
};

class SessionActivity : public ActivitySettingsService
{
public:
    explicit SessionActivity(Acheron::Core::Session *session) : session(session) {}
    bool showCurrentGame(Id accountId) const override;
    void setShowCurrentGame(Id accountId, bool show) override;
    void fetchConnections(Id accountId, std::function<void(bool, const QList<ActivityConnection> &)> done) override;
    void setConnectionShowActivity(Id accountId, const QString &type, const QString &connectionId, bool show,
                                   std::function<void(bool)> done) override;

private:
    Acheron::Core::Session *session;
};

class SessionPresences : public PresenceDirectory
{
public:
    explicit SessionPresences(Acheron::Core::Session *session) : session(session) {}
    QString status(Id accountId, Id userId) const override;

private:
    Acheron::Core::Session *session;
};

class SessionDevices : public SessionService
{
public:
    explicit SessionDevices(Acheron::Core::Session *session) : session(session) {}
    void fetchSessions(Id accountId, std::function<void(bool, const QList<DeviceSession> &)> done) override;
    void refreshDevicesPage() override;
    QList<Id> accountIds() const override;
    static std::function<void()> &devicesRefresher(); // set by the main window

private:
    Acheron::Core::Session *session;
};

class SessionUsers : public UserLookupService
{
public:
    explicit SessionUsers(Acheron::Core::Session *session) : session(session) {}
    [[nodiscard]] bool isKnown(Id accountId, Id userId) const override;
    void fetchUser(Id accountId, Id userId, Callback done) override;
    void refreshMessage(Id accountId, Id channelId, Id messageId) override;

private:
    Acheron::Core::Session *session;
};

class SessionExpressions : public ExpressionService
{
public:
    explicit SessionExpressions(Acheron::Core::Session *session)
        : session(session)
    {
    }
    [[nodiscard]] int premiumType(Id accountId) const override;
    [[nodiscard]] std::optional<CustomEmojiInfo> customEmoji(Id accountId, Id emojiId) const override;
    [[nodiscard]] std::optional<StickerInfo> sticker(Id accountId, Id stickerId) const override;
    [[nodiscard]] QList<Id> ownRoles(Id accountId, Id guildId) const override;
    [[nodiscard]] bool hasChannelPermission(Id accountId, Id channelId, quint64 permission) const override;
    void setChatBypass(bool emojis, bool stickers) override;
    [[nodiscard]] bool hasGuildPermission(Id accountId, Id guildId, quint64 permission) const override;
    void setStreamQualityBypass(bool on) override;
    void setOversizeImageHandler(ImageShrinker shrink) override;
    void setLongMessageSplitter(SplitCounter count) override;
    void setVideoShrinking(bool on) override;
    void setVideoShrinkOptions(int priority, int maxSeconds, bool trimToFit) override;
    void setFakeReactions(bool on) override;

private:
    Acheron::Core::Session *session;
};

class QtDisplayOptions : public DisplayOptionsService
{
public:
    void setRelativeTimeRoundsDown(bool floor) override;
    void setMobileIndicatorForAnyStatus(bool any) override;
    void setForceOwnerCrown(bool force) override;
    void setRoleColors(const RoleColors &colors) override;
    void setFolderSidebar(const std::optional<FolderSidebar> &folders) override;
    void setImageLens(const std::optional<ImageLens> &lens, std::function<void(double, int)> changed) override;
    void setCompactConnections(const std::optional<CompactConnections> &connections, GithubOrgsLookup githubOrgs) override;
    void setRoleDot(const std::optional<RoleDot> &dot) override;
    PluginHost *host = nullptr;
};

// Marks servers read through the main window, which knows the server list
class QtReadStates : public ReadStateService
{
public:
    void markAllServersRead(Id accountId) override;
    static std::function<void(Id accountId)> &allServersMarker();
};

class SessionMessageActions : public MessageActionService
{
public:
    explicit SessionMessageActions(Acheron::Core::Session *session)
        : session(session)
    {
    }
    void setMessageFlags(Id accountId, Id channelId, Id messageId, quint64 flags) override;
    void deleteMessage(Id accountId, Id channelId, Id messageId) override;
    void setPinned(Id accountId, Id channelId, Id messageId, bool pinned) override;
    void setReaction(Id accountId, Id channelId, Id messageId, const QString &emoji, bool add) override;

private:
    Acheron::Core::Session *session;
};

class SessionMessageService : public MessageService
{
public:
    explicit SessionMessageService(Acheron::Core::Session *session)
        : session(session)
    {
    }
    void refreshMessage(Id accountId, Id channelId, Id messageId) override;
    void removeMessage(Id accountId, Id channelId, Id messageId) override;
    void refreshFilters() override;
    PluginHost *host = nullptr;

private:
    Acheron::Core::Session *session;
};

// Owns the plugin host for the lifetime of the session and publishes it as
// PluginHost::instance() for client call sites.
class ClientPlugins
{
public:
    explicit ClientPlugins(Acheron::Core::Session *session);
    ~ClientPlugins();

    ClientPlugins(const ClientPlugins &) = delete;
    ClientPlugins &operator=(const ClientPlugins &) = delete;

    [[nodiscard]] PluginHost *host() const { return pluginHost.get(); }
    // Custom settings widgets of the built-in plugins
    static const SettingsWidgetRegistry &settingsWidgets();

private:
    QtNetworkService network;
    SessionMemberDirectory members;
    QtClipboardService clipboard;
    SessionRelationshipDirectory relationships;
    QtUiService ui;
    SessionMessageService messages;
    SessionMessageActions messageActions;
    QtDisplayOptions displayOptions;
    QtReadStates readStates;
    SessionExpressions expressions;
    SessionReactions reactions;
    SessionGuilds guilds;
    SessionUsers users;
    QtPrivateChannels privateChannels;
    QtAudioOptions audio;
    SessionMessageLookup messageLookup;
    SessionVoiceStates voiceStates;
    SessionActivity activity;
    SessionDevices devices;
    SessionPresences presences;
    std::unique_ptr<FileStorage> storage;
    std::unique_ptr<PluginHost> pluginHost;
};

} // namespace Plugins
} // namespace Qord
