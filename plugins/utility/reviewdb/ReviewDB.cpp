/*
 * Qord, a native Discord client
 * Port of Equicord's ReviewDB plugin (src/plugins/reviewDB)
 * Original: Copyright (c) Vendicated and contributors, authors mantikafasi, V
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ReviewDB.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace Qord {
namespace Plugins {

namespace {
const QString ApiUrl = QStringLiteral("https://manti.vendicated.dev/api/reviewdb");
const QString ClientId = QStringLiteral("915703782174752809");
const QString RedirectUri = QStringLiteral("https://manti.vendicated.dev/api/reviewdb/auth");
const QString TabId = QStringLiteral("reviews");
const QString ViewReviewsId = QStringLiteral("vc-rdb-user-reviews");
const QString ServerReviewsId = QStringLiteral("vc-rdb-server-reviews");
const QString ReviewDBAvatar = QStringLiteral(
        "https://cdn.discordapp.com/avatars/1134864775000629298/3f87ad315b32ee464d84f1270c8d1b37.png?size=256&format=webp&quality=lossless");

QString key(Id accountId, const QString &userId)
{
    return QString::number(accountId) + QLatin1Char('/') + userId;
}

QString defaultAvatar(const QString &discordId)
{
    // Discord's default avatar for new usernames: (id >> 22) % 6
    return QStringLiteral("https://cdn.discordapp.com/embed/avatars/%1.png").arg((discordId.toULongLong() >> 22) % 6);
}
} // namespace

ReviewDBPlugin::Review ReviewDBPlugin::systemReview(const QString &message)
{
    Review r;
    r.id = 0;
    r.comment = message;
    r.type = ReviewType::System;
    r.senderId = QStringLiteral("1134864775000629298");
    r.senderName = QStringLiteral("ReviewDB");
    r.senderPhoto = ReviewDBAvatar;
    return r;
}

ReviewDBPlugin::ReviewsData ReviewDBPlugin::parseReviews(const QByteArray &json)
{
    ReviewsData out;
    const QJsonObject obj = QJsonDocument::fromJson(json).object();
    out.hasNextPage = obj.value(QStringLiteral("hasNextPage")).toBool();
    out.reviewCount = obj.value(QStringLiteral("reviewCount")).toInt();
    out.hasOptedOut = obj.value(QStringLiteral("hasOptedOut")).toBool();
    out.message = obj.value(QStringLiteral("message")).toString();
    for (const QJsonValue &v : obj.value(QStringLiteral("reviews")).toArray()) {
        const QJsonObject r = v.toObject();
        const QJsonObject sender = r.value(QStringLiteral("sender")).toObject();
        Review review;
        review.id = r.value(QStringLiteral("id")).toInt();
        review.comment = r.value(QStringLiteral("comment")).toString();
        review.score = r.value(QStringLiteral("score")).toInt();
        review.type = ReviewType(r.value(QStringLiteral("type")).toInt());
        review.timestamp = qint64(r.value(QStringLiteral("timestamp")).toDouble());
        review.senderId = sender.value(QStringLiteral("discordID")).toString();
        review.senderName = sender.value(QStringLiteral("username")).toString();
        review.senderPhoto = sender.value(QStringLiteral("profilePhoto")).toString();
        for (const QJsonValue &b : sender.value(QStringLiteral("badges")).toArray())
            review.badges.append(b.toObject().value(QStringLiteral("name")).toString());
        out.reviews.append(review);
    }
    return out;
}

std::optional<ReviewDBPlugin::CurrentUser> ReviewDBPlugin::parseCurrentUser(const QByteArray &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return std::nullopt;
    const QJsonObject obj = doc.object();
    CurrentUser user;
    user.discordId = obj.value(QStringLiteral("discordID")).toString();
    user.type = UserType(obj.value(QStringLiteral("type")).toInt());
    user.lastReviewId = qint64(obj.value(QStringLiteral("lastReviewID")).toDouble());
    for (const QJsonValue &b : obj.value(QStringLiteral("blockedUsers")).toArray())
        user.blockedUsers.append(b.toString());
    const QJsonObject n = obj.value(QStringLiteral("notification")).toObject();
    if (!n.isEmpty())
        user.notification = Notice{ n.value(QStringLiteral("id")).toInt(), n.value(QStringLiteral("title")).toString(),
                                    n.value(QStringLiteral("content")).toString(), n.value(QStringLiteral("type")).toInt() };
    return user;
}

ReviewDBPlugin::ReviewDBPlugin() = default;
ReviewDBPlugin::~ReviewDBPlugin() = default;

void ReviewDBPlugin::start(PluginContext &ctx)
{
    context = &ctx;
    alive = std::make_shared<bool>(true);
    // Upstream asks who you are a few seconds after starting
    std::weak_ptr<bool> weak = alive;
    QTimer::singleShot(StartupDelayMs, [this, weak]() {
        if (weak.expired())
            return;
        const QJsonObject tokens = QJsonDocument::fromJson(context->settings().stringValue(QStringLiteral("tokens")).toUtf8()).object();
        for (auto it = tokens.begin(); it != tokens.end(); ++it)
            checkCurrentUser(it.key().toULongLong());
    });
}

void ReviewDBPlugin::stop()
{
    alive.reset();
    windows.clear();
    profileReviews.clear();
    profileFetches.clear();
    me.clear();
    context = nullptr;
}

QString ReviewDBPlugin::token(Id accountId) const
{
    if (!context)
        return {};
    const QJsonObject tokens =
            QJsonDocument::fromJson(context->settings().stringValue(QStringLiteral("tokens")).toUtf8()).object();
    return tokens.value(QString::number(accountId)).toString();
}

void ReviewDBPlugin::setToken(Id accountId, const QString &value)
{
    QJsonObject tokens = QJsonDocument::fromJson(context->settings().stringValue(QStringLiteral("tokens")).toUtf8()).object();
    tokens.insert(QString::number(accountId), value);
    context->settings().setValue(QStringLiteral("tokens"),
                                 QString::fromUtf8(QJsonDocument(tokens).toJson(QJsonDocument::Compact)));
}

Id ReviewDBPlugin::defaultAccount() const
{
    return accountsSeen.isEmpty() ? 0 : *accountsSeen.begin();
}

void ReviewDBPlugin::toast(const QString &text, UiService::ToastType type)
{
    if (UiService *ui = context ? context->ui() : nullptr)
        ui->showToast(text, type);
}

void ReviewDBPlugin::request(Id accountId, const QByteArray &method, const QString &path, const QByteArray &body,
                             Reply done)
{
    NetworkService *network = context ? context->network() : nullptr;
    if (!network) {
        done(false, 0, {});
        return;
    }
    // The token is a secret: it only travels in this header
    NetworkService::Headers headers{ { "Accept", "application/json" },
                                     { "Authorization", token(accountId).toUtf8() } };
    if (!body.isEmpty())
        headers.append(qMakePair(QByteArray("Content-Type"), QByteArray("application/json")));
    std::weak_ptr<bool> weak = alive;
    network->request(method, QUrl(ApiUrl + path), headers, body,
                     [weak, done = std::move(done)](const NetworkService::Reply &reply) {
                         if (!weak.expired())
                             done(reply.ok, reply.status, reply.body);
                     });
}

void ReviewDBPlugin::fetchReviews(Id accountId, const QString &targetId, int page, bool withVotes,
                                  std::function<void(const ReviewsData &)> done)
{
    QUrlQuery query;
    if (!context->settings().boolValue(QStringLiteral("showWarning")))
        query.addQueryItem(QStringLiteral("flags"), QString::number(WarningFlag));
    if (page > 1)
        query.addQueryItem(QStringLiteral("offset"), QString::number((page - 1) * ReviewsPerPage));
    query.addQueryItem(QStringLiteral("limit"), QString::number(ReviewsPerPage));
    const QString path = QStringLiteral("/users/%1/reviews?%2").arg(targetId, query.toString(QUrl::FullyEncoded));
    std::weak_ptr<bool> weak = alive;
    request(accountId, "GET", path, {}, [this, weak, accountId, targetId, withVotes, done](bool ok, int status, const QByteArray &body) {
        if (weak.expired())
            return;
        if (!ok) {
            const QString message = status == 429
                                            ? QStringLiteral("You are sending requests too fast. Wait a few seconds and try again.")
                                            : QStringLiteral("An Error occured while fetching reviews. Please try again later.");
            toast(message, UiService::ToastType::Failure);
            ReviewsData failed;
            failed.message = message;
            failed.reviews.append(systemReview(message));
            done(failed);
            return;
        }
        ReviewsData data = parseReviews(body);
        // Reviews from people you blocked on Discord stay hidden
        const RelationshipDirectory *relationships = context->relationships();
        if (context->settings().boolValue(QStringLiteral("hideBlockedUsers")) && relationships)
            data.reviews.erase(std::remove_if(data.reviews.begin(), data.reviews.end(),
                                              [&](const Review &r) {
                                                  return relationships->isBlocked(accountId, r.senderId.toULongLong());
                                              }),
                               data.reviews.end());
        // System messages first
        std::stable_partition(data.reviews.begin(), data.reviews.end(),
                              [](const Review &r) { return r.type == ReviewType::System; });
        if (!withVotes || token(accountId).isEmpty() || data.reviews.isEmpty()) {
            done(data);
            return;
        }
        request(accountId, "GET", QStringLiteral("/users/%1/reviews/votes").arg(targetId), {},
                [data, done](bool ok, int, const QByteArray &body) mutable {
                    if (ok) {
                        QHash<int, bool> votes;
                        for (const QJsonValue &v : QJsonDocument::fromJson(body).object().value(QStringLiteral("votes")).toArray())
                            votes.insert(v.toObject().value(QStringLiteral("reviewID")).toInt(),
                                         v.toObject().value(QStringLiteral("isUpvote")).toBool());
                        for (Review &r : data.reviews)
                            if (votes.contains(r.id))
                                r.userVote = votes.value(r.id);
                    }
                    done(data);
                });
    });
}

// ------------------------------------------------------------------ surfaces

QList<ProfileTab> ReviewDBPlugin::profileTabs(const UserContext &user)
{
    if (!context || !user.userId)
        return {};
    accountsSeen.insert(user.accountId);
    const QString k = key(user.accountId, QString::number(user.userId));
    ProfileTab tab;
    tab.id = TabId;
    tab.title = QStringLiteral("User Reviews");
    const auto it = profileReviews.constFind(k);
    if (it == profileReviews.constEnd()) {
        tab.emptyText = QStringLiteral("Loading…");
        if (!profileFetches.contains(k)) {
            profileFetches.insert(k);
            std::weak_ptr<bool> weak = alive;
            fetchReviews(user.accountId, QString::number(user.userId), 1, false, [this, weak, k](const ReviewsData &data) {
                if (weak.expired())
                    return;
                profileFetches.remove(k);
                profileReviews.insert(k, data);
                context->refreshProfileTabs();
            });
        }
        return { tab };
    }
    tab.emptyText = it->hasOptedOut ? QStringLiteral("User opted out") : QStringLiteral("No reviews yet");
    for (const Review &r : it->reviews) {
        if (r.id == 0)
            continue;
        ProfileTab::Row row;
        row.id = QString::number(r.id);
        row.title = r.senderName;
        row.subtitle = r.comment;
        row.iconUrl = r.senderPhoto.isEmpty() ? defaultAvatar(r.senderId) : r.senderPhoto;
        tab.rows.append(row);
    }
    if (it->reviewCount > 0)
        tab.title = QStringLiteral("User Reviews (%1)").arg(it->reviewCount);
    return { tab };
}

void ReviewDBPlugin::onProfileTabRowClicked(const QString &tabId, const QString &rowId, const UserContext &user)
{
    Q_UNUSED(rowId);
    if (tabId != TabId)
        return;
    openReviews(user.accountId, QString::number(user.userId),
                user.username.isEmpty() ? QString::number(user.userId) : user.username, ReviewType::User);
}

QList<MenuItem> ReviewDBPlugin::userContextMenuItems(const UserContext &user)
{
    if (!user.userId)
        return {};
    accountsSeen.insert(user.accountId);
    return { { ViewReviewsId, QStringLiteral("View Reviews") } };
}

void ReviewDBPlugin::onUserContextMenuAction(const QString &id, const UserContext &user)
{
    if (id == ViewReviewsId)
        openReviews(user.accountId, QString::number(user.userId),
                    user.username.isEmpty() ? QString::number(user.userId) : user.username, ReviewType::User);
}

QList<MenuItem> ReviewDBPlugin::channelContextMenuItems(const ChannelContext &channel)
{
    // The server's menu (guild-context, guild-header-popout)
    if (!channel.guildId || channel.channelId)
        return {};
    accountsSeen.insert(channel.accountId);
    return { { ServerReviewsId, QStringLiteral("View Reviews") } };
}

void ReviewDBPlugin::onChannelContextMenuAction(const QString &id, const ChannelContext &channel)
{
    if (id == ServerReviewsId)
        openReviews(channel.accountId, QString::number(channel.guildId), channel.name, ReviewType::Server);
}

// -------------------------------------------------------------- the window

void ReviewDBPlugin::openReviews(Id accountId, const QString &targetId, const QString &name, ReviewType type)
{
    UiService *ui = context ? context->ui() : nullptr;
    if (!ui)
        return;
    accountsSeen.insert(accountId);
    ListDialogEvents events;
    auto handle = std::make_shared<quint64>(0);
    events.menuAction = [this, handle](const QString &item, const QString &menu) { onMenu(*handle, item, menu); };
    events.submitted = [this, handle](const QString &text) { submit(*handle, text); };
    events.page = [this, handle](int delta) {
        auto it = windows.find(*handle);
        if (it == windows.end())
            return;
        const int pages = qMax(1, (it->data.reviewCount + ReviewsPerPage - 1) / ReviewsPerPage);
        it->page = qBound(1, it->page + delta, pages);
        reload(*handle);
    };
    events.inputBlocked = [this, accountId, handle]() {
        toast(QStringLiteral("Opening authorization window..."));
        authorize(accountId, [this, handle]() { render(*handle); });
    };
    events.closed = [this, handle]() { windows.remove(*handle); };
    ListDialogView view;
    view.title = QStringLiteral("%1's Reviews").arg(name);
    view.emptyText = QStringLiteral("Loading…");
    *handle = ui->openListDialog(view, std::move(events));
    if (!*handle)
        return;
    Window window;
    window.accountId = accountId;
    window.targetId = targetId;
    window.name = name;
    window.type = type;
    windows.insert(*handle, window);
    reload(*handle);
}

void ReviewDBPlugin::reload(quint64 handle)
{
    const auto it = windows.constFind(handle);
    if (it == windows.constEnd())
        return;
    std::weak_ptr<bool> weak = alive;
    fetchReviews(it->accountId, it->targetId, it->page, true, [this, weak, handle](const ReviewsData &data) {
        if (weak.expired() || !windows.contains(handle))
            return;
        windows[handle].data = data;
        render(handle);
    });
}

bool ReviewDBPlugin::canDelete(Id accountId, const QString &profileId, const Review &review) const
{
    const QString myId = QString::number(accountId);
    const auto user = me.constFind(accountId);
    return myId == profileId || review.senderId == myId || (user != me.constEnd() && user->type == UserType::Admin);
}

void ReviewDBPlugin::render(quint64 handle)
{
    UiService *ui = context ? context->ui() : nullptr;
    const auto it = windows.constFind(handle);
    if (!ui || it == windows.constEnd())
        return;
    const Window &w = *it;
    const QString myId = QString::number(w.accountId);
    const bool loggedIn = !token(w.accountId).isEmpty();
    const QStringList blocked = me.value(w.accountId).blockedUsers;
    const bool hideTimes = context->settings().boolValue(QStringLiteral("hideTimestamps"));

    ListDialogView view;
    view.title = QStringLiteral("%1's Reviews").arg(w.name);
    if (w.data.reviewCount > 0)
        view.title += QStringLiteral(" (%1 Reviews)").arg(w.data.reviewCount);
    view.emptyText = QStringLiteral("Looks like nobody reviewed this %1 yet. You could be the first!")
                             .arg(w.type == ReviewType::Server ? QStringLiteral("server") : QStringLiteral("user"));
    bool isAuthor = false;
    for (const Review &r : w.data.reviews) {
        isAuthor |= r.senderId == myId;
        ListDialogItem item;
        item.id = QString::number(r.id);
        item.avatarUrl = r.senderPhoto.isEmpty() ? defaultAvatar(r.senderId) : r.senderPhoto;
        item.title = r.senderName;
        QStringList badges = r.badges;
        if (r.type == ReviewType::System)
            badges.prepend(QStringLiteral("System"));
        if (blocked.contains(r.senderId))
            badges.prepend(QStringLiteral("You have blocked this user"));
        item.badge = badges.join(QStringLiteral(" · "));
        QStringList meta;
        if (r.id != 0)
            meta.append((r.score > 0 ? QStringLiteral("+") : QString()) + QString::number(r.score));
        if (!hideTimes && r.type != ReviewType::System)
            meta.append(QLocale().toString(QDateTime::fromSecsSinceEpoch(r.timestamp).date(), QLocale::ShortFormat));
        item.meta = meta.join(QStringLiteral(" · "));
        item.body = r.comment;
        if (r.id != 0) {
            item.menu.append({ QStringLiteral("profile"), QStringLiteral("View Profile") });
            if (r.senderId != myId) {
                item.menu.append({ QStringLiteral("up"), r.userVote == true ? QStringLiteral("Remove Upvote")
                                                                            : QStringLiteral("Upvote") });
                item.menu.append({ QStringLiteral("down"), r.userVote == false ? QStringLiteral("Remove Downvote")
                                                                               : QStringLiteral("Downvote") });
                item.menu.append({ QStringLiteral("report"), QStringLiteral("Report Review") });
            }
            // On your own profile you can block reviewers
            if (w.targetId == myId && r.senderId != myId)
                item.menu.append({ QStringLiteral("block"), blocked.contains(r.senderId) ? QStringLiteral("Unblock User")
                                                                                         : QStringLiteral("Block User") });
            if (canDelete(w.accountId, w.targetId, r))
                item.menu.append({ QStringLiteral("delete"), QStringLiteral("Delete Review") });
        }
        view.items.append(item);
    }
    view.inputPlaceholder = !loggedIn ? QStringLiteral("You need to authorize to review users!")
                            : isAuthor ? QStringLiteral("Update review for @%1").arg(w.name)
                                       : QStringLiteral("Review @%1").arg(w.name);
    view.inputEnabled = loggedIn;
    if (w.data.reviewCount > 0) {
        const int pages = qMax(1, (w.data.reviewCount + ReviewsPerPage - 1) / ReviewsPerPage);
        if (pages > 1) {
            view.pageText = QStringLiteral("Page %1 of %2").arg(w.page).arg(pages);
            view.canPrevious = w.page > 1;
            view.canNext = w.page < pages;
        }
    }
    ui->updateListDialog(handle, view);
}

void ReviewDBPlugin::onMenu(quint64 handle, const QString &itemId, const QString &menuId)
{
    UiService *ui = context ? context->ui() : nullptr;
    const auto it = windows.constFind(handle);
    if (!ui || it == windows.constEnd())
        return;
    const Window w = *it;
    const auto review = std::find_if(w.data.reviews.cbegin(), w.data.reviews.cend(),
                                     [&](const Review &r) { return QString::number(r.id) == itemId; });
    if (review == w.data.reviews.cend())
        return;
    const Review r = *review;
    const bool loggedIn = !token(w.accountId).isEmpty();
    auto confirm = [ui](const QString &text, const QString &confirmText) {
        UiService::Confirmation c;
        c.title = QStringLiteral("Are you sure?");
        c.text = text;
        c.confirmText = confirmText;
        c.cancelText = QStringLiteral("Nevermind");
        return ui->confirm(c);
    };
    auto messageToast = [this](const QByteArray &body) {
        const QString message = QJsonDocument::fromJson(body).object().value(QStringLiteral("message")).toString();
        if (!message.isEmpty())
            toast(message);
    };
    std::weak_ptr<bool> weak = alive;

    if (menuId == QLatin1String("profile")) {
        ui->openUserProfile(w.accountId, 0, r.senderId.toULongLong());
    } else if (menuId == QLatin1String("up") || menuId == QLatin1String("down")) {
        if (!loggedIn) {
            toast(QStringLiteral("Please authorize to vote on reviews."));
            authorize(w.accountId);
            return;
        }
        const bool up = menuId == QLatin1String("up");
        // Voting the same way again takes the vote back
        const bool undo = r.userVote == up;
        const QString path = QStringLiteral("/reviews/%1/vote").arg(r.id);
        const QByteArray body = undo ? QByteArray()
                                     : QJsonDocument(QJsonObject{ { QStringLiteral("isUpvote"), up } }).toJson(QJsonDocument::Compact);
        request(w.accountId, undo ? "DELETE" : "POST", path, body, [this, weak, handle](bool ok, int, const QByteArray &body) {
            if (weak.expired())
                return;
            if (!ok) {
                const QString message = QJsonDocument::fromJson(body).object().value(QStringLiteral("message")).toString();
                toast(message.isEmpty() ? QStringLiteral("ReviewDB: Request failed") : message, UiService::ToastType::Failure);
            }
            reload(handle);
        });
    } else if (menuId == QLatin1String("report")) {
        if (!confirm(QStringLiteral("Do you really want to report this review?"), QStringLiteral("Report")))
            return;
        if (!loggedIn) {
            toast(QStringLiteral("You must be logged in to report reviews."));
            return;
        }
        request(w.accountId, "PUT", QStringLiteral("/reports"),
                QJsonDocument(QJsonObject{ { QStringLiteral("reviewid"), r.id } }).toJson(QJsonDocument::Compact),
                [messageToast](bool, int, const QByteArray &body) { messageToast(body); });
    } else if (menuId == QLatin1String("delete")) {
        if (!confirm(QStringLiteral("Do you really want to delete this review?"), QStringLiteral("Delete")))
            return;
        if (!loggedIn) {
            toast(QStringLiteral("You must be logged in to delete reviews."));
            return;
        }
        // Upstream sends the review's id in the path, as here
        request(w.accountId, "DELETE", QStringLiteral("/users/%1/reviews").arg(r.id),
                QJsonDocument(QJsonObject{ { QStringLiteral("reviewid"), r.id } }).toJson(QJsonDocument::Compact),
                [this, weak, handle, messageToast, account = w.accountId, target = w.targetId](bool ok, int, const QByteArray &body) {
                    if (weak.expired())
                        return;
                    messageToast(body);
                    if (ok) {
                        profileReviews.remove(key(account, target));
                        reload(handle);
                    }
                });
    } else if (menuId == QLatin1String("block")) {
        const bool unblock = me.value(w.accountId).blockedUsers.contains(r.senderId);
        if (!unblock &&
            !confirm(QStringLiteral("Do you really want to block this user? They will be unable to leave further reviews "
                                    "on your profile. You can unblock users in the plugin settings."),
                     QStringLiteral("Block")))
            return;
        if (!loggedIn) {
            toast(QStringLiteral("You must be logged in to block users."));
            return;
        }
        const QString action = unblock ? QStringLiteral("unblock") : QStringLiteral("block");
        request(w.accountId, "PATCH", QStringLiteral("/blocks"),
                QJsonDocument(QJsonObject{ { QStringLiteral("action"), action }, { QStringLiteral("discordId"), r.senderId } })
                        .toJson(QJsonDocument::Compact),
                [this, weak, handle, action, account = w.accountId, sender = r.senderId](bool ok, int, const QByteArray &) {
                    if (weak.expired() || !ok)
                        return;
                    toast(QStringLiteral("Successfully %1ed user").arg(action), UiService::ToastType::Success);
                    QStringList &list = me[account].blockedUsers;
                    if (action == QLatin1String("block"))
                        list.append(sender);
                    else
                        list.removeAll(sender);
                    render(handle);
                });
    }
}

void ReviewDBPlugin::submit(quint64 handle, const QString &text)
{
    const auto it = windows.constFind(handle);
    if (it == windows.constEnd())
        return;
    const Window w = *it;
    if (token(w.accountId).isEmpty()) {
        toast(QStringLiteral("Please authorize to add a review."));
        authorize(w.accountId);
        return;
    }
    std::weak_ptr<bool> weak = alive;
    const QByteArray body = QJsonDocument(QJsonObject{ { QStringLiteral("userid"), w.targetId },
                                                       { QStringLiteral("comment"), text } })
                                    .toJson(QJsonDocument::Compact);
    request(w.accountId, "PUT", QStringLiteral("/users/%1/reviews").arg(w.targetId), body,
            [this, weak, handle, account = w.accountId, target = w.targetId](bool ok, int, const QByteArray &body) {
                if (weak.expired())
                    return;
                const QString message = QJsonDocument::fromJson(body).object().value(QStringLiteral("message")).toString();
                if (!message.isEmpty())
                    toast(message, ok ? UiService::ToastType::Message : UiService::ToastType::Failure);
                if (ok) {
                    profileReviews.remove(key(account, target));
                    reload(handle);
                }
            });
}

// ------------------------------------------------------------------ login

void ReviewDBPlugin::authorize(Id accountId, std::function<void()> then)
{
    UiService *ui = context ? context->ui() : nullptr;
    if (!ui || !accountId)
        return;
    OAuthRequest request;
    request.clientId = ClientId;
    request.scopes = QStringList{ QStringLiteral("identify") };
    request.redirectUri = RedirectUri;
    request.responseType = QStringLiteral("code");
    std::weak_ptr<bool> weak = alive;
    ui->authorizeApp(request, [this, weak, accountId, then](std::optional<QString> location) {
        if (weak.expired() || !location)
            return;
        // ReviewDB trades the code in the redirect for its own token
        QUrl url(*location);
        QUrlQuery query(url);
        query.addQueryItem(QStringLiteral("clientMod"), QStringLiteral("qord"));
        url.setQuery(query);
        NetworkService *network = context->network();
        if (!network)
            return;
        network->request("GET", url, { { "Accept", "application/json" } }, {},
                         [this, weak, accountId, then](const NetworkService::Reply &reply) {
                             if (weak.expired())
                                 return;
                             const QJsonObject obj = QJsonDocument::fromJson(reply.body).object();
                             if (!reply.ok || obj.value(QStringLiteral("token")).toString().isEmpty()) {
                                 const QString message = obj.value(QStringLiteral("message")).toString();
                                 toast(message.isEmpty() ? QStringLiteral("An error occured while authorizing") : message,
                                       UiService::ToastType::Failure);
                                 return;
                             }
                             setToken(accountId, obj.value(QStringLiteral("token")).toString());
                             toast(QStringLiteral("Successfully logged in!"), UiService::ToastType::Success);
                             checkCurrentUser(accountId);
                             if (then)
                                 then();
                         });
    });
}

void ReviewDBPlugin::checkCurrentUser(Id accountId)
{
    if (!context || token(accountId).isEmpty())
        return;
    accountsSeen.insert(accountId);
    std::weak_ptr<bool> weak = alive;
    request(accountId, "POST", QStringLiteral("/users"), {}, [this, weak, accountId](bool ok, int, const QByteArray &body) {
        if (weak.expired() || !ok)
            return;
        const auto user = parseCurrentUser(body);
        if (!user)
            return;
        me.insert(accountId, *user);
        auto &settings = context->settings();
        // "You have new reviews on your profile!", once a newer review exists
        if (settings.boolValue(QStringLiteral("notifyReviews"))) {
            const qint64 last = qint64(settings.numberValue(QStringLiteral("lastReviewId")));
            if (last && last < user->lastReviewId && user->lastReviewId != 0)
                toast(QStringLiteral("You have new reviews on your profile!"));
            if (!last || last < user->lastReviewId)
                settings.setValue(QStringLiteral("lastReviewId"), double(user->lastReviewId));
        }
        // ReviewDB's notices (warnings, bans): shown once, then marked read
        if (user->notification) {
            if (UiService *ui = context->ui()) {
                UiService::Confirmation c;
                c.title = user->notification->title;
                c.text = user->notification->content;
                c.confirmText = QStringLiteral("OK");
                ui->confirm(c);
            }
            request(accountId, "PATCH", QStringLiteral("/notifications?id=%1").arg(user->notification->id), {},
                    [](bool, int, const QByteArray &) {});
        }
    });
}

void ReviewDBPlugin::manageBlocked(Id accountId)
{
    UiService *ui = context ? context->ui() : nullptr;
    if (!ui)
        return;
    if (token(accountId).isEmpty()) {
        toast(QStringLiteral("Please authorize to manage blocked users."));
        authorize(accountId);
        return;
    }
    auto handle = std::make_shared<quint64>(0);
    auto render = std::make_shared<std::function<void()>>();
    std::weak_ptr<bool> weak = alive;
    *render = [this, weak, ui, handle, accountId]() {
        request(accountId, "GET", QStringLiteral("/blocks"), {}, [weak, ui, handle](bool ok, int, const QByteArray &body) {
            if (weak.expired())
                return;
            ListDialogView view;
            view.title = QStringLiteral("Blocked Users");
            view.emptyText = ok ? QStringLiteral("No blocked users.") : QStringLiteral("Couldn't load blocked users.");
            for (const QJsonValue &v : QJsonDocument::fromJson(body).array()) {
                const QJsonObject u = v.toObject();
                ListDialogItem item;
                item.id = u.value(QStringLiteral("discordID")).toString();
                item.title = u.value(QStringLiteral("username")).toString();
                item.avatarUrl = u.value(QStringLiteral("profilePhoto")).toString();
                item.menu.append({ QStringLiteral("unblock"), QStringLiteral("Unblock") });
                view.items.append(item);
            }
            ui->updateListDialog(*handle, view);
        });
    };
    ListDialogEvents events;
    events.menuAction = [this, weak, render, accountId](const QString &userId, const QString &) {
        request(accountId, "PATCH", QStringLiteral("/blocks"),
                QJsonDocument(QJsonObject{ { QStringLiteral("action"), QStringLiteral("unblock") },
                                           { QStringLiteral("discordId"), userId } })
                        .toJson(QJsonDocument::Compact),
                [this, weak, render, accountId, userId](bool ok, int, const QByteArray &) {
                    if (weak.expired() || !ok)
                        return;
                    toast(QStringLiteral("Successfully unblocked user"), UiService::ToastType::Success);
                    me[accountId].blockedUsers.removeAll(userId);
                    (*render)();
                });
    };
    ListDialogView view;
    view.title = QStringLiteral("Blocked Users");
    view.emptyText = QStringLiteral("Loading…");
    *handle = ui->openListDialog(view, std::move(events));
    (*render)();
}

void ReviewDBPlugin::onSettingAction(const QString &key)
{
    UiService *ui = context ? context->ui() : nullptr;
    if (!ui)
        return;
    const Id account = defaultAccount();
    if (key == QLatin1String("authorize")) {
        if (!account) {
            toast(QStringLiteral("Open a profile or a menu with your account first."));
            return;
        }
        authorize(account);
    } else if (key == QLatin1String("manageBlocked")) {
        if (account)
            manageBlocked(account);
    } else if (key == QLatin1String("sponsor")) {
        ui->openUrl(QUrl(QStringLiteral("https://github.com/sponsors/mantikafasi")));
    } else if (key == QLatin1String("website")) {
        QString url = QStringLiteral("https://reviewdb.mantikafasi.dev");
        const QString t = token(account);
        if (!t.isEmpty())
            url += QStringLiteral("/api/redirect?token=") + QString::fromUtf8(QUrl::toPercentEncoding(t));
        ui->openUrl(QUrl(url));
    } else if (key == QLatin1String("supportServer")) {
        ui->openUrl(QUrl(QStringLiteral("https://discord.gg/eWPBSbvznt")));
    }
}

QByteArray qord_manifest_reviewdb();

void qord_register_reviewdb(Registry &registry)
{
    registry.add({ QStringLiteral("reviewdb/plugin.json"), [] { return std::make_unique<ReviewDBPlugin>(); },
                   qord_manifest_reviewdb() });
}

} // namespace Plugins
} // namespace Qord
