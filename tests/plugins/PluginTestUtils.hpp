#pragma once

// Helpers shared by plugin tests: a host holding a single real plugin
// (loaded through its real manifest), with fake client services.

#include "Plugins/PluginHost.hpp"

#include <QHash>
#include <QMap>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <memory>

namespace Qord {
namespace Plugins {
namespace Testing {

class FakeNetwork : public NetworkService
{
public:
    Handle get(const QUrl &url, Callback callback) override
    {
        const Handle h = ++next;
        requests.insert(h, { url, std::move(callback) });
        urls.append(url);
        return h;
    }
    Handle post(const QUrl &url, const Headers &headers, const QByteArray &body, Callback callback) override
    {
        const Handle h = ++next;
        requests.insert(h, { url, std::move(callback) });
        urls.append(url);
        posts.append({ url, headers, body });
        return h;
    }
    Handle request(const QByteArray &method, const QUrl &url, const Headers &headers, const QByteArray &body,
                   Callback callback) override
    {
        const Handle h = ++next;
        requests.insert(h, { url, std::move(callback) });
        urls.append(url);
        calls.append({ method, url, headers, body });
        return h;
    }
    struct Call
    {
        QByteArray method;
        QUrl url;
        Headers headers;
        QByteArray body;
    };
    QList<Call> calls; // request()
    void cancel(Handle handle) override
    {
        if (requests.remove(handle))
            ++cancelled;
    }
    // Completes the oldest outstanding request.
    bool respond(bool ok, const QByteArray &body, int status = 200)
    {
        if (requests.isEmpty())
            return false;
        auto it = requests.begin();
        for (auto i = requests.begin(); i != requests.end(); ++i)
            if (i.key() < it.key())
                it = i;
        Callback cb = it->callback;
        requests.erase(it);
        Reply r;
        r.ok = ok;
        r.status = status;
        r.body = body;
        r.error = ok ? QString() : QStringLiteral("simulated failure");
        cb(r);
        return true;
    }

    struct Pending
    {
        QUrl url;
        Callback callback;
    };
    struct Post
    {
        QUrl url;
        Headers headers;
        QByteArray body;
    };
    QList<Post> posts;
    QHash<Handle, Pending> requests;
    QList<QUrl> urls;
    int cancelled = 0;
    Handle next = 0;
};

class FakeMembers : public MemberDirectory
{
public:
    std::optional<QList<Id>> memberRoles(Id accountId, Id guildId, Id userId) const override
    {
        Q_UNUSED(accountId);
        ++lookups;
        auto it = roles.constFind(qMakePair(guildId, userId));
        if (it == roles.constEnd())
            return std::nullopt;
        return *it;
    }
    QHash<QPair<Id, Id>, QList<Id>> roles;
    mutable int lookups = 0;
};

class FakeClipboard : public ClipboardService
{
public:
    void setText(const QString &t) override { text = t; ++writes; }
    QString text;
    int writes = 0;
};

class FakeRelationships : public RelationshipDirectory
{
public:
    bool isBlocked(Id accountId, Id userId) const override
    {
        Q_UNUSED(accountId);
        return blocked.contains(userId);
    }
    bool isIgnored(Id accountId, Id userId) const override
    {
        Q_UNUSED(accountId);
        return ignored.contains(userId);
    }
    QList<Id> relationshipIds(Id, Kind kind) const override
    {
        const QSet<Id> &set = kind == Kind::Friend ? friends : kind == Kind::Blocked ? blocked : ignored;
        QList<Id> out(set.begin(), set.end());
        std::sort(out.begin(), out.end());
        return out;
    }
    bool hasRelationship(Id, Id userId) const override
    {
        return blocked.contains(userId) || friends.contains(userId) || others.contains(userId);
    }
    void fetchUserAffinities(Id accountId, AffinitiesCallback done) const override
    {
        affinityFetches.append(accountId);
        pendingAffinities.append(std::move(done));
    }
    QSet<Id> blocked;
    QSet<Id> ignored;
    QSet<Id> friends;
    QSet<Id> others; // requests and the like
    mutable QList<Id> affinityFetches;
    mutable QList<AffinitiesCallback> pendingAffinities;
};

class FakeUi : public UiService
{
public:
    void showTextDialog(const QString &title, const QList<TextSection> &sections) override
    {
        dialogTitle = title;
        dialogSections = sections;
        ++dialogs;
    }
    void openUrl(const QUrl &url) override { opened.append(url); }
    void openUserProfile(Id accountId, Id guildId, Id userId) override
    {
        Q_UNUSED(guildId);
        profilesOpened.append({ accountId, userId });
    }
    QList<QPair<Id, Id>> profilesOpened;
    Id currentAccountId() const override { return currentAccount; }
    Id currentAccount = 1;
    void showToast(const QString &text, ToastType type) override { toasts.append({ text, type }); }
    QList<QPair<QString, ToastType>> toasts;
    void showPermissions(const PermissionsView &view) override { permissionViews.append(view); }
    void showMemberPermissions(const MemberPermissionsView &view) override { memberPermissionViews.append(view); }
    QList<PermissionsView> permissionViews;
    void refreshAttachments(Id, Id, Id messageId) override { attachmentRefreshes.append(messageId); }
    void showServerInfo(const ServerInfoView &view) override { serverInfoViews.append(view); }
    void cancelReply() override { ++replyCancels; }
    void cancelEditing() override { ++editCancels; }
    void focusMessageBox() override { ++focuses; }
    void revealMessage(Id messageId) override { revealed.append(messageId); }
    void openChannel(Id accountId, Id channelId) override { openedChannels.append({ accountId, channelId }); }
    QList<QPair<Id, Id>> openedChannels;
    void joinVoiceChannel(Id accountId, Id channelId) override { joinedVoice.append({ accountId, channelId }); }
    void leaveVoice(Id accountId) override { leftVoice.append(accountId); }
    QList<Id> leftVoice;
    void showLocalMessage(Id accountId, Id channelId, const QString &content) override
    {
        localMessages.append({ accountId, channelId, content });
    }
    void previewDraft(Id accountId, Id channelId, const QString &content) override
    {
        localMessages.append({ accountId, channelId, content, true });
    }
    struct LocalMessage
    {
        Id accountId;
        Id channelId;
        QString content;
        bool withDraftAttachments = false;
    };
    QList<LocalMessage> localMessages;
    QList<QPair<Id, Id>> joinedVoice;
    void showForm(const QString &title, const QList<FormField> &fields,
                  std::function<void(const QString &, const QString &)> changed) override
    {
        formTitle = title;
        formFields = fields;
        formChanged = std::move(changed);
    }
    void showFormDialog(const QString &title, const QList<FormField> &fields, const QString &confirmText,
                        std::function<void(const QHash<QString, QString> &)> done) override
    {
        formTitle = title;
        formFields = fields;
        formConfirmText = confirmText;
        formDone = std::move(done);
    }
    QString formConfirmText;
    std::function<void(const QHash<QString, QString> &)> formDone;
    QString formTitle;
    QList<FormField> formFields;
    std::function<void(const QString &, const QString &)> formChanged;
    int replyCancels = 0;
    int editCancels = 0;
    int focuses = 0;
    QList<Id> revealed;
    QList<ServerInfoView> serverInfoViews;
    QList<Id> attachmentRefreshes;
    QList<MemberPermissionsView> memberPermissionViews;
    void openImage(const QUrl &url) override { images.append(url); }
    void startEditing(const MessageContext &message) override { edits.append(message.messageId); }
    void openThread(const MessageContext &message) override { threads.append(message.messageId); }
    QList<Id> threads;
    void startReply(const MessageContext &message, bool shiftHeld, std::optional<bool> mention) override
    {
        replies.append({ message.messageId, shiftHeld, mention });
    }
    struct Reply
    {
        Id messageId;
        bool shiftHeld;
        std::optional<bool> mention;
    };
    QList<Id> edits;
    QList<Reply> replies;
    void insertIntoMessageBox(const QString &text) override { inserted.append(text); }
    void pickTimestamp(std::function<void(const QString &)> done) override { pendingPick = std::move(done); }
    void openPluginSettings(const QString &pluginId) override { settingsOpened.append(pluginId); }
    void showNotification(const PluginNotification &notification, std::function<void()> clicked) override
    {
        notifications.append(notification);
        notificationClicks.append(std::move(clicked));
    }
    QList<PluginNotification> notifications;
    QList<std::function<void()>> notificationClicks;
    void openDevicesSettings() override { ++devicesOpened; }
    int devicesOpened = 0;
    void authorizeApp(const OAuthRequest &request, std::function<void(std::optional<QString>)> done) override
    {
        authorizations.append(request);
        pendingAuthorization = std::move(done);
    }
    QList<OAuthRequest> authorizations;
    std::function<void(std::optional<QString>)> pendingAuthorization;
    quint64 openListDialog(const ListDialogView &view, ListDialogEvents events) override
    {
        const quint64 handle = ++lastDialog;
        listDialogs.insert(handle, view);
        dialogEvents.insert(handle, std::move(events));
        return handle;
    }
    void updateListDialog(quint64 handle, const ListDialogView &view) override
    {
        if (listDialogs.contains(handle))
            listDialogs.insert(handle, view);
    }
    void closeListDialog(quint64 handle) override
    {
        listDialogs.remove(handle);
        dialogEvents.remove(handle);
    }
    QHash<quint64, ListDialogView> listDialogs;
    QHash<quint64, ListDialogEvents> dialogEvents;
    quint64 lastDialog = 0;
    void promptText(const QString &title, const QString &, const QString &initial, int,
                    std::function<void(const QString &)> done) override
    {
        promptTitle = title;
        promptInitial = initial;
        pendingPrompt = std::move(done);
    }
    void promptTextWithReset(const QString &title, const QString &label, const QString &initial,
                             const QString &placeholder, const QString &resetLabel,
                             std::function<void(const QString &)> done) override
    {
        promptPlaceholder = placeholder;
        promptResetLabel = resetLabel;
        promptText(title, label, initial, 0, std::move(done));
    }
    QString promptPlaceholder, promptResetLabel;
    bool confirm(Confirmation &confirmation) override
    {
        confirmations.append(confirmation);
        if (checkboxAnswer)
            confirmation.checkboxChecked = *checkboxAnswer;
        return confirmAnswer;
    }
    void attachFile(const QString &fileName, const QByteArray &data, const QString &mimeType) override
    {
        attachments.append({ fileName, data, mimeType });
    }
    struct Attached
    {
        QString fileName;
        QByteArray data;
        QString mimeType;
    };
    QList<Confirmation> confirmations;
    bool confirmAnswer = true;
    std::optional<bool> checkboxAnswer;
    QList<Attached> attachments;
    QString promptTitle;
    QString promptInitial;
    std::function<void(const QString &)> pendingPrompt;
    QStringList settingsOpened;
    QStringList inserted;
    std::function<void(const QString &)> pendingPick;
    QString dialogTitle;
    QList<TextSection> dialogSections;
    int dialogs = 0;
    QList<QUrl> opened;
    QList<QUrl> images;
};

class FakeMessageLookup : public MessageLookupService
{
public:
    std::optional<LinkedMessage> message(Id, Id channelId, Id messageId) override
    {
        const auto it = messages.constFind(messageId);
        if (it == messages.constEnd() || it->channelId != channelId)
            return std::nullopt;
        return *it;
    }
    void fetchMessage(Id, Id channelId, Id messageId, std::function<void(bool)> done) override
    {
        fetches.append({ channelId, messageId });
        pending.append(std::move(done));
    }
    std::optional<LinkedChannel> channel(Id, Id channelId) override
    {
        const auto it = channels.constFind(channelId);
        return it == channels.constEnd() ? std::nullopt : std::optional<LinkedChannel>(*it);
    }
    QHash<Id, LinkedMessage> messages;
    QHash<Id, LinkedChannel> channels;
    QList<QPair<Id, Id>> fetches;
    QList<std::function<void(bool)>> pending;
};

class FakeVoiceStates : public VoiceStateDirectory
{
public:
    std::optional<UserVoiceState> voiceState(Id, Id userId) const override
    {
        const auto it = states.constFind(userId);
        return it == states.constEnd() ? std::nullopt : std::optional<UserVoiceState>(*it);
    }
    QList<Id> channelUsers(Id, Id channelId) const override
    {
        QList<Id> out;
        for (auto it = states.constBegin(); it != states.constEnd(); ++it)
            if (it->channelId == channelId)
                out.append(it.key());
        std::sort(out.begin(), out.end());
        return out;
    }
    std::optional<LinkedChannel> channel(Id, Id channelId) const override
    {
        const auto it = channels.constFind(channelId);
        return it == channels.constEnd() ? std::nullopt : std::optional<LinkedChannel>(*it);
    }
    QString displayName(Id, Id, Id userId) const override { return names.value(userId); }
    QHash<Id, UserVoiceState> states;
    QHash<Id, LinkedChannel> channels;
    QHash<Id, QString> names;
};

class FakeActivity : public ActivitySettingsService
{
public:
    bool showCurrentGame(Id) const override { return showGame; }
    void setShowCurrentGame(Id, bool show) override
    {
        showGame = show;
        ++gameChanges;
    }
    void fetchConnections(Id, std::function<void(bool, const QList<ActivityConnection> &)> done) override
    {
        ++connectionFetches;
        pendingFetches.append(std::move(done));
    }
    void setConnectionShowActivity(Id, const QString &type, const QString &id, bool show,
                                   std::function<void(bool)> done) override
    {
        activityChanges.append({ type, id, show });
        pendingChanges.append(std::move(done));
    }
    struct Change
    {
        QString type;
        QString id;
        bool show = false;
    };
    bool showGame = true;
    int gameChanges = 0;
    int connectionFetches = 0;
    QList<std::function<void(bool, const QList<ActivityConnection> &)>> pendingFetches;
    QList<Change> activityChanges;
    QList<std::function<void(bool)>> pendingChanges;
};

class FakePresences : public PresenceDirectory
{
public:
    QString status(Id, Id userId) const override { return statuses.value(userId, QStringLiteral("offline")); }
    QHash<Id, QString> statuses;
};

class FakeSessions : public SessionService
{
public:
    void fetchSessions(Id accountId, std::function<void(bool, const QList<DeviceSession> &)> done) override
    {
        fetches.append(accountId);
        pending.append(std::move(done));
    }
    void refreshDevicesPage() override { ++refreshes; }
    QList<Id> accountIds() const override { return accounts; }
    QList<Id> accounts{ 1 };
    QList<Id> fetches;
    QList<std::function<void(bool, const QList<DeviceSession> &)>> pending;
    int refreshes = 0;
};

class FakeAudio : public AudioOptionsService
{
public:
    void setUserVolumeMultiplier(double value) override { multiplier = value; }
    double multiplier = 1.0;
};

class FakePrivateChannels : public PrivateChannelDirectory
{
public:
    QList<PrivateChannelInfo> privateChannels(Id) const override { return channels; }
    QList<PrivateChannelInfo> channels;
};

class FakeMessages : public MessageService
{
public:
    void refreshMessage(Id, Id, Id messageId) override { refreshed.append(messageId); }
    void removeMessage(Id, Id, Id messageId) override { removed.append(messageId); }
    void refreshFilters() override { ++filterRefreshes; }
    QList<Id> refreshed;
    QList<Id> removed;
    int filterRefreshes = 0;
};

class FakeMessageActions : public MessageActionService
{
public:
    void setMessageFlags(Id, Id channelId, Id messageId, quint64 flags) override
    {
        calls.append({ channelId, messageId, flags });
    }
    void deleteMessage(Id, Id, Id messageId) override { deleted.append(messageId); }
    void setPinned(Id, Id, Id messageId, bool pinned) override { pins.append({ messageId, pinned }); }
    void setReaction(Id, Id, Id, const QString &emoji, bool add) override
    {
        reactions.append((add ? QStringLiteral("+") : QStringLiteral("-")) + emoji);
    }
    QList<Id> deleted;
    QList<QPair<Id, bool>> pins;
    QStringList reactions;
    struct Call
    {
        Id channelId;
        Id messageId;
        quint64 flags;
    };
    QList<Call> calls;
};

class FakeDisplayOptions : public DisplayOptionsService
{
public:
    void setRelativeTimeRoundsDown(bool floor) override { roundsDown = floor; }
    void setMobileIndicatorForAnyStatus(bool any) override { mobileAnyStatus = any; }
    void setForceOwnerCrown(bool force) override { ownerCrown = force; }
    void setRoleColors(const RoleColors &colors) override { roleColors = colors; ++roleColorUpdates; }
    void setImageLens(const std::optional<ImageLens> &value, std::function<void(double, int)> changed) override
    {
        lens = value;
        lensChanged = std::move(changed);
    }
    std::optional<ImageLens> lens;
    void setFolderSidebar(const std::optional<FolderSidebar> &value) override { folders = value; }
    std::optional<FolderSidebar> folders;
    void setCompactConnections(const std::optional<CompactConnections> &value, GithubOrgsLookup lookup) override
    {
        connections = value;
        githubOrgs = std::move(lookup);
    }
    std::optional<CompactConnections> connections;
    void setRoleDot(const std::optional<RoleDot> &value) override { roleDot = value; }
    std::optional<RoleDot> roleDot;
    GithubOrgsLookup githubOrgs;
    std::function<void(double, int)> lensChanged;
    RoleColors roleColors;
    int roleColorUpdates = 0;
    bool roundsDown = false;
    bool ownerCrown = false;
    bool mobileAnyStatus = false;
};

class FakeReadStates : public ReadStateService
{
public:
    void markAllServersRead(Id accountId) override { markedAccounts.append(accountId); }
    QList<Id> markedAccounts;
};

class FakeReactions : public ReactionService
{
public:
    void fetchReactionUsers(Id, Id, Id messageId, const QString &emojiKey, int type, Callback done) override
    {
        fetches.append({ messageId, emojiKey, type, std::move(done) });
    }
    void reactionUsersChanged(Id, Id, Id messageId) override { changed.append(messageId); }
    struct Fetch
    {
        Id messageId;
        QString emojiKey;
        int type;
        Callback done;
    };
    QList<Fetch> fetches;
    QList<Id> changed;
};

class FakeExpressions : public ExpressionService
{
public:
    int premiumType(Id) const override { return premium; }
    std::optional<CustomEmojiInfo> customEmoji(Id, Id emojiId) const override
    {
        if (emojis.contains(emojiId))
            return emojis.value(emojiId);
        return std::nullopt;
    }
    std::optional<StickerInfo> sticker(Id, Id stickerId) const override
    {
        if (stickers.contains(stickerId))
            return stickers.value(stickerId);
        return std::nullopt;
    }
    QList<Id> ownRoles(Id, Id guildId) const override { return roles.value(guildId); }
    bool hasChannelPermission(Id, Id, quint64 permission) const override { return !(denied & permission); }
    void setChatBypass(bool emojis, bool stickers) override { bypass = { emojis, stickers }; }
    bool hasGuildPermission(Id, Id guildId, quint64) const override { return adminOf.contains(guildId); }
    void setStreamQualityBypass(bool on) override { streamQuality = on; }
    void setOversizeImageHandler(ImageShrinker shrink) override { shrinker = std::move(shrink); }
    void setLongMessageSplitter(SplitCounter count) override { splitter = std::move(count); }
    void setVideoShrinking(bool on) override { shrinkVideos = on; }
    void setFakeReactions(bool on) override { fakeReactions = on; }
    void setVideoShrinkOptions(int priority, int maxSeconds, bool trimToFit) override
    {
        videoPriority = priority;
        maxVideoSeconds = maxSeconds;
        trimVideosToFit = trimToFit;
    }

    int premium = 0;
    QHash<Id, CustomEmojiInfo> emojis;
    QHash<Id, StickerInfo> stickers;
    QHash<Id, QList<Id>> roles;
    quint64 denied = 0;
    std::pair<bool, bool> bypass { false, false };
    QList<Id> adminOf;
    bool streamQuality = false;
    ImageShrinker shrinker;
    SplitCounter splitter;
    bool shrinkVideos = false;
    bool fakeReactions = false;
    int videoPriority = -1;
    int maxVideoSeconds = -1;
    bool trimVideosToFit = false;
};

class FakeGuilds : public GuildDirectory
{
public:
    std::optional<GuildInfo> guild(Id, Id guildId) const override
    {
        return guilds.contains(guildId) ? std::optional<GuildInfo>(guilds.value(guildId)) : std::nullopt;
    }
    std::optional<ChannelInfo> channel(Id, Id channelId) const override
    {
        return channels.contains(channelId) ? std::optional<ChannelInfo>(channels.value(channelId)) : std::nullopt;
    }
    std::optional<MemberInfo> member(Id, Id, Id userId) const override
    {
        return members.contains(userId) ? std::optional<MemberInfo>(members.value(userId)) : std::nullopt;
    }
    void requestMembers(Id, Id guildId, const QList<Id> &userIds) override { requested.append({ guildId, userIds }); }
    QList<Id> guildIds(Id) const override { return guildOrder; }
    QList<Id> memberIds(Id, Id guildId) const override { return guildMembers.value(guildId); }
    std::optional<QString> rawJson(Id, RawKind kind, Id id, Id) const override
    {
        const auto it = raw.constFind({ int(kind), id });
        return it == raw.constEnd() ? std::nullopt : std::optional<QString>(*it);
    }
    QMap<QPair<int, Id>, QString> raw;
    QList<Id> guildOrder;
    QHash<Id, QList<Id>> guildMembers;

    QHash<Id, GuildInfo> guilds;
    QHash<Id, ChannelInfo> channels;
    QHash<Id, MemberInfo> members;
    QList<QPair<Id, QList<Id>>> requested;
};

class FakeUsers : public UserLookupService
{
public:
    bool isKnown(Id, Id userId) const override { return known.contains(userId); }
    void fetchUser(Id, Id userId, Callback done) override { fetches.append({ userId, std::move(done) }); }
    void refreshMessage(Id, Id, Id messageId) override { refreshed.append(messageId); }
    struct Fetch
    {
        Id userId;
        Callback done;
    };
    QSet<Id> known;
    QList<Fetch> fetches;
    QList<Id> refreshed;
};

using RegisterFn = void (*)(Registry &);

// Owns a host with one registered plugin.
struct SinglePluginHost
{
    explicit SinglePluginHost(RegisterFn registerPlugin)
    {
        Registry registry;
        registerPlugin(registry);
        Services services;
        services.network = &network;
        services.members = &members;
        services.clipboard = &clipboard;
        services.relationships = &relationships;
        services.ui = &ui;
        services.messages = &messages;
        services.messageActions = &messageActions;
        services.displayOptions = &displayOptions;
        services.readStates = &readStates;
        services.reactions = &reactions;
        services.expressions = &expressions;
        services.guilds = &guilds;
        services.users = &users;
        services.privateChannels = &dms;
        services.audio = &audio;
        services.messageLookup = &messageLookup;
        services.voiceStates = &voiceStates;
        services.activity = &activity;
        services.sessions = &sessions;
        services.presences = &presences;
        host = std::make_unique<PluginHost>(registry, &storage, services, dataDir.path());
        host->load();
        id = host->pluginIds().value(0);
    }

    // Grants everything the manifest declares and enables the plugin.
    bool enableWithAllPermissions()
    {
        host->setGranted(id, info()->manifest.permissions);
        QString error;
        const bool ok = host->setEnabled(id, true, &error);
        if (!ok)
            qWarning() << "enable failed:" << error;
        return ok;
    }

    const PluginHost::PluginInfo *info() const { return host->info(id); }

    QTemporaryDir dataDir;
    MemoryStorage storage;
    FakeNetwork network;
    FakeMembers members;
    FakeClipboard clipboard;
    FakeRelationships relationships;
    FakeUi ui;
    FakeMessages messages;
    FakePrivateChannels dms;
    FakeAudio audio;
    FakeMessageLookup messageLookup;
    FakeVoiceStates voiceStates;
    FakeActivity activity;
    FakeSessions sessions;
    FakePresences presences;
    FakeMessageActions messageActions;
    FakeDisplayOptions displayOptions;
    FakeReadStates readStates;
    FakeReactions reactions;
    FakeExpressions expressions;
    FakeGuilds guilds;
    FakeUsers users;
    std::unique_ptr<PluginHost> host;
    QString id;
};

} // namespace Testing
} // namespace Plugins
} // namespace Qord
