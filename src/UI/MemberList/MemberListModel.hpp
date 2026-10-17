#pragma once

#include <functional>

#include <QAbstractListModel>
#include <QPixmap>
#include <QPointer>

#include "Core/MemberListManager.hpp"
#include "Core/ImageManager.hpp"
#include "Core/Presence/PresenceManager.hpp"
#include "UI/AvatarRequestTracker.hpp"

namespace Acheron {
namespace UI {

struct MemberActivity
{
    QString text;
    Discord::ActivityType kind = Discord::ActivityType::PLAYING;
    QPixmap emoji;
    QString emojiText;
};

class MemberListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        ItemTypeRole = Qt::UserRole + 1,
        UserIdRole,
        UsernameRole,
        AvatarRole,
        RoleColorRole,
        GroupNameRole,
        GroupCountRole,
        GroupColorRole,
        LoadedRole,
        PresenceBadgeRole,
        HasActivityRole,
        ActivityRole,
        AccountIdRole, // Qord: for name decorations
        GuildIdRole,
        OwnerCrownRole, // Qord: the server owner's crown is shown
        IsTypingRole, // Qord: typing in the open channel (Discord's dots on the avatar)
    };

    // Qord: the crown next to a server's owner. Like Discord, it is left out
    // in large servers (Qord's limit: LargeServerCrownLimit members) unless a
    // plugin forces it (ForceOwnerCrown, Core::forceOwnerCrown).
    static constexpr int LargeServerCrownLimit = 1000;
    using OwnerProvider = std::function<Core::Snowflake(Core::Snowflake guildId)>;
    void setOwnerProvider(OwnerProvider provider) { ownerProvider = std::move(provider); }
    // Qord: who is typing in the open channel
    using TypingProvider = std::function<bool(Core::Snowflake userId)>;
    void setTypingProvider(TypingProvider provider) { typingProvider = std::move(provider); }
    // Repaints the typing dots after the typers changed
    void typingChanged();

    explicit MemberListModel(Core::ImageManager *imageManager, QObject *parent = nullptr);

    void setManager(Core::MemberListManager *manager);
    void setPresenceManager(Core::PresenceManager *presences);
    void setAccount(Core::Snowflake accountId);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;

private:
    QVariant presenceData(const Core::MemberListItem &item, int role, const QModelIndex &index) const;
    QString activityText(const Core::MemberListItem &item, Core::Snowflake guildId) const;
    MemberActivity activityLine(const Core::MemberListItem &item, Core::Snowflake guildId, const QModelIndex &index) const;
    QVariant cachedImage(const QUrl &url, const QSize &size, const QModelIndex &index) const;

    void onListAboutToReset();
    void onListReset();
    void onItemsChanged(const QList<int> &indices);
    void onPresencesChanged(const QList<Core::Snowflake> &userIds);
    void onImageFetched(const QUrl &url, const QSize &size, const QPixmap &pixmap);

    void connectManager();
    void disconnectManager();

    void notifyRows(const QList<int> &rows);

    QPointer<Core::MemberListManager> manager;
    QPointer<Core::PresenceManager> presenceManager;
    Core::ImageManager *imageManager;
    Core::Snowflake accountId;

    mutable AvatarRequestTracker<QPersistentModelIndex> avatarTracker;
    OwnerProvider ownerProvider;
    TypingProvider typingProvider;
    mutable QHash<Core::Snowflake, Core::Snowflake> owners; // by guild
};

} // namespace UI
} // namespace Acheron

Q_DECLARE_METATYPE(Acheron::UI::MemberActivity)
