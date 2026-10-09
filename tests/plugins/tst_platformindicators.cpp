#include "PlatformIndicators.hpp"
#include "PluginTestUtils.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

namespace Qord::Plugins {
void qord_register_platformindicators(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestPlatformIndicators : public QObject
{
    Q_OBJECT

    static QJsonObject golden()
    {
        QFile file(QStringLiteral(QORD_TEST_DATA_DIR "/platformindicators-golden.json"));
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson(file.readAll()).object();
    }

    static DecorationContext user(DecorationContext::Place place = DecorationContext::Place::Message)
    {
        DecorationContext u;
        u.accountId = 1;
        u.userId = 42;
        u.place = place;
        u.clientStatus = { { QStringLiteral("desktop"), QStringLiteral("online") },
                           { QStringLiteral("mobile"), QStringLiteral("idle") } };
        return u;
    }

private slots:
    // Upstream's icon table, read from its source by the generator
    void iconsMatchUpstream()
    {
        const QJsonObject icons = golden()[u"icons"].toObject();
        QCOMPARE(icons.size(), 7);
        const QList<QPair<QString, QString>> keys = {
            { QStringLiteral("desktop"), QStringLiteral("equicord") },
            { QStringLiteral("web"), QStringLiteral("equicord") },
            { QStringLiteral("mobile"), QStringLiteral("equicord") },
            { QStringLiteral("vr"), QStringLiteral("equicord") },
            { QStringLiteral("embedded"), QStringLiteral("equicord") },
            { QStringLiteral("suncord"), QStringLiteral("suncord") },
            { QStringLiteral("vencord"), QStringLiteral("vencord") },
        };
        for (const auto &[name, console] : keys) {
            const QString platform =
                    (name == QLatin1String("suncord") || name == QLatin1String("vencord")) ? QStringLiteral("embedded") : name;
            const auto icon = PlatformIndicators::iconFor(platform, console);
            const QJsonObject expected = icons[name].toObject();
            QCOMPARE(icon.path, expected[u"path"].toString());
            QCOMPARE(icon.viewBox, expected[u"viewBox"].toString());
            QCOMPARE(icon.width, expected[u"width"].toInt());
            QCOMPARE(icon.height, expected[u"height"].toInt());
        }
        // Unknown platforms use the desktop icon
        QCOMPARE(PlatformIndicators::iconFor(QStringLiteral("xbox"), QStringLiteral("equicord")).path,
                 icons[u"desktop"].toObject()[u"path"].toString());
    }

    // useEnsureOwnStatus(), run in Node
    void ownStatusMatchesUpstream()
    {
        const QJsonArray cases = golden()[u"own"].toArray();
        QCOMPARE(cases.size(), 8);
        for (const auto &v : cases) {
            QList<SessionInfo> sessions;
            for (const auto &s : v.toObject()[u"sessions"].toArray()) {
                const QJsonObject session = s.toObject();
                sessions.append(SessionInfo { session[u"client"].toString(), session[u"status"].toString() });
            }
            QList<QPair<QString, QString>> expected;
            for (const auto &e : v.toObject()[u"expected"].toArray()) {
                const QJsonArray pair = e.toArray();
                expected.append(qMakePair(pair[0].toString(), pair[1].toString()));
            }
            QCOMPARE(PlatformIndicators::ownClientStatus(sessions), expected);
        }
    }

    void tooltips()
    {
        QCOMPARE(PlatformIndicators::tooltipFor(QStringLiteral("embedded")), QStringLiteral("Embedded (Console or Game)"));
        QCOMPARE(PlatformIndicators::tooltipFor(QStringLiteral("vr")), QStringLiteral("VR"));
        QCOMPARE(PlatformIndicators::tooltipFor(QStringLiteral("desktop")), QStringLiteral("Desktop"));
        QCOMPARE(PlatformIndicators::tooltipFor(QStringLiteral("mobile")), QStringLiteral("Mobile"));
    }

    void decorations()
    {
        SinglePluginHost h(qord_register_platformindicators);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.host->dispatchNameDecorations(user()).isEmpty()); // disabled
        QVERIFY(h.enableWithAllPermissions());
        auto d = h.host->dispatchNameDecorations(user());
        QCOMPARE(d.size(), 2);
        QCOMPARE(d[0].tooltip, QStringLiteral("Desktop"));
        QCOMPARE(d[0].color, QStringLiteral("status:online"));
        QCOMPARE(d[0].width, 20);
        QVERIFY(d[0].svg.contains("viewBox=\"0 0 24 24\""));
        QVERIFY(d[0].svg.contains("fill=\"currentColor\""));
        QCOMPARE(d[1].tooltip, QStringLiteral("Mobile"));
        QCOMPARE(d[1].color, QStringLiteral("status:idle"));
        QCOMPARE(d[1].width, 17);

        // Member list and profiles use the small icons
        d = h.host->dispatchNameDecorations(user(DecorationContext::Place::MemberList));
        QCOMPARE(d[0].width, 17);
        QCOMPARE(d[1].height, 14);
        d = h.host->dispatchNameDecorations(user(DecorationContext::Place::Profile));
        QCOMPARE(d[0].height, 17);
    }

    void selfUsesSessions()
    {
        SinglePluginHost h(qord_register_platformindicators);
        QVERIFY(h.enableWithAllPermissions());
        DecorationContext me = user();
        me.isSelf = true;
        me.ownSessions = { { QStringLiteral("web"), QStringLiteral("dnd") },
                           { QStringLiteral("unknown"), QStringLiteral("online") } };
        const auto d = h.host->dispatchNameDecorations(me);
        QCOMPARE(d.size(), 1);
        QCOMPARE(d[0].tooltip, QStringLiteral("Web"));
        QCOMPARE(d[0].color, QStringLiteral("status:dnd"));
    }

    void settingsAndBots()
    {
        SinglePluginHost h(qord_register_platformindicators);
        QVERIFY(h.enableWithAllPermissions());
        auto *s = h.host->settings(h.id);
        DecorationContext bot = user();
        bot.isBot = true;
        QVERIFY(h.host->dispatchNameDecorations(bot).isEmpty());
        s->setValue(QStringLiteral("showBots"), true);
        QCOMPARE(h.host->dispatchNameDecorations(bot).size(), 2);

        s->setValue(QStringLiteral("messages"), false);
        QVERIFY(h.host->dispatchNameDecorations(user()).isEmpty());
        s->setValue(QStringLiteral("list"), false);
        QVERIFY(h.host->dispatchNameDecorations(user(DecorationContext::Place::MemberList)).isEmpty());
        s->setValue(QStringLiteral("profiles"), false);
        QVERIFY(h.host->dispatchNameDecorations(user(DecorationContext::Place::Profile)).isEmpty());

        DecorationContext console = user(DecorationContext::Place::MemberList);
        s->setValue(QStringLiteral("list"), true);
        s->setValue(QStringLiteral("ConsoleIcon"), QStringLiteral("suncord"));
        console.clientStatus = { { QStringLiteral("embedded"), QStringLiteral("online") } };
        const auto d = h.host->dispatchNameDecorations(console);
        QCOMPARE(d.size(), 1);
        QCOMPARE(d[0].width, 21); // suncord is 24px, minus 3 in the member list
        QCOMPARE(d[0].tooltip, QStringLiteral("Embedded (Console or Game)"));
    }

    void mobileIndicator()
    {
        SinglePluginHost h(qord_register_platformindicators);
        QVERIFY(!h.displayOptions.mobileAnyStatus);
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(h.displayOptions.mobileAnyStatus);
        h.host->settings(h.id)->setValue(QStringLiteral("colorMobileIndicator"), false);
        QVERIFY(!h.displayOptions.mobileAnyStatus);
        h.host->settings(h.id)->setValue(QStringLiteral("colorMobileIndicator"), true);
        QVERIFY(h.displayOptions.mobileAnyStatus);
        QVERIFY(h.host->setEnabled(h.id, false));
        QVERIFY(!h.displayOptions.mobileAnyStatus);
    }

    void needsPermission()
    {
        SinglePluginHost h(qord_register_platformindicators);
        h.host->setGranted(h.id, Permission::ModifyDisplayedMessages);
        QVERIFY(h.host->setEnabled(h.id, true));
        QVERIFY(h.host->dispatchNameDecorations(user()).isEmpty());
        // The status dot option needs modify_displayed_messages
        h.host->setGranted(h.id, Permission::ReadPresences);
        QVERIFY(!h.displayOptions.mobileAnyStatus);
        QCOMPARE(h.host->dispatchNameDecorations(user()).size(), 2);
    }
};

QTEST_GUILESS_MAIN(TestPlatformIndicators)
#include "tst_platformindicators.moc"
