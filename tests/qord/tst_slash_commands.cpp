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
                   { "type": 7, "name": "where", "description": "Channel" } ] },
    { "id": "7", "application_id": "900", "version": "17", "type": 1, "name": "weather", "description": "Weather",
      "options": [ { "type": 3, "name": "city", "description": "City", "required": true, "autocomplete": true },
                   { "type": 4, "name": "days", "description": "Days" } ] }
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
        QCOMPARE(index.commands.size(), 6); // the user command (type 2) is not a slash command
        QCOMPARE(index.commands.first().name, QStringLiteral("config"));
        QCOMPARE(index.commands.first().applicationName, QStringLiteral("Helper Bot"));
    }

    void suggestions()
    {
        QStringList names;
        for (const auto &c : suggest(index, QString()))
            names << c.name;
        QCOMPARE(names, (QStringList { "config", "ping", "remind", "say", "warn", "weather" }));
        names.clear();
        for (const auto &c : suggest(index, QStringLiteral("r")))
            names << c.name;
        QCOMPARE(names, (QStringList { "remind", "warn", "weather" })); // prefix first, then contains
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

    void focusFollowsTheLastOption()
    {
        const QString text = QStringLiteral("/config role set role:<@&5> mode:St");
        const auto f = focus(index, text);
        QVERIFY(f.has_value());
        QCOMPARE(f->option->name, QStringLiteral("mode"));
        QCOMPARE(f->typed, QStringLiteral("St"));
        QCOMPARE(text.mid(f->valueStart), QStringLiteral("St"));
        QVERIFY(f->autocompleteData.isEmpty()); // static choices: no request
        const auto choices = matchingChoices(*f->option, f->typed);
        QCOMPARE(choices.size(), 1);
        QCOMPARE(choices[0].first, QStringLiteral("Strict"));
        // An empty value lists them all; "oos" finds Loose by containing it
        QCOMPARE(matchingChoices(*f->option, QString()).size(), 2);
        QCOMPARE(matchingChoices(*f->option, QStringLiteral("oos")).first().first, QStringLiteral("Loose"));
        // No option being written yet
        QVERIFY(!focus(index, QStringLiteral("/remind ")).has_value());
        QVERIFY(!focus(index, QStringLiteral("/nope a:b")).has_value());
    }

    void loneValueIsFocusedToo()
    {
        const auto f = focus(index, QStringLiteral("/say  hello there"));
        QVERIFY(f.has_value());
        QCOMPARE(f->option->name, QStringLiteral("text"));
        QCOMPARE(f->typed, QStringLiteral("hello there"));
    }

    void autocompleteRequestData()
    {
        const auto f = focus(index, QStringLiteral("/weather days:3 city:Par"));
        QVERIFY(f.has_value());
        QVERIFY(f->option->autocomplete);
        const QJsonObject d = f->autocompleteData;
        QCOMPARE(d.value("name").toString(), QStringLiteral("weather"));
        QCOMPARE(d.value("id").toString(), QStringLiteral("7"));
        const QJsonArray options = d.value("options").toArray();
        QCOMPARE(options.size(), 2);
        QCOMPARE(options[0].toObject().value("name").toString(), QStringLiteral("days"));
        QCOMPARE(options[0].toObject().value("value").toInt(), 3);
        QVERIFY(!options[0].toObject().contains("focused"));
        QCOMPARE(options[1].toObject().value("name").toString(), QStringLiteral("city"));
        QCOMPARE(options[1].toObject().value("value").toString(), QStringLiteral("Par"));
        QVERIFY(options[1].toObject().value("focused").toBool());
        // Options that don't convert yet are left out, not an error
        const auto partial = focus(index, QStringLiteral("/weather days:x city:P"));
        QCOMPARE(partial->autocompleteData.value("options").toArray().size(), 1);
    }

    void picksSendTheirValue()
    {
        // The text shows the choice's name; the app gets its value
        Picks picks;
        picks.insert(QStringLiteral("city"), { QStringLiteral("Paris"), QStringLiteral("fr-75") });
        const Built built = build(index, QStringLiteral("/weather city:Paris days:2"), picks);
        QVERIFY2(built.ok, qPrintable(built.error));
        const QJsonArray options = built.data.value("options").toArray();
        QCOMPARE(options[0].toObject().value("value").toString(), QStringLiteral("fr-75"));
        // Edited after picking: the text is sent as typed
        const Built edited = build(index, QStringLiteral("/weather city:Parma"), picks);
        QCOMPARE(edited.data.value("options").toArray()[0].toObject().value("value").toString(), QStringLiteral("Parma"));
    }

    void localCommands()
    {
        // A plugin's command joins Discord's and is built the same way
        const QJsonArray options = QJsonDocument::fromJson(R"([
            { "type": 3, "name": "toggle", "description": "Where",
              "choices": [ { "name": "Global", "value": "global" }, { "name": "Channel", "value": "channel" } ] },
            { "type": 5, "name": "chat-icon", "description": "Icon" }
        ])").array();
        const Command local = localCommand(QStringLiteral("silenttyping"), QStringLiteral("SilentTyping"),
                                           QStringLiteral("silent-typing"), QStringLiteral("Hide typing"), options);
        QVERIFY(local.local);
        const Index merged = withLocalCommands(index, { local });
        QCOMPARE(merged.commands.size(), index.commands.size() + 1);
        QCOMPARE(suggest(merged, QStringLiteral("sil")).value(0).name, QStringLiteral("silent-typing"));
        QCOMPARE(suggest(merged, QStringLiteral("sil")).value(0).applicationName, QStringLiteral("SilentTyping"));
        const Built built = build(merged, QStringLiteral("/silent-typing toggle:Channel chat-icon:false"));
        QVERIFY2(built.ok, qPrintable(built.error));
        QVERIFY(built.command->local);
        QCOMPARE(built.command->applicationId, QStringLiteral("silenttyping"));
        const QJsonObject values = optionValues(built.data);
        QCOMPARE(values.value("toggle").toString(), QStringLiteral("channel"));
        QCOMPARE(values.value("chat-icon").toBool(true), false);
        QVERIFY(!values.contains("missing"));
    }
};

QTEST_GUILESS_MAIN(TestSlashCommands)
#include "tst_slash_commands.moc"
