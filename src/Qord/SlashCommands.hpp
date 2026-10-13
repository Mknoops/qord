#pragma once

// Slash commands: Discord's application command index, suggestions while
// typing "/", and turning "/name sub option:value" into an interaction.
// Pure data handling (no network), so it is unit tested.

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QPair>
#include <QString>

#include <optional>

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
    bool autocomplete = false; // the app suggests values as you type
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

// Values picked from a suggestion list: option name -> (name shown in the
// text, value sent). A picked autocomplete choice sends its value, which the
// text alone can't say (an app may show "Paris" and want "fr-75").
using Picks = QHash<QString, QPair<QString, QJsonValue>>;

// "/name [group] [sub] option:value option2:value"; values run until the next
// "known_option:". Mentions (<@id>, <#id>, <@&id>) give ids; choices match by
// name or value.
Built build(const Index &index, const QString &text, const Picks &picks = {});

// The option whose value is being typed at the end of `text`
struct Focus
{
    const Command *command = nullptr;
    const Option *option = nullptr;
    QString typed; // its value so far
    int valueStart = -1; // where that value starts in `text`
    // For an autocomplete option: the interaction's "data", with the options
    // given so far and this one marked focused (Discord's type 4 request)
    QJsonObject autocompleteData;
};
std::optional<Focus> focus(const Index &index, const QString &text, const Picks &picks = {});

// An option's choices that match what was typed (by name, then containing it)
QList<QPair<QString, QJsonValue>> matchingChoices(const Option &option, const QString &typed, int limit = 25);

} // namespace SlashCommands
} // namespace Qord
