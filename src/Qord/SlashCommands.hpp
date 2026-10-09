#pragma once

// Slash commands: Discord's application command index, suggestions while
// typing "/", and turning "/name sub option:value" into an interaction.
// Pure data handling (no network), so it is unit tested.

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QPair>
#include <QString>

namespace Qord {
namespace SlashCommands {

// Discord's ApplicationCommandOptionType
enum OptionType {
    SubCommand = 1,
    SubCommandGroup = 2,
    String = 3,
    Integer = 4,
    Boolean = 5,
    User = 6,
    Channel = 7,
    Role = 8,
    Mentionable = 9,
    Number = 10,
    Attachment = 11,
};

struct Option
{
    int type = String;
    QString name;
    QString description;
    bool required = false;
    QList<QPair<QString, QJsonValue>> choices; // name -> value
    QList<Option> options; // for subcommands and groups
};

struct Command
{
    QString id;
    QString applicationId;
    QString applicationName;
    QString version;
    QString name;
    QString description;
    QList<Option> options;
    QJsonObject raw; // sent back as data.application_command
};

struct Index
{
    QList<Command> commands; // chat input commands only, sorted by name
};

// GET /guilds/{id}/application-command-index (or a channel's) response
Index parseIndex(const QJsonObject &json);

// Commands whose name starts with `typed` (then those containing it), at most `limit`
QList<Command> suggest(const Index &index, const QString &typed, int limit = 25);

// "/name sub <required> [optional]" for the command, or for the subcommand
// named in `text` when there is one
QString usage(const Command &command, const QString &text = QString());

// If `text` starts with "/" and names a command in the index
const Command *find(const Index &index, const QString &text);

struct Built
{
    bool ok = false;
    QString error; // for the user, when !ok
    QJsonObject data; // the interaction's "data"
    const Command *command = nullptr;
};

// "/name [group] [sub] option:value option2:value"; values run until the next
// "known_option:". Mentions (<@id>, <#id>, <@&id>) give ids; choices match by
// name or value.
Built build(const Index &index, const QString &text);

} // namespace SlashCommands
} // namespace Qord
