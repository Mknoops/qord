#include "PluginTestUtils.hpp"

#include "Ingtoninator.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

namespace Qord::Plugins {
void qord_register_ingtoninator(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestIngtoninator : public QObject
{
    Q_OBJECT

private slots:
    void golden_data()
    {
        QTest::addColumn<QJsonObject>("c");
        QFile f(QStringLiteral(QORD_TEST_DATA_DIR "/ingtoninator-golden.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        int row = 0;
        for (const auto &c : QJsonDocument::fromJson(f.readAll()).array())
            QTest::addRow("%d %s", row++, qPrintable(c.toObject()["input"].toString())) << c.toObject();
    }

    // Same output as upstream handleMessage() under V8, with the same random draws
    void golden()
    {
        QFETCH(QJsonObject, c);
        QList<double> draws;
        for (const auto &v : c["random"].toArray())
            draws.append(v.toDouble());
        auto random = [&draws] { return draws.isEmpty() ? 0.0 : draws.takeFirst(); };
        QCOMPARE(Ingtoninator::apply(c["input"].toString(), random), c["expected"].toString());
    }

    void suffixes()
    {
        QCOMPARE(Ingtoninator::ington("WASHINGTON"), QString());
        QCOMPARE(Ingtoninator::ington("WASHINGTO"), QStringLiteral("N"));
        QCOMPARE(Ingtoninator::ington("SING"), QStringLiteral("TON"));
        QCOMPARE(Ingtoninator::ington("KEVIN"), QStringLiteral("GTON"));
        QCOMPARE(Ingtoninator::ington("TAXI"), QStringLiteral("NGTON"));
        QCOMPARE(Ingtoninator::ington("CAT"), QStringLiteral("INGTON"));
    }

    void pluginButtonTogglesSetting()
    {
        SinglePluginHost h(qord_register_ingtoninator);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        ChatContext chat;
        auto buttons = h.host->dispatchChatBarButtons(chat);
        QCOMPARE(buttons.size(), 1);
        QVERIFY(buttons[0].button.active);
        QCOMPARE(buttons[0].button.tooltip, QStringLiteral("Ingtoninator Enabled"));

        OutgoingMessage m;
        m.content = "cat";
        h.host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("catington"));

        h.host->dispatchChatBarButtonClicked(h.id, "toggle", chat);
        QCOMPARE(h.host->settings(h.id)->boolValue("isEnabled"), false);
        buttons = h.host->dispatchChatBarButtons(chat);
        QVERIFY(!buttons[0].button.active);
        m.content = "cat";
        h.host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("cat"));

        QVERIFY(h.host->settings(h.id)->setValue("showIcon", false).isEmpty());
        QVERIFY(h.host->dispatchChatBarButtons(chat).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestIngtoninator)
#include "tst_ingtoninator.moc"
