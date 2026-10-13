#include "PluginTestUtils.hpp"

#include "MessageLinkEmbeds.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_messagelinkembeds(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

namespace {
constexpr Id General = 400000000000000005, Dm = 420000000000000001, Hidden = 400000000000000099;
constexpr Id Linked = 800000000000000001, Missing = 800000000000000002, InDm = 800000000000000003;

QString link(Id channel, Id message, const QString &guild = QStringLiteral("300000000000000001"))
{
    return QStringLiteral("https://discord.com/channels/%1/%2/%3").arg(guild).arg(channel).arg(message);
}

MessageContext withLinks(const QString &content)
{
    MessageContext m;
    m.accountId = 1;
    m.channelId = 9;
    m.messageId = 77;
    m.authorId = 200000000000000002;
    m.content = content;
    return m;
}

void fill(SinglePluginHost &h)
{
    h.messageLookup.channels.insert(General, { General, 300000000000000001, 0, QStringLiteral("general"), 0,
                                               QStringLiteral("https://cdn/icon.png"), true });
    h.messageLookup.channels.insert(Dm, { Dm, 0, 1, QStringLiteral("Ava"), 7, QStringLiteral("https://cdn/ava.png"), true });
    h.messageLookup.channels.insert(Hidden, { Hidden, 300000000000000001, 0, QStringLiteral("staff"), 0, {}, false });
    LinkedMessage linked;
    linked.channelId = General;
    linked.messageId = Linked;
    linked.authorName = QStringLiteral("Ben");
    linked.content = QStringLiteral("hey! did anyone try the new build?");
    linked.timestamp = QDateTime::fromSecsSinceEpoch(1700000000);
    h.messageLookup.messages.insert(Linked, linked);
    LinkedMessage pic;
    pic.channelId = Dm;
    pic.messageId = InDm;
    pic.attachmentCount = 2;
    pic.images = { { QStringLiteral("https://cdn/a.png"), 480, 270 } };
    h.messageLookup.messages.insert(InDm, pic);
}
} // namespace

class TestMessageLinkEmbeds : public QObject
{
    Q_OBJECT

private slots:
    void linksLikeUpstream()
    {
        const auto found = MessageLinkEmbedsPlugin::links(
                QStringLiteral("see %1 and <%2> and %3").arg(link(1, 2), link(3, 4), link(General, Linked)));
        QCOMPARE(found.size(), 1); // ids must be 17 to 20 digits, and <links> don't embed
        QCOMPARE(found[0].messageId, Linked);
        QCOMPARE(MessageLinkEmbedsPlugin::links(QStringLiteral("https://ptb.discord.com/channels/@me/%1/%2").arg(Dm).arg(InDm)).size(), 1);
        QCOMPARE(MessageLinkEmbedsPlugin::noContent(2, 0), QStringLiteral("[no content, 2 attachments]"));
        QCOMPARE(MessageLinkEmbedsPlugin::noContent(1, 1), QStringLiteral("[no content, 1 attachment and 1 embed]"));
        QCOMPARE(MessageLinkEmbedsPlugin::noContent(0, 0), QString());
    }

    void previews()
    {
        SinglePluginHost h(qord_register_messagelinkembeds);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        fill(h);

        QVERIFY(!h.host->dispatchMessageAccessories(withLinks(QStringLiteral("no links"))).size());
        const auto entries = h.host->dispatchMessageAccessories(
                withLinks(link(General, Linked) + QStringLiteral(" ") + link(Dm, InDm, QStringLiteral("@me")) + " " +
                          link(Hidden, 800000000000000009) + " " + link(General, Linked)));
        QCOMPARE(entries.size(), 1);
        const auto &embeds = entries[0].accessory.embeds;
        QCOMPARE(embeds.size(), 2); // the hidden channel is skipped, the repeat too
        QCOMPARE(embeds[0].authorName, QStringLiteral("Server - #general"));
        QCOMPARE(embeds[0].authorIconUrl, QStringLiteral("https://cdn/icon.png"));
        QCOMPARE(embeds[0].description, QStringLiteral("hey! did anyone try the new build?"));
        QCOMPARE(embeds[0].footerText, QStringLiteral("Ben"));
        QCOMPARE(embeds[1].authorName, QStringLiteral("Direct Message - @Ava"));
        QCOMPARE(embeds[1].description, QStringLiteral("[no content, 2 attachments]"));
        QCOMPARE(embeds[1].images.size(), 1);
        QVERIFY(h.messageLookup.fetches.isEmpty());
    }

    void fetchesMissingOnce()
    {
        SinglePluginHost h(qord_register_messagelinkembeds);
        QVERIFY(h.enableWithAllPermissions());
        fill(h);
        const MessageContext message = withLinks(link(General, Missing) + " " + link(General, 800000000000000004));
        QVERIFY(h.host->dispatchMessageAccessories(message).isEmpty());
        // One at a time, newest first
        QCOMPARE(h.messageLookup.fetches.size(), 1);
        QCOMPARE(h.messageLookup.fetches[0].second, Missing);
        h.host->dispatchMessageAccessories(message);
        QCOMPARE(h.messageLookup.fetches.size(), 1); // no second request for the same message

        LinkedMessage arrived;
        arrived.channelId = General;
        arrived.messageId = Missing;
        arrived.content = QStringLiteral("here");
        h.messageLookup.messages.insert(Missing, arrived);
        h.messageLookup.pending.takeFirst()(true);
        QCOMPARE(h.messages.refreshed, QList<Id> { 77 });
        QCOMPARE(h.messageLookup.fetches.size(), 2);
        QCOMPARE(h.messageLookup.fetches[1].second, Id(800000000000000004));
        h.messageLookup.pending.takeFirst()(false);
        QCOMPARE(h.messages.refreshed.size(), 1);
        QCOMPARE(h.host->dispatchMessageAccessories(message)[0].accessory.embeds.size(), 1);
    }

    void idList()
    {
        SinglePluginHost h(qord_register_messagelinkembeds);
        QVERIFY(h.enableWithAllPermissions());
        fill(h);
        const MessageContext message = withLinks(link(General, Linked) + " " + link(Dm, InDm, QStringLiteral("@me")));
        auto *settings = h.host->settings(h.id);
        settings->setValue(QStringLiteral("idList"), QString::number(General));
        QCOMPARE(h.host->dispatchMessageAccessories(message)[0].accessory.embeds.size(), 1);
        QCOMPARE(h.host->dispatchMessageAccessories(message)[0].accessory.embeds[0].authorName,
                 QStringLiteral("Direct Message - @Ava"));
        settings->setValue(QStringLiteral("listMode"), QStringLiteral("whitelist"));
        QCOMPARE(h.host->dispatchMessageAccessories(message)[0].accessory.embeds[0].authorName,
                 QStringLiteral("Server - #general"));
        // The author of the message with the links counts too
        settings->setValue(QStringLiteral("idList"), QStringLiteral("200000000000000002"));
        QCOMPARE(h.host->dispatchMessageAccessories(message)[0].accessory.embeds.size(), 2);
    }
};

QTEST_GUILESS_MAIN(TestMessageLinkEmbeds)
#include "tst_messagelinkembeds.moc"
