#include "PluginTestUtils.hpp"

#include "ShowMeYourName.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

namespace Qord::Plugins {
void qord_register_showmeyourname(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

static QJsonArray golden()
{
    QFile f(QStringLiteral(QORD_TEST_DATA_DIR "/showmeyourname-golden.json"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).array();
}

static QStringList strings(const QJsonValue &v)
{
    QStringList out;
    for (const QJsonValue &s : v.toArray())
        out.append(s.toString());
    return out;
}

class TestShowMeYourName : public QObject
{
    Q_OBJECT

private slots:
    // Upstream's template functions run under Node (tests/plugins/data/generate-showmeyourname-golden.mjs)
    void templatesMatchUpstream()
    {
        const QJsonArray cases = golden();
        QVERIFY(cases.size() > 10);
        for (const QJsonValue &v : cases) {
            const QJsonObject c = v.toObject();
            const QString templ = c.value("template").toString();
            QCOMPARE(ShowMeYourName::splitTemplate(templ), strings(c.value("split")));
            const QJsonArray items = c.value("items").toArray();
            const QStringList split = ShowMeYourName::splitTemplate(templ);
            for (int i = 0; i < split.size(); ++i) {
                const auto item = ShowMeYourName::parseTemplateItem(split[i]);
                const QJsonObject want = items[i].toObject();
                QCOMPARE(item.prefix, want.value("prefix").toString());
                QCOMPARE(item.suffix, want.value("suffix").toString());
                QCOMPARE(item.names, strings(want.value("targetProcessedNames")));
            }
            QCOMPARE(ShowMeYourName::validTemplate(templ), c.value("valid").toBool());
        }
    }

    void composeMatchesUpstream()
    {
        for (const QJsonValue &v : golden()) {
            const QJsonObject c = v.toObject();
            for (const QJsonValue &pv : c.value("composed").toArray()) {
                const QJsonObject p = pv.toObject();
                const QJsonObject n = p.value("names").toObject();
                ShowMeYourName::Names names;
                names.username = n.value("user").toString();
                names.display = n.value("display").toString();
                names.nick = n.value("nick").toString();
                names.friendName = n.value("friend").toString();
                names.custom = n.value("custom").toString();
                ShowMeYourName::Options o;
                o.includedNames = c.value("template").toString();
                o.nameSeparator = " ";
                o.removeDuplicates = true;
                QCOMPARE(ShowMeYourName::compose(names, o), p.value("dedupe").toString());
                o.nameSeparator = " | ";
                o.removeDuplicates = false;
                QCOMPARE(ShowMeYourName::compose(names, o), p.value("noDedupe").toString());
            }
        }
    }

    void processedNames()
    {
        AuthorContext a;
        a.username = "Qordbot";
        a.globalName = "Qord Bot";
        a.isBot = true;
        a.discriminator = "1234";
        a.guildId = 5;
        a.friendNickname = "buddy";
        ShowMeYourName::Options o;
        auto n = ShowMeYourName::processNames(a, "custom", o);
        QCOMPARE(n.username, QStringLiteral("Qordbot#1234"));
        QCOMPARE(n.display, QStringLiteral("Qordbot")); // upstream replaces such bots' display name
        QCOMPARE(n.friendName, QStringLiteral("buddy"));
        o.discriminators = false;
        QCOMPARE(ShowMeYourName::processNames(a, {}, o).username, QStringLiteral("Qordbot"));
        o.friendNameOnlyInDirectMessages = true;
        o.customNameOnlyInDirectMessages = true;
        n = ShowMeYourName::processNames(a, "custom", o);
        QVERIFY(n.friendName.isEmpty());
        QVERIFY(n.custom.isEmpty());
        a.guildId = 0; // in a DM they show
        n = ShowMeYourName::processNames(a, "custom", o);
        QCOMPARE(n.custom, QStringLiteral("custom"));
    }

    void pluginNamesAndNicknames()
    {
        SinglePluginHost h(qord_register_showmeyourname);
        QVERIFY(h.info()->loadError.isEmpty());
        AuthorContext a;
        a.accountId = 1;
        a.userId = 7;
        a.guildId = 5;
        a.username = "benji";
        a.globalName = "Ben";
        a.nick = "Benny";
        QVERIFY(!h.host->dispatchAuthorDisplayName(a));
        QVERIFY(h.enableWithAllPermissions());
        QCOMPARE(*h.host->dispatchAuthorDisplayName(a), QStringLiteral("Benny [Ben] (@benji)"));

        // Replies follow their own switch
        a.place = AuthorContext::Place::Reply;
        QVERIFY(h.host->settings(h.id)->setValue("replies", false).isEmpty());
        QVERIFY(!h.host->dispatchAuthorDisplayName(a));
        a.place = AuthorContext::Place::Message;

        // Custom nickname from the user menu
        UserContext u;
        u.accountId = 1;
        u.userId = 7;
        auto items = h.host->dispatchUserContextMenuItems(u);
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].item.label, QStringLiteral("Add SMYN Nickname"));
        h.host->dispatchUserContextMenuAction(h.id, items[0].item.id, u);
        QVERIFY(h.ui.pendingPrompt);
        h.ui.pendingPrompt("  The Benster  ");
        QCOMPARE(*h.host->dispatchAuthorDisplayName(a), QStringLiteral("The Benster [Ben] (@benji)"));
        QCOMPARE(h.host->dispatchUserContextMenuItems(u)[0].item.label, QStringLiteral("Change SMYN Nickname"));
        // Empty resets
        h.host->dispatchUserContextMenuAction(h.id, items[0].item.id, u);
        QCOMPARE(h.ui.promptInitial, QStringLiteral("The Benster"));
        h.ui.pendingPrompt("");
        QCOMPARE(*h.host->dispatchAuthorDisplayName(a), QStringLiteral("Benny [Ben] (@benji)"));
        // Not offered for yourself
        u.userId = 1;
        QVERIFY(h.host->dispatchUserContextMenuItems(u).isEmpty());

        // An invalid template falls back to the default
        QVERIFY(h.host->settings(h.id)->setValue("includedNames", "{bogus}").isEmpty());
        QCOMPARE(*h.host->dispatchAuthorDisplayName(a), QStringLiteral("Benny [Ben] (@benji)"));
        QVERIFY(h.host->settings(h.id)->setValue("includedNames", "{user} ~{nick}~").isEmpty());
        QCOMPARE(*h.host->dispatchAuthorDisplayName(a), QStringLiteral("benji ~Benny~"));
    }
};

QTEST_GUILESS_MAIN(TestShowMeYourName)
#include "tst_showmeyourname.moc"
