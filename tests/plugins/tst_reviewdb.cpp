#include "ReviewDB.hpp"
#include "PluginTestUtils.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QUrlQuery>

namespace Qord::Plugins {
void qord_register_reviewdb(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

// JSON written with ' for " (readable here, and moc can't read raw strings)
static QByteArray json(const char *text)
{
    return QByteArray(text).replace('\'', '"');
}

static QByteArray reviewsJson(int count = 2, bool optedOut = false)
{
    return QString::fromUtf8(json("{'reviews':[ {'id':5,'comment':'great person','score':3,'star':0,'timestamp':1760000000,'type':0, 'sender':{'id':1,'discordID':'300','username':'ben','profilePhoto':'https://cdn.discordapp.com/avatars/300/a.png', 'badges':[{'name':'Donor'}]}}, {'id':0,'comment':'Be respectful','score':0,'star':0,'timestamp':0,'type':3, 'sender':{'id':0,'discordID':'1134864775000629298','username':'ReviewDB','profilePhoto':'','badges':[]}}, {'id':6,'comment':'meh','score':-1,'star':0,'timestamp':1760000100,'type':0, 'sender':{'id':2,'discordID':'100','username':'me','profilePhoto':'','badges':[]}} ],'hasNextPage':false,'reviewCount':%1,'hasOptedOut':%2}"))
            .arg(count)
            .arg(optedOut ? QStringLiteral("true") : QStringLiteral("false"))
            .toUtf8();
}


class TestReviewDB : public QObject
{
    Q_OBJECT

    static constexpr Id Me = 100;
    static constexpr Id Ava = 200;

    static UserContext user(Id userId = Ava)
    {
        UserContext u;
        u.accountId = Me;
        u.userId = userId;
        u.username = QStringLiteral("ava");
        return u;
    }

    static void enable(SinglePluginHost &h)
    {
        QVERIFY(h.enableWithAllPermissions());
        h.host->settings(h.id)->setValue(QStringLiteral("tokens"), QString()); // the store outlives a test
    }

    static void login(SinglePluginHost &h)
    {
        h.host->settings(h.id)->setValue(QStringLiteral("tokens"), QString::fromUtf8(json("{'100':'rdb-token'}")));
    }

    // The newest request() call
    static FakeNetwork::Call last(SinglePluginHost &h) { return h.network.calls.last(); }

private slots:
    void parses()
    {
        const auto data = ReviewDBPlugin::parseReviews(reviewsJson(3));
        QCOMPARE(data.reviews.size(), 3);
        QCOMPARE(data.reviewCount, 3);
        QCOMPARE(data.reviews[0].senderName, QStringLiteral("ben"));
        QCOMPARE(data.reviews[0].badges, QStringList{ QStringLiteral("Donor") });
        QCOMPARE(data.reviews[1].type, ReviewDBPlugin::ReviewType::System);
        QCOMPARE(data.reviews[2].score, -1);
        const auto me = ReviewDBPlugin::parseCurrentUser(
                json("{'discordID':'100','type':1,'lastReviewID':42,'blockedUsers':['300'],'notification':{'id':7,'title':'Hi','content':'Notice','type':0}}"));
        QVERIFY(me);
        QCOMPARE(me->type, ReviewDBPlugin::UserType::Admin);
        QCOMPARE(me->lastReviewId, 42);
        QCOMPARE(me->blockedUsers, QStringList{ QStringLiteral("300") });
        QCOMPARE(me->notification->title, QStringLiteral("Hi"));
        QVERIFY(!ReviewDBPlugin::parseCurrentUser("nope"));
    }

    void profileTabLoadsThenLists()
    {
        SinglePluginHost h(&qord_register_reviewdb);
        enable(h);
        QSignalSpy refresh(h.host.get(), &PluginHost::profileTabsChanged);
        auto tabs = h.host->dispatchProfileTabs(user());
        QCOMPARE(tabs.size(), 1);
        QCOMPARE(tabs[0].tab.emptyText, QStringLiteral("Loading…"));
        QCOMPARE(last(h).method, QByteArray("GET"));
        QCOMPARE(last(h).url.path(), QStringLiteral("/api/reviewdb/users/200/reviews"));
        QCOMPARE(QUrlQuery(last(h).url).queryItemValue(QStringLiteral("limit")), QStringLiteral("50"));
        QVERIFY(h.network.respond(true, reviewsJson(2)));
        QCOMPARE(refresh.size(), 1);
        tabs = h.host->dispatchProfileTabs(user());
        QCOMPARE(tabs[0].tab.title, QStringLiteral("User Reviews (2)"));
        // The system message isn't a review
        QCOMPARE(tabs[0].tab.rows.size(), 2);
        QCOMPARE(tabs[0].tab.rows[0].title, QStringLiteral("ben"));
        QCOMPARE(tabs[0].tab.rows[0].subtitle, QStringLiteral("great person"));
        QCOMPARE(h.network.calls.size(), 1); // cached

        // Opted out
        h.host->dispatchProfileTabs(user(201));
        QVERIFY(h.network.respond(true, json("{'reviews':[],'reviewCount':0,'hasOptedOut':true}")));
        QCOMPARE(h.host->dispatchProfileTabs(user(201))[0].tab.emptyText, QStringLiteral("User opted out"));

        // A row opens the reviews window
        h.host->dispatchProfileTabRowClicked(h.id, tabs[0].tab.id, tabs[0].tab.rows[0].id, user());
        QCOMPARE(h.ui.listDialogs.size(), 1);
        QCOMPARE(h.ui.listDialogs.begin()->title, QStringLiteral("ava's Reviews"));
    }

    void hidesBlockedAndWarning()
    {
        SinglePluginHost h(&qord_register_reviewdb);
        enable(h);
        h.relationships.blocked = { 300 };
        h.host->settings(h.id)->setValue(QStringLiteral("showWarning"), false);
        h.host->dispatchProfileTabs(user());
        QCOMPARE(QUrlQuery(last(h).url).queryItemValue(QStringLiteral("flags")), QStringLiteral("2"));
        QVERIFY(h.network.respond(true, reviewsJson(2)));
        const auto rows = h.host->dispatchProfileTabs(user())[0].tab.rows;
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows[0].title, QStringLiteral("me"));
    }

    void windowWithoutLogin()
    {
        SinglePluginHost h(&qord_register_reviewdb);
        enable(h);
        const auto items = h.host->dispatchUserContextMenuItems(user());
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].item.label, QStringLiteral("View Reviews"));
        h.host->dispatchUserContextMenuAction(h.id, items[0].item.id, user());
        QVERIFY(h.network.respond(true, reviewsJson(3)));
        QCOMPARE(h.ui.listDialogs.size(), 1);
        const quint64 handle = h.ui.listDialogs.begin().key();
        const ListDialogView view = h.ui.listDialogs.value(handle);
        QCOMPARE(view.title, QStringLiteral("ava's Reviews (3 Reviews)"));
        // System messages first
        QCOMPARE(view.items[0].title, QStringLiteral("ReviewDB"));
        QCOMPARE(view.items[0].badge, QStringLiteral("System"));
        QVERIFY(view.items[0].menu.isEmpty());
        QCOMPARE(view.items[1].badge, QStringLiteral("Donor"));
        QVERIFY(view.items[1].meta.startsWith(QStringLiteral("+3 · ")));
        QCOMPARE(view.inputPlaceholder, QStringLiteral("You need to authorize to review users!"));
        QVERIFY(!view.inputEnabled);
        // Without a login there are no votes to fetch
        QCOMPARE(h.network.calls.size(), 1);

        // Clicking the input starts the login
        h.ui.dialogEvents.value(handle).inputBlocked();
        QCOMPARE(h.ui.authorizations.size(), 1);
        QCOMPARE(h.ui.authorizations[0].clientId, QStringLiteral("915703782174752809"));
        QCOMPARE(h.ui.authorizations[0].scopes, QStringList{ QStringLiteral("identify") });
        QCOMPARE(h.ui.authorizations[0].redirectUri, QStringLiteral("https://manti.vendicated.dev/api/reviewdb/auth"));
    }

    void authorizeTradesTheCode()
    {
        SinglePluginHost h(&qord_register_reviewdb);
        enable(h);
        h.host->dispatchUserContextMenuItems(user()); // the account is known from here on
        h.host->dispatchSettingAction(h.id, QStringLiteral("authorize"));
        QCOMPARE(h.ui.authorizations.size(), 1);
        h.ui.pendingAuthorization(QStringLiteral("https://manti.vendicated.dev/api/reviewdb/auth?code=abc"));
        const QUrl url = last(h).url;
        QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("code")), QStringLiteral("abc"));
        QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("clientMod")), QStringLiteral("qord"));
        QVERIFY(h.network.respond(true, json("{'token':'fresh'}")));
        QCOMPARE(h.ui.toasts.last().first, QStringLiteral("Successfully logged in!"));
        // Kept per account, and used for who-am-I right away
        QCOMPARE(QJsonDocument::fromJson(h.host->settings(h.id)->stringValue(QStringLiteral("tokens")).toUtf8())
                         .object()
                         .value(QStringLiteral("100"))
                         .toString(),
                 QStringLiteral("fresh"));
        QCOMPARE(last(h).method, QByteArray("POST"));
        QCOMPARE(last(h).url.path(), QStringLiteral("/api/reviewdb/users"));
        QVERIFY(last(h).headers.contains({ "Authorization", "fresh" }));

        // A cancelled authorization does nothing
        h.host->dispatchSettingAction(h.id, QStringLiteral("authorize"));
        const int calls = h.network.calls.size();
        h.ui.pendingAuthorization(std::nullopt);
        QCOMPARE(h.network.calls.size(), calls);
    }

    void loggedInWindowActions()
    {
        SinglePluginHost h(&qord_register_reviewdb);
        enable(h);
        login(h);
        h.host->dispatchUserContextMenuAction(h.id, QStringLiteral("vc-rdb-user-reviews"), user());
        QVERIFY(h.network.respond(true, reviewsJson(3)));
        QCOMPARE(last(h).url.path(), QStringLiteral("/api/reviewdb/users/200/reviews/votes"));
        QVERIFY(h.network.respond(true, json("{'votes':[{'reviewID':5,'isUpvote':true}]}")));
        const quint64 handle = h.ui.listDialogs.begin().key();
        ListDialogView view = h.ui.listDialogs.value(handle);
        QVERIFY(view.inputEnabled);
        QCOMPARE(view.inputPlaceholder, QStringLiteral("Update review for @ava")); // "me" reviewed already
        const auto menuIds = [](const ListDialogItem &item) {
            QStringList ids;
            for (const auto &m : item.menu)
                ids.append(m.id + QLatin1Char(':') + m.label);
            return ids;
        };
        // Someone else's review: vote (already up), report; not delete on another's profile
        QCOMPARE(menuIds(view.items[1]), (QStringList{ "profile:View Profile", "up:Remove Upvote", "down:Downvote",
                                                       "report:Report Review" }));
        // Your own: delete only
        QCOMPARE(menuIds(view.items[2]), (QStringList{ "profile:View Profile", "delete:Delete Review" }));

        // Taking the upvote back
        auto events = h.ui.dialogEvents.value(handle);
        events.menuAction(QStringLiteral("5"), QStringLiteral("up"));
        QCOMPARE(last(h).method, QByteArray("DELETE"));
        QCOMPARE(last(h).url.path(), QStringLiteral("/api/reviewdb/reviews/5/vote"));
        QVERIFY(h.network.respond(true, "{}"));
        // ...reloads the list
        QCOMPARE(last(h).url.path(), QStringLiteral("/api/reviewdb/users/200/reviews"));
        QVERIFY(h.network.respond(true, reviewsJson(3)));
        QVERIFY(h.network.respond(true, json("{'votes':[]}")));
        events.menuAction(QStringLiteral("5"), QStringLiteral("down"));
        QCOMPARE(last(h).method, QByteArray("POST"));
        QCOMPARE(QJsonDocument::fromJson(last(h).body).object().value(QStringLiteral("isUpvote")).toBool(), false);
        QVERIFY(h.network.respond(true, "{}"));
        QVERIFY(h.network.respond(true, reviewsJson(3)));
        QVERIFY(h.network.respond(true, json("{'votes':[]}")));

        // Report, after the confirmation
        events.menuAction(QStringLiteral("5"), QStringLiteral("report"));
        QCOMPARE(h.ui.confirmations.last().text, QStringLiteral("Do you really want to report this review?"));
        QCOMPARE(last(h).method, QByteArray("PUT"));
        QCOMPARE(last(h).url.path(), QStringLiteral("/api/reviewdb/reports"));
        QVERIFY(h.network.respond(true, json("{'message':'Successfully reported review'}")));
        QCOMPARE(h.ui.toasts.last().first, QStringLiteral("Successfully reported review"));

        // Deleting needs a yes
        h.ui.confirmAnswer = false;
        const int before = h.network.calls.size();
        events.menuAction(QStringLiteral("6"), QStringLiteral("delete"));
        QCOMPARE(h.network.calls.size(), before);
        h.ui.confirmAnswer = true;
        events.menuAction(QStringLiteral("6"), QStringLiteral("delete"));
        QCOMPARE(last(h).method, QByteArray("DELETE"));
        QCOMPARE(last(h).url.path(), QStringLiteral("/api/reviewdb/users/6/reviews"));

        // Writing
        QVERIFY(h.network.respond(true, json("{'message':'Successfully deleted review'}")));
        QVERIFY(h.network.respond(true, reviewsJson(2)));
        QVERIFY(h.network.respond(true, json("{'votes':[]}")));
        events.submitted(QStringLiteral("lovely"));
        QCOMPARE(last(h).method, QByteArray("PUT"));
        QCOMPARE(last(h).url.path(), QStringLiteral("/api/reviewdb/users/200/reviews"));
        QCOMPARE(QJsonDocument::fromJson(last(h).body).object().value(QStringLiteral("comment")).toString(),
                 QStringLiteral("lovely"));

        // View Profile
        events.menuAction(QStringLiteral("5"), QStringLiteral("profile"));
        QCOMPARE(h.ui.profilesOpened.last(), qMakePair(Me, Id(300)));
    }

    void ownProfileCanBlock()
    {
        SinglePluginHost h(&qord_register_reviewdb);
        enable(h);
        login(h);
        h.host->dispatchUserContextMenuAction(h.id, QStringLiteral("vc-rdb-user-reviews"), user(Me));
        QVERIFY(h.network.respond(true, reviewsJson(3)));
        QVERIFY(h.network.respond(true, json("{'votes':[]}")));
        const quint64 handle = h.ui.listDialogs.begin().key();
        ListDialogView view = h.ui.listDialogs.value(handle);
        QCOMPARE(view.items[1].menu.last().label, QStringLiteral("Delete Review")); // your profile: any review
        QVERIFY(std::any_of(view.items[1].menu.cbegin(), view.items[1].menu.cend(),
                            [](const MenuItem &m) { return m.label == QLatin1String("Block User"); }));
        h.ui.dialogEvents.value(handle).menuAction(QStringLiteral("5"), QStringLiteral("block"));
        QCOMPARE(last(h).method, QByteArray("PATCH"));
        QCOMPARE(QJsonDocument::fromJson(last(h).body).object().value(QStringLiteral("discordId")).toString(),
                 QStringLiteral("300"));
        QVERIFY(h.network.respond(true, "{}"));
        QCOMPARE(h.ui.toasts.last().first, QStringLiteral("Successfully blocked user"));
        view = h.ui.listDialogs.value(handle);
        QVERIFY(view.items[1].badge.contains(QStringLiteral("You have blocked this user")));
    }

    void serverReviews()
    {
        SinglePluginHost h(&qord_register_reviewdb);
        enable(h);
        ChannelContext server;
        server.accountId = Me;
        server.guildId = 400;
        server.name = QStringLiteral("Qord Dev");
        const auto items = h.host->dispatchChannelContextMenuItems(server);
        QCOMPARE(items.size(), 1);
        h.host->dispatchChannelContextMenuAction(h.id, items[0].item.id, server);
        QCOMPARE(last(h).url.path(), QStringLiteral("/api/reviewdb/users/400/reviews"));
        QVERIFY(h.network.respond(true, json("{'reviews':[],'reviewCount':0}")));
        const ListDialogView view = h.ui.listDialogs.begin().value();
        QCOMPARE(view.emptyText, QStringLiteral("Looks like nobody reviewed this server yet. You could be the first!"));
        // Not for channels
        server.channelId = 401;
        QVERIFY(h.host->dispatchChannelContextMenuItems(server).isEmpty());
    }

    void failuresShowUpstreamsMessage()
    {
        SinglePluginHost h(&qord_register_reviewdb);
        enable(h);
        h.host->dispatchUserContextMenuAction(h.id, QStringLiteral("vc-rdb-user-reviews"), user());
        QVERIFY(h.network.respond(false, {}, 429));
        QCOMPARE(h.ui.toasts.last().first,
                 QStringLiteral("You are sending requests too fast. Wait a few seconds and try again."));
        const ListDialogView view = h.ui.listDialogs.begin().value();
        QCOMPARE(view.items.size(), 1);
        QCOMPARE(view.items[0].title, QStringLiteral("ReviewDB"));
    }

    void newReviewsAndNotices()
    {
        SinglePluginHost h(&qord_register_reviewdb);
        enable(h);
        login(h);
        h.host->settings(h.id)->setValue(QStringLiteral("lastReviewId"), 40);
        QTest::qWait(ReviewDBPlugin::StartupDelayMs + 200);
        QCOMPARE(last(h).url.path(), QStringLiteral("/api/reviewdb/users"));
        QVERIFY(h.network.respond(true,
                                  json("{'discordID':'100','type':0,'lastReviewID':42,'notification':{'id':7,'title':'Warning','content':'Be nice','type':3}}")));
        QCOMPARE(h.ui.toasts.last().first, QStringLiteral("You have new reviews on your profile!"));
        QCOMPARE(h.host->settings(h.id)->numberValue(QStringLiteral("lastReviewId")), 42.0);
        QCOMPARE(h.ui.confirmations.last().title, QStringLiteral("Warning"));
        QCOMPARE(last(h).method, QByteArray("PATCH"));
        QCOMPARE(last(h).url.toString(QUrl::RemoveScheme | QUrl::RemoveAuthority),
                 QStringLiteral("/api/reviewdb/notifications?id=7"));
    }

    void stoppingClosesWindows()
    {
        SinglePluginHost h(&qord_register_reviewdb);
        enable(h);
        h.host->dispatchUserContextMenuAction(h.id, QStringLiteral("vc-rdb-user-reviews"), user());
        QCOMPARE(h.ui.listDialogs.size(), 1);
        QVERIFY(h.host->setEnabled(h.id, false));
        QVERIFY(h.ui.listDialogs.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestReviewDB)
#include "tst_reviewdb.moc"
