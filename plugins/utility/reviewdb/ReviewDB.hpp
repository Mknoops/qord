/*
 * Qord, a native Discord client
 * Port of Equicord's ReviewDB plugin (src/plugins/reviewDB)
 * Original: Copyright (c) Vendicated and contributors, authors mantikafasi, V
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"
#include "Plugins/Services.hpp"

#include <QHash>
#include <QSet>

#include <functional>
#include <memory>

namespace Qord {
namespace Plugins {

class PluginContext;

// Reviews of users and servers from ReviewDB (manti.vendicated.dev): read
// them in profiles and a reviews window, and with a ReviewDB login write,
// vote on, report and delete them.
class ReviewDBPlugin : public Plugin
{
public:
    static constexpr int ReviewsPerPage = 50;
    static constexpr int WarningFlag = 0b10;
    static constexpr int StartupDelayMs = 4000; // upstream waits before asking who you are

    enum class ReviewType { User = 0, Server = 1, Support = 2, System = 3 };
    enum class UserType { Banned = -1, Normal = 0, Admin = 1 };

    struct Review
    {
        int id = 0;
        QString comment;
        int score = 0;
        ReviewType type = ReviewType::User;
        qint64 timestamp = 0; // seconds
        QString senderId; // Discord id
        QString senderName;
        QString senderPhoto;
        QStringList badges; // names
        std::optional<bool> userVote; // your vote: up, down, or none
    };
    struct ReviewsData
    {
        QList<Review> reviews;
        bool hasNextPage = false;
        int reviewCount = 0;
        bool hasOptedOut = false;
        QString message;
    };
    struct Notice
    {
        int id = 0;
        QString title;
        QString content;
        int type = 0; // 1: a ban
    };
    struct CurrentUser
    {
        QString discordId;
        UserType type = UserType::Normal;
        QStringList blockedUsers;
        qint64 lastReviewId = 0;
        std::optional<Notice> notification;
    };

    static ReviewsData parseReviews(const QByteArray &json);
    static std::optional<CurrentUser> parseCurrentUser(const QByteArray &json);
    // Upstream's error review, shown in place of the list when it can't load
    static Review systemReview(const QString &message);

    ReviewDBPlugin();
    ~ReviewDBPlugin() override;

    [[nodiscard]] Hooks hooks() const override
    {
        return Hook::ProfileTabs | Hook::UserContextMenu | Hook::ChannelContextMenu;
    }
    void start(PluginContext &context) override;
    void stop() override;
    void onSettingAction(const QString &key) override;

    QList<ProfileTab> profileTabs(const UserContext &user) override;
    void onProfileTabRowClicked(const QString &tabId, const QString &rowId, const UserContext &user) override;
    QList<MenuItem> userContextMenuItems(const UserContext &user) override;
    void onUserContextMenuAction(const QString &id, const UserContext &user) override;
    QList<MenuItem> channelContextMenuItems(const ChannelContext &channel) override;
    void onChannelContextMenuAction(const QString &id, const ChannelContext &channel) override;

    // The reviews window for a user or server
    void openReviews(Id accountId, const QString &targetId, const QString &name, ReviewType type);
    // Discord's app authorization, then a ReviewDB token for the account
    void authorize(Id accountId, std::function<void()> then = {});
    // Who you are on ReviewDB: new reviews on your profile, and its notices
    void checkCurrentUser(Id accountId);

    [[nodiscard]] QString token(Id accountId) const;

private:
    struct Window
    {
        Id accountId = 0;
        QString targetId;
        QString name;
        ReviewType type = ReviewType::User;
        int page = 1;
        ReviewsData data;
    };
    using Reply = std::function<void(bool ok, int status, const QByteArray &body)>;

    void request(Id accountId, const QByteArray &method, const QString &path, const QByteArray &body, Reply done);
    void fetchReviews(Id accountId, const QString &targetId, int page, bool withVotes,
                      std::function<void(const ReviewsData &)> done);
    void reload(quint64 handle);
    void render(quint64 handle);
    void onMenu(quint64 handle, const QString &itemId, const QString &menuId);
    void submit(quint64 handle, const QString &text);
    void setToken(Id accountId, const QString &token);
    void toast(const QString &text, UiService::ToastType type = UiService::ToastType::Message);
    void manageBlocked(Id accountId);
    [[nodiscard]] Id defaultAccount() const;
    [[nodiscard]] bool canDelete(Id accountId, const QString &profileId, const Review &review) const;

    PluginContext *context = nullptr;
    QHash<quint64, Window> windows; // by list dialog handle
    QHash<QString, ReviewsData> profileReviews; // "account/user", for profile tabs
    QSet<QString> profileFetches;
    QHash<Id, CurrentUser> me; // by account
    QSet<Id> accountsSeen; // the accounts the plugin has dealt with
    std::shared_ptr<bool> alive;
};

} // namespace Plugins
} // namespace Qord
