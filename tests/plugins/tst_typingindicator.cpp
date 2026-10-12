#include "PluginTestUtils.hpp"

#include "TypingIndicator.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_typingindicator(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

namespace {
constexpr Id Self = 1;

MemberInfo member(Id id, const QString &username, const QString &globalName, const QString &nick = {})
{
    MemberInfo m;
    m.userId = id;
    m.username = username;
    m.globalName = globalName;
    m.nick = nick;
    m.isMember = true;
    return m;
}

void fill(SinglePluginHost &h)
{
    h.guilds.members.insert(7, member(7, QStringLiteral("ava"), QStringLiteral("Ava")));
    h.guilds.members.insert(8, member(8, QStringLiteral("benji"), QStringLiteral("Ben"), QStringLiteral("Benny")));
    h.guilds.members.insert(9, member(9, QStringLiteral("cleo.dev"), QString()));
    h.guilds.members.insert(10, member(10, QStringLiteral("dan"), QStringLiteral("Dan")));
    h.relationships.blocked = { 20 };
    h.relationships.ignored = { 21 };
}

ChannelTypingContext channel(QList<Id> typers, bool selected = false, bool muted = false)
{
    ChannelTypingContext c;
    c.accountId = Self;
    c.guildId = 100;
    c.channelId = 5;
    c.selected = selected;
    c.muted = muted;
    c.typers = std::move(typers);
    return c;
}
} // namespace

class TestTypingIndicator : public QObject
{
    Q_OBJECT

private slots:
    void tooltipsLikeDiscord()
    {
        QCOMPARE(TypingIndicatorPlugin::tooltip({ "Ava" }), QStringLiteral("Ava is typing..."));
        QCOMPARE(TypingIndicatorPlugin::tooltip({ "Ava", "Ben" }), QStringLiteral("Ava and Ben are typing..."));
        QCOMPARE(TypingIndicatorPlugin::tooltip({ "Ava", "Ben", "Cleo" }),
                 QStringLiteral("Ava, Ben, and Cleo are typing..."));
        QCOMPARE(TypingIndicatorPlugin::tooltip({ "a", "b", "c", "d" }), QStringLiteral("Several people are typing..."));
    }

    void namesAndAvatars()
    {
        SinglePluginHost h(qord_register_typingindicator);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        fill(h);

        QVERIFY(!h.host->dispatchChannelTypingIndicator(channel({})).has_value());
        // Nickname, then display name, then username
        auto indicator = h.host->dispatchChannelTypingIndicator(channel({ 8, 7, 9 }));
        QVERIFY(indicator.has_value());
        QCOMPARE(indicator->tooltip, QStringLiteral("Benny, Ava, and cleo.dev are typing..."));
        QCOMPARE(indicator->avatars, (QList<Id> { 8, 7, 9 }));
        QVERIFY(indicator->dots);
        indicator = h.host->dispatchChannelTypingIndicator(channel({ 7, 8, 9, 10 }));
        QCOMPARE(indicator->tooltip, QStringLiteral("Several people are typing..."));
        QCOMPARE(indicator->avatars.size(), 4);
    }

    void filters()
    {
        SinglePluginHost h(qord_register_typingindicator);
        QVERIFY(h.enableWithAllPermissions());
        fill(h);
        // You, blocked and ignored users don't count
        QVERIFY(!h.host->dispatchChannelTypingIndicator(channel({ Self, 20, 21 })).has_value());
        QCOMPARE(h.host->dispatchChannelTypingIndicator(channel({ 20, 7 }))->tooltip, QStringLiteral("Ava is typing..."));
        // Muted channels are skipped by default; the open channel is shown
        QVERIFY(!h.host->dispatchChannelTypingIndicator(channel({ 7 }, false, true)).has_value());
        QVERIFY(h.host->dispatchChannelTypingIndicator(channel({ 7 }, true)).has_value());

        auto *settings = h.host->settings(h.id);
        settings->setValue(QStringLiteral("includeBlockedUsers"), true);
        settings->setValue(QStringLiteral("includeIgnoredUsers"), true);
        settings->setValue(QStringLiteral("includeMutedChannels"), true);
        settings->setValue(QStringLiteral("includeCurrentChannel"), false);
        QCOMPARE(h.host->dispatchChannelTypingIndicator(channel({ 20, 21 }, false, true))->avatars, (QList<Id> { 20, 21 }));
        QVERIFY(!h.host->dispatchChannelTypingIndicator(channel({ 7 }, true)).has_value());
    }

    void modes()
    {
        SinglePluginHost h(qord_register_typingindicator);
        QVERIFY(h.enableWithAllPermissions());
        fill(h);
        h.host->settings(h.id)->setValue(QStringLiteral("indicatorMode"), QStringLiteral("dots"));
        auto indicator = h.host->dispatchChannelTypingIndicator(channel({ 7 }));
        QVERIFY(indicator->avatars.isEmpty());
        QVERIFY(indicator->dots);
        h.host->settings(h.id)->setValue(QStringLiteral("indicatorMode"), QStringLiteral("avatars"));
        indicator = h.host->dispatchChannelTypingIndicator(channel({ 7 }));
        QCOMPARE(indicator->avatars, QList<Id> { 7 });
        QVERIFY(!indicator->dots);
    }
};

QTEST_GUILESS_MAIN(TestTypingIndicator)
#include "tst_typingindicator.moc"
