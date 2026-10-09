#include "Qord/SlashCommands.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

using namespace Qord::SlashCommands;

namespace {
// The shape of GET /guilds/{id}/application-command-index
const char *IndexJson = R"({
  "applications": [{ "id": "900", "name": "Helper Bot" }],
  "application_commands": [
    { "id": "1", "application_id": "900", "version": "11", "type": 1, "name": "ping", "description": "Pong!" },
    { "id": "2", "application_id": "900", "version": "12", "type": 1, "name": "remind", "description": "Set a reminder",
      "options": [
        { "type": 3, "name": "what", "description": "What to remember", "required": true },
        { "type": 4, "name": "minutes", "description": "In how many minutes", "required": true },
        { "type": 5, "name": "dm", "description": "Send it in DMs" }
      ] },
    { "id": "3", "application_id": "900", "version": "13", "type": 1, "name": "config", "description": "Settings",
      "options": [
        { "type": 1, "name": "show", "description": "Show settings" },
        { "type": 2, "name": "role", "description": "Role settings", "options": [
          { "type": 1, "name": "set", "description": "Set the role", "options": [
            { "type": 8, "name": "role", "description": "The role", "required": true },
            { "type": 3, "name": "mode", "description": "Mode", "choices": [
              { "name": "Strict", "value": "strict" }, { "name": "Loose", "value": "loose" } ] }
          ] }
        ] }
      ] },
    { "id": "4", "application_id": "900", "version": "14", "type": 1, "name": "say", "description": "Repeat",
      "options": [ { "type": 3, "name": "text", "description": "Text", "required": true } ] },
    { "id": "5", "application_id": "900", "version": "15", "type": 2, "name": "User Info", "description": "" },
    { "id": "6", "application_id": "900", "version": "16", "type": 1, "name": "warn", "description": "Warn",
      "options": [ { "type": 6, "name": "user", "description": "Who", "required": true },
                   { "type": 10, "name": "points", "description": "Points" },
                   { "type": 7, "name": "where", "description": "Channel" } ] }
  ]
})";
} // namespace

class TestSlashCommands : public QObject
{
    Q_OBJECT

    Index index;

private slots:
    void initTestCase() { index = parseIndex(QJsonDocument::fromJson(IndexJson).object()); }

    void parsesChatInputCommandsOnly()
    {
        QCOMPARE(index.commands.size(), 5); // the user command (type 2) is not a slash command
        QCOMPARE(index.commands.first().name, QStringLiteral("config"));
        QCOMPARE(index.commands.first().applicationName, QStringLiteral("Helper Bot"));
    }

    void suggestions()
    {
        QStringList names;
        for (const auto &c : suggest(index, QString()))
            names << c.name;
        QCOMPARE(names, (QStringList { "config", "ping", "remind", "say", "warn" }));
        names.clear();
        for (const auto &c : suggest(index, QStringLiteral("r")))
            names << c.name;
        QCOMPARE(names, (QStringList { "remind", "warn" })); // prefix first, then contains
    }

    void usageText()
    {
        const Command *remind = find(index, QStringLiteral("/remind"));
        QVERIFY(remind);
        QCOMPARE(usage(*remind), QStringLiteral("/remind what: minutes: [dm:]"));
        const Command *config = find(index, QStringLiteral("/config role set"));
        QCOMPARE(usage(*config, QStringLiteral("/config role set")), QStringLiteral("/config role set role: [mode:]"));
        QCOMPARE(usage(*config), QStringLiteral("/config <show|role>"));
    }

    void buildsOptions()
    {
        const Built b = build(index, QStringLiteral("/remind what: buy milk and eggs minutes: 30 dm:yes"));
        QVERIFY2(b.ok, qPrintable(b.error));
        QCOMPARE(b.data.value("id").toString(), QStringLiteral("2"));
        QCOMPARE(b.data.value("version").toString(), QStringLiteral("12"));
        QCOMPARE(b.data.value("type").toInt(), 1);
        const QJsonArray opts = b.data.value("options").toArray();
        QCOMPARE(opts.size(), 3);
        QCOMPARE(opts[0].toObject().value("value").toString(), QStringLiteral("buy milk and eggs"));
        QCOMPARE(opts[1].toObject().value("value").toVariant().toLongLong(), 30LL);
        QCOMPARE(opts[1].toObject().value("type").toInt(), 4);
        QCOMPARE(opts[2].toObject().value("value").toBool(), true);
        QCOMPARE(b.data.value("application_command").toObject().value("name").toString(), QStringLiteral("remind"));
    }

    void noOptions()
    {
        const Built b = build(index, QStringLiteral("/ping"));
        QVERIFY(b.ok);
        QVERIFY(b.data.value("options").toArray().isEmpty());
    }

    void singleOptionWithoutName()
    {
        const Built b = build(index, QStringLiteral("/say hello there"));
        QVERIFY2(b.ok, qPrintable(b.error));
        QCOMPARE(b.data.value("options").toArray()[0].toObject().value("value").toString(),
                 QStringLiteral("hello there"));
    }

    void subcommandsAndChoices()
    {
        Built b = build(index, QStringLiteral("/config show"));
        QVERIFY2(b.ok, qPrintable(b.error));
        QCOMPARE(b.data.value("options").toArray()[0].toObject().value("name").toString(), QStringLiteral("show"));
        QCOMPARE(b.data.value("options").toArray()[0].toObject().value("type").toInt(), 1);

        b = build(index, QStringLiteral("/config role set role:<@&555555555555555555> mode:Strict"));
        QVERIFY2(b.ok, qPrintable(b.error));
        const QJsonObject group = b.data.value("options").toArray()[0].toObject();
        QCOMPARE(group.value("type").toInt(), 2);
        QCOMPARE(group.value("name").toString(), QStringLiteral("role"));
        const QJsonObject sub = group.value("options").toArray()[0].toObject();
        QCOMPARE(sub.value("name").toString(), QStringLiteral("set"));
        const QJsonArray leaf = sub.value("options").toArray();
        QCOMPARE(leaf[0].toObject().value("value").toString(), QStringLiteral("555555555555555555"));
        QCOMPARE(leaf[1].toObject().value("value").toString(), QStringLiteral("strict"));
    }

    void mentionsAndNumbers()
    {
        const Built b = build(index, QStringLiteral("/warn user:<@!123456789012345678> points: 2.5 where:<#987654321098765432>"));
        QVERIFY2(b.ok, qPrintable(b.error));
        const QJsonArray opts = b.data.value("options").toArray();
        QCOMPARE(opts[0].toObject().value("value").toString(), QStringLiteral("123456789012345678"));
        QCOMPARE(opts[1].toObject().value("value").toDouble(), 2.5);
        QCOMPARE(opts[2].toObject().value("value").toString(), QStringLiteral("987654321098765432"));
    }

    void errors()
    {
        QCOMPARE(build(index, QStringLiteral("/nope")).error, QStringLiteral("Unknown command"));
        QVERIFY(build(index, QStringLiteral("/remind what: x")).error.startsWith(QStringLiteral("Missing minutes")));
        QCOMPARE(build(index, QStringLiteral("/remind what: x minutes: soon")).error,
                 QStringLiteral("minutes must be a whole number"));
        QVERIFY(build(index, QStringLiteral("/config")).error.startsWith(QStringLiteral("Choose a subcommand")));
        QVERIFY(build(index, QStringLiteral("/config role set role:<@&1> mode:Medium")).error.isEmpty() == false);
        QCOMPARE(build(index, QStringLiteral("/warn user: bob")).error, QStringLiteral("user must be a user mention"));
        QVERIFY(!find(index, QStringLiteral("hello")));
    }
};

QTEST_GUILESS_MAIN(TestSlashCommands)
#include "tst_slash_commands.moc"
