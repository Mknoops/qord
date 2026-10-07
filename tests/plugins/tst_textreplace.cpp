#include "PluginTestUtils.hpp"

#include "TextReplace.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

namespace Qord::Plugins {
void qord_register_textreplace(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;
using namespace Qord::Plugins::TextReplace;

static QJsonArray loadGolden()
{
    QFile f(QStringLiteral(QORD_TEST_DATA_DIR "/textreplace-golden.json"));
    if (!f.open(QIODevice::ReadOnly))
        qFatal("missing textreplace-golden.json");
    return QJsonDocument::fromJson(f.readAll()).array();
}

static QJsonObject rule(const QString &find, const QString &replace, const QString &scope = "myMessages")
{
    return QJsonObject { { "id", "r" }, { "name", "" }, { "find", find }, { "replace", replace },
                         { "onlyIfIncludes", "" }, { "scope", scope } };
}

class TestTextReplace : public QObject
{
    Q_OBJECT

private slots:
    void golden_data()
    {
        QTest::addColumn<QJsonObject>("c");
        const QJsonArray cases = loadGolden();
        QVERIFY(cases.size() >= 30);
        for (const auto &c : cases)
            QTest::newRow(qPrintable(c.toObject()["name"].toString())) << c.toObject();
    }

    // Same output as upstream applyRules() under V8
    void golden()
    {
        QFETCH(QJsonObject, c);
        const QString out = applyRules(c["input"].toString(), scopeFromString(c["scope"].toString()),
                                       rulesFromJson(c["stringRules"].toArray()),
                                       rulesFromJson(c["regexRules"].toArray()));
        QCOMPARE(out, c["expected"].toString());
    }

    void invalidRegexIsReported()
    {
        QStringList errors;
        const QString out = applyRules("a(", Scope::MyMessages, {}, rulesFromJson(QJsonArray { rule("/(/", "x"), rule("a", "b") }),
                                       [&](const QString &e) { errors.append(e); });
        QCOMPARE(out, QStringLiteral("b("));
        QCOMPARE(errors, QStringList { "Invalid regex: /(/" });
        QVERIFY(!checkRegex("/(/").isEmpty());
        QVERIFY(checkRegex("/a+/gi").isEmpty());
        QVERIFY(checkRegex("plain").isEmpty());
    }

    void rulesRoundTripAndNormalize()
    {
        QJsonArray in { QJsonObject { { "find", "a" }, { "replace", "b" } } }; // old rule without scope/id
        const QList<Rule> rules = rulesFromJson(in);
        QCOMPARE(rules.size(), 1);
        QCOMPARE(rules[0].scope, Scope::MyMessages);
        QVERIFY(!rules[0].id.isEmpty());
        const QList<Rule> again = rulesFromJson(rulesToJson(rules));
        QCOMPARE(again[0].id, rules[0].id);
        QCOMPARE(again[0].find, QStringLiteral("a"));
        QCOMPARE(scopeToString(Scope::OthersMessages), QStringLiteral("othersMessages"));
        QCOMPARE(scopeFromString("bogus"), Scope::MyMessages);
    }

    // ------------------------------------------------------------- plugin
    void pluginSendAndIncoming()
    {
        SinglePluginHost h(qord_register_textreplace);
        QVERIFY(h.info()->loadError.isEmpty());
        PluginSettings *s = h.host->settings(h.id);
        QVERIFY(s);
        QVERIFY(s->setValue("stringRules", QJsonArray { rule("brb", "be right back"), rule("lol", "haha", "othersMessages") }).isEmpty());
        QVERIFY(s->setValue("regexRules", QJsonArray { rule("/(\\d+)c/", "$1°C", "allMessages") }).isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(h.host->hasHook(Hook::BeforeSend));
        QVERIFY(h.host->hasHook(Hook::TransformIncomingContent));

        OutgoingMessage m;
        m.channelId = 42;
        m.content = "brb lol 20c";
        h.host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("be right back lol 20°C"));

        IncomingMessage in;
        in.authorId = 7;
        in.content = "brb lol 20c";
        h.host->dispatchTransformIncomingContent(in);
        QCOMPARE(in.content, QStringLiteral("brb haha 20°C"));

        // Own messages and authorless messages are never touched on display
        IncomingMessage own = in;
        own.content = "lol";
        own.isOwn = true;
        h.host->dispatchTransformIncomingContent(own);
        QCOMPARE(own.content, QStringLiteral("lol"));
        IncomingMessage system;
        system.content = "lol";
        h.host->dispatchTransformIncomingContent(system);
        QCOMPARE(system.content, QStringLiteral("lol"));
    }

    void pluginSkipsExemptChannels()
    {
        SinglePluginHost h(qord_register_textreplace);
        QVERIFY(h.host->settings(h.id)->setValue("stringRules", QJsonArray { rule("a", "b") }).isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        for (Id channel : exemptChannelIds()) {
            OutgoingMessage m;
            m.channelId = channel;
            m.content = "a";
            h.host->dispatchBeforeSend(m);
            QCOMPARE(m.content, QStringLiteral("a"));
        }
        QCOMPARE(exemptChannelIds().size(), 4);
    }

    void pluginReloadsOnSettingChange()
    {
        SinglePluginHost h(qord_register_textreplace);
        QVERIFY(h.enableWithAllPermissions());
        OutgoingMessage m;
        m.content = "a";
        h.host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("a"));
        QVERIFY(h.host->settings(h.id)->setValue("stringRules", QJsonArray { rule("a", "b") }).isEmpty());
        h.host->dispatchBeforeSend(m);
        QCOMPARE(m.content, QStringLiteral("b"));
    }

    void pluginWithoutDisplayPermission()
    {
        SinglePluginHost h(qord_register_textreplace);
        QVERIFY(h.host->settings(h.id)->setValue("stringRules", QJsonArray { rule("a", "b", "allMessages") }).isEmpty());
        h.host->setGranted(h.id, Permission::ModifyOutgoingMessages);
        QVERIFY(h.host->setEnabled(h.id, true));
        QVERIFY(h.host->hasHook(Hook::BeforeSend));
        QVERIFY(!h.host->hasHook(Hook::TransformIncomingContent));
    }

    void benchmarkTwentyRules()
    {
        QJsonArray strings, regexes;
        for (int i = 0; i < 10; ++i) {
            strings.append(rule(QStringLiteral("word%1").arg(i), QStringLiteral("w%1").arg(i)));
            regexes.append(rule(QStringLiteral("/n%1(\\d+)/gi").arg(i), QStringLiteral("$1")));
        }
        const RuleSet rules = RuleSet::build(rulesFromJson(strings), rulesFromJson(regexes));
        const QString msg = QStringLiteral("a typical message with word3 and N5123 inside of it, nothing else");
        QBENCHMARK {
            (void)applyRules(msg, Scope::MyMessages, rules);
        }
    }
};

QTEST_GUILESS_MAIN(TestTextReplace)
#include "tst_textreplace.moc"
