#include "SlashCommands.hpp"

#include <QHash>
#include <QSet>
#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>

namespace Qord {
namespace SlashCommands {

namespace {
constexpr int ChatInput = 1;

Option parseOption(const QJsonObject &o)
{
    Option opt;
    opt.type = o.value(QStringLiteral("type")).toInt(String);
    opt.name = o.value(QStringLiteral("name")).toString();
    opt.description = o.value(QStringLiteral("description")).toString();
    opt.required = o.value(QStringLiteral("required")).toBool();
    opt.autocomplete = o.value(QStringLiteral("autocomplete")).toBool();
    for (const auto &c : o.value(QStringLiteral("choices")).toArray())
        opt.choices.append({ c.toObject().value(QStringLiteral("name")).toString(), c.toObject().value(QStringLiteral("value")) });
    for (const auto &sub : o.value(QStringLiteral("options")).toArray())
        opt.options.append(parseOption(sub.toObject()));
    return opt;
}

bool isSub(const Option &o)
{
    return o.type == SubCommand || o.type == SubCommandGroup;
}

const Option *child(const QList<Option> &options, const QString &name)
{
    for (const auto &o : options)
        if (isSub(o) && o.name.compare(name, Qt::CaseInsensitive) == 0)
            return &o;
    return nullptr;
}

QString commandName(const QString &text)
{
    if (!text.startsWith(QLatin1Char('/')))
        return {};
    return text.mid(1).section(QLatin1Char(' '), 0, 0, QString::SectionSkipEmpty);
}

// The words after the command that name subcommands/groups, and the options they lead to
struct Path
{
    QStringList names;
    const QList<Option> *options = nullptr;
    int consumed = 0; // characters of `rest` used by the path
};

Path resolvePath(const Command &command, const QString &rest)
{
    Path path;
    path.options = &command.options;
    int pos = 0;
    while (true) {
        static const QRegularExpression word(QStringLiteral("^\\s*([^\\s:]+)(?=\\s|$)"));
        const auto m = word.match(rest.mid(pos));
        if (!m.hasMatch())
            break;
        const Option *sub = child(*path.options, m.captured(1));
        if (!sub)
            break;
        path.names.append(sub->name);
        path.options = &sub->options;
        pos += m.capturedEnd(0);
    }
    path.consumed = pos;
    return path;
}

QString idFrom(const QString &value, const QRegularExpression &mention)
{
    const auto m = mention.match(value.trimmed());
    if (m.hasMatch())
        return m.captured(1);
    static const QRegularExpression rawId(QStringLiteral("^\\d{15,21}$"));
    return rawId.match(value.trimmed()).hasMatch() ? value.trimmed() : QString();
}

// Returns an error, or sets `out`
QString convert(const Option &option, const QString &raw, QJsonValue &out)
{
    const QString value = raw.trimmed();
    if (!option.choices.isEmpty()) {
        for (const auto &[name, choice] : option.choices) {
            if (name.compare(value, Qt::CaseInsensitive) == 0 ||
                choice.toVariant().toString().compare(value, Qt::CaseInsensitive) == 0) {
                out = choice;
                return {};
            }
        }
        QStringList names;
        for (const auto &c : option.choices)
            names.append(c.first);
        return QStringLiteral("%1 must be one of: %2").arg(option.name, names.join(QStringLiteral(", ")));
    }
    static const QRegularExpression userMention(QStringLiteral("^<@!?(\\d+)>$"));
    static const QRegularExpression channelMention(QStringLiteral("^<#(\\d+)>$"));
    static const QRegularExpression roleMention(QStringLiteral("^<@&(\\d+)>$"));
    switch (option.type) {
    case Integer: {
        bool ok = false;
        const qlonglong n = value.toLongLong(&ok);
        if (!ok)
            return QStringLiteral("%1 must be a whole number").arg(option.name);
        out = n;
        return {};
    }
    case Number: {
        bool ok = false;
        const double n = value.toDouble(&ok);
        if (!ok)
            return QStringLiteral("%1 must be a number").arg(option.name);
        out = n;
        return {};
    }
    case Boolean: {
        const QString v = value.toLower();
        if (v == QLatin1String("true") || v == QLatin1String("yes"))
            out = true;
        else if (v == QLatin1String("false") || v == QLatin1String("no"))
            out = false;
        else
            return QStringLiteral("%1 must be true or false").arg(option.name);
        return {};
    }
    case User:
    case Channel:
    case Role:
    case Mentionable: {
        QString id;
        if (option.type == User || option.type == Mentionable)
            id = idFrom(value, userMention);
        if (id.isEmpty() && option.type == Channel)
            id = idFrom(value, channelMention);
        if (id.isEmpty() && (option.type == Role || option.type == Mentionable))
            id = idFrom(value, roleMention);
        if (id.isEmpty())
            return QStringLiteral("%1 must be a %2 mention").arg(
                    option.name, option.type == Channel ? QStringLiteral("channel")
                                 : option.type == Role  ? QStringLiteral("role")
                                                        : QStringLiteral("user"));
        out = id;
        return {};
    }
    case Attachment:
        return QStringLiteral("%1 needs a file, which Qord cannot attach to commands yet").arg(option.name);
    default:
        out = value;
        return {};
    }
}
} // namespace

Index parseIndex(const QJsonObject &json)
{
    Index index;
    QHash<QString, QString> appNames;
    for (const auto &a : json.value(QStringLiteral("applications")).toArray())
        appNames.insert(a.toObject().value(QStringLiteral("id")).toString(),
                        a.toObject().value(QStringLiteral("name")).toString());
    for (const auto &v : json.value(QStringLiteral("application_commands")).toArray()) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("type")).toInt(ChatInput) != ChatInput)
            continue;
        Command c;
        c.id = o.value(QStringLiteral("id")).toString();
        c.applicationId = o.value(QStringLiteral("application_id")).toString();
        c.applicationName = appNames.value(c.applicationId);
        c.version = o.value(QStringLiteral("version")).toString();
        c.name = o.value(QStringLiteral("name")).toString();
        c.description = o.value(QStringLiteral("description")).toString();
        for (const auto &opt : o.value(QStringLiteral("options")).toArray())
            c.options.append(parseOption(opt.toObject()));
        c.raw = o;
        if (!c.name.isEmpty())
            index.commands.append(c);
    }
    std::stable_sort(index.commands.begin(), index.commands.end(),
                     [](const Command &a, const Command &b) { return a.name < b.name; });
    return index;
}

Command localCommand(const QString &pluginId, const QString &pluginName, const QString &name,
                     const QString &description, const QJsonArray &options)
{
    Command c;
    c.id = QStringLiteral("local:%1:%2").arg(pluginId, name);
    c.applicationId = pluginId;
    c.applicationName = pluginName;
    c.name = name;
    c.description = description;
    for (const auto &opt : options)
        c.options.append(parseOption(opt.toObject()));
    c.local = true;
    return c;
}

Index withLocalCommands(const Index &index, const QList<Command> &local)
{
    Index merged;
    merged.commands = local + index.commands;
    std::stable_sort(merged.commands.begin(), merged.commands.end(),
                     [](const Command &a, const Command &b) { return a.name < b.name; });
    return merged;
}

QJsonObject optionValues(const QJsonObject &data)
{
    QJsonObject out;
    for (const auto &v : data.value(QStringLiteral("options")).toArray()) {
        const QJsonObject o = v.toObject();
        const int type = o.value(QStringLiteral("type")).toInt();
        if (type == SubCommand || type == SubCommandGroup) {
            const QJsonObject inner = optionValues(o);
            for (auto it = inner.begin(); it != inner.end(); ++it)
                out.insert(it.key(), it.value());
        } else {
            out.insert(o.value(QStringLiteral("name")).toString(), o.value(QStringLiteral("value")));
        }
    }
    return out;
}

QList<Command> suggest(const Index &index, const QString &typed, int limit)
{
    QList<Command> starts;
    QList<Command> contains;
    const QString needle = typed.trimmed().toLower();
    for (const auto &c : index.commands) {
        if (c.name.startsWith(needle))
            starts.append(c);
        else if (!needle.isEmpty() && c.name.contains(needle))
            contains.append(c);
    }
    QList<Command> out = starts + contains;
    if (out.size() > limit)
        out = out.mid(0, limit);
    return out;
}

const Command *find(const Index &index, const QString &text)
{
    const QString name = commandName(text);
    if (name.isEmpty())
        return nullptr;
    for (const auto &c : index.commands)
        if (c.name.compare(name, Qt::CaseInsensitive) == 0)
            return &c;
    return nullptr;
}

QString usage(const Command &command, const QString &text)
{
    QString out = QLatin1Char('/') + command.name;
    const QString rest = text.isEmpty() ? QString() : text.mid(1 + commandName(text).size());
    const Path path = resolvePath(command, rest);
    for (const auto &n : path.names)
        out += QLatin1Char(' ') + n;
    QStringList subs;
    for (const auto &o : *path.options) {
        if (isSub(o))
            subs.append(o.name);
        else
            out += o.required ? QStringLiteral(" %1:").arg(o.name) : QStringLiteral(" [%1:]").arg(o.name);
    }
    if (!subs.isEmpty())
        out += QStringLiteral(" <%1>").arg(subs.join(QLatin1Char('|')));
    return out;
}

namespace {
struct Mark
{
    int start;
    int valueStart;
    const Option *option;
};

// The command, its subcommand path and the "name:value" marks of `text`
struct Parsed
{
    const Command *command = nullptr;
    Path path;
    QString args; // the text after the command and its subcommands
    int argsOffset = 0; // where `args` starts in the text
    QList<Mark> marks;
    bool loneValue = false; // a value without "name:", for a single-option command
};

Parsed parse(const Index &index, const QString &text)
{
    Parsed p;
    p.command = find(index, text);
    if (!p.command)
        return p;
    const int restOffset = 1 + int(commandName(text).size());
    const QString rest = text.mid(restOffset);
    p.path = resolvePath(*p.command, rest);
    p.args = rest.mid(p.path.consumed);
    p.argsOffset = restOffset + p.path.consumed;
    static const QRegularExpression key(QStringLiteral("(?:^|\\s)([^\\s:]+):"));
    auto it = key.globalMatch(p.args);
    while (it.hasNext()) {
        const auto m = it.next();
        for (const auto &o : *p.path.options) {
            if (!isSub(o) && o.name.compare(m.captured(1), Qt::CaseInsensitive) == 0) {
                p.marks.append({ int(m.capturedStart(0)), int(m.capturedEnd(0)), &o });
                break;
            }
        }
    }
    if (p.marks.isEmpty() && !p.args.trimmed().isEmpty()) {
        // A lone value for a command with a single option, like Discord fills the first field
        const Option *only = nullptr;
        int count = 0;
        for (const auto &o : *p.path.options) {
            only = &o;
            ++count;
        }
        if (count == 1 && !isSub(*only)) {
            const int lead = int(p.args.size() - p.args.trimmed().size() == 0 ? 0 : p.args.indexOf(p.args.trimmed()));
            p.marks.append({ 0, qMax(0, lead), only });
            p.loneValue = true;
        }
    }
    return p;
}

QString rawValue(const Parsed &p, int i)
{
    const int end = i + 1 < p.marks.size() ? p.marks[i + 1].start : int(p.args.size());
    return p.args.mid(p.marks[i].valueStart, end - p.marks[i].valueStart);
}

// A picked choice stands for its value while the text still shows its name
bool pickedValue(const Picks &picks, const Option &option, const QString &raw, QJsonValue &out)
{
    const auto it = picks.constFind(option.name);
    if (it == picks.constEnd() || it->first.compare(raw.trimmed(), Qt::CaseInsensitive) != 0)
        return false;
    out = it->second;
    return true;
}

QJsonObject commandData(const Command &command, const Path &path, const QJsonArray &options)
{
    // Wrap in the subcommand path, innermost first
    QJsonArray wrapped = options;
    const QList<Option> *level = &command.options;
    QList<const Option *> chain;
    for (const auto &n : path.names) {
        const Option *sub = child(*level, n);
        chain.append(sub);
        level = &sub->options;
    }
    for (int i = int(chain.size()) - 1; i >= 0; --i) {
        QJsonObject sub { { QStringLiteral("type"), chain[i]->type }, { QStringLiteral("name"), chain[i]->name },
                          { QStringLiteral("options"), wrapped } };
        wrapped = QJsonArray { sub };
    }
    return QJsonObject {
        { QStringLiteral("version"), command.version },
        { QStringLiteral("id"), command.id },
        { QStringLiteral("name"), command.name },
        { QStringLiteral("type"), ChatInput },
        { QStringLiteral("options"), wrapped },
        { QStringLiteral("application_command"), command.raw },
        { QStringLiteral("attachments"), QJsonArray() },
    };
}
} // namespace

Built build(const Index &index, const QString &text, const Picks &picks)
{
    Built built;
    const Parsed p = parse(index, text);
    if (!p.command) {
        built.error = QStringLiteral("Unknown command");
        return built;
    }
    built.command = p.command;
    for (const auto &o : *p.path.options) {
        if (isSub(o)) {
            built.error = QStringLiteral("Choose a subcommand: ") + usage(*p.command, text);
            return built;
        }
    }
    if (p.marks.isEmpty() && !p.args.trimmed().isEmpty()) {
        built.error = QStringLiteral("Write options as name:value — ") + usage(*p.command, text);
        return built;
    }

    QJsonArray options;
    QSet<QString> given;
    for (int i = 0; i < p.marks.size(); ++i) {
        const QString raw = rawValue(p, i);
        if (raw.trimmed().isEmpty())
            continue;
        const Option &option = *p.marks[i].option;
        QJsonValue value;
        if (!pickedValue(picks, option, raw, value)) {
            const QString error = convert(option, raw, value);
            if (!error.isEmpty()) {
                built.error = error;
                return built;
            }
        }
        if (given.contains(option.name))
            continue;
        given.insert(option.name);
        options.append(QJsonObject { { QStringLiteral("type"), option.type },
                                     { QStringLiteral("name"), option.name },
                                     { QStringLiteral("value"), value } });
    }
    for (const auto &o : *p.path.options) {
        if (o.required && !given.contains(o.name)) {
            built.error = QStringLiteral("Missing %1 — %2").arg(o.name, usage(*p.command, text));
            return built;
        }
    }
    built.data = commandData(*p.command, p.path, options);
    built.ok = true;
    return built;
}

std::optional<Focus> focus(const Index &index, const QString &text, const Picks &picks)
{
    const Parsed p = parse(index, text);
    if (!p.command || p.marks.isEmpty())
        return std::nullopt;
    const int last = int(p.marks.size()) - 1;
    Focus f;
    f.command = p.command;
    f.option = p.marks[last].option;
    f.valueStart = p.argsOffset + p.marks[last].valueStart;
    f.typed = text.mid(f.valueStart);
    if (!f.option->autocomplete)
        return f;

    // Discord's autocomplete request: the options so far (as far as they
    // convert), then the focused one with what was typed, as a string
    QJsonArray options;
    QSet<QString> given;
    for (int i = 0; i < last; ++i) {
        const Option &option = *p.marks[i].option;
        const QString raw = rawValue(p, i);
        QJsonValue value;
        if (raw.trimmed().isEmpty() || given.contains(option.name))
            continue;
        if (!pickedValue(picks, option, raw, value) && !convert(option, raw, value).isEmpty())
            continue;
        given.insert(option.name);
        options.append(QJsonObject { { QStringLiteral("type"), option.type },
                                     { QStringLiteral("name"), option.name },
                                     { QStringLiteral("value"), value } });
    }
    options.append(QJsonObject { { QStringLiteral("type"), f.option->type },
                                 { QStringLiteral("name"), f.option->name },
                                 { QStringLiteral("value"), f.typed.trimmed() },
                                 { QStringLiteral("focused"), true } });
    f.autocompleteData = commandData(*p.command, p.path, options);
    return f;
}

QList<QPair<QString, QJsonValue>> matchingChoices(const Option &option, const QString &typed, int limit)
{
    QList<QPair<QString, QJsonValue>> starts;
    QList<QPair<QString, QJsonValue>> contains;
    const QString needle = typed.trimmed();
    for (const auto &choice : option.choices) {
        if (choice.first.startsWith(needle, Qt::CaseInsensitive))
            starts.append(choice);
        else if (choice.first.contains(needle, Qt::CaseInsensitive))
            contains.append(choice);
    }
    auto out = starts + contains;
    if (out.size() > limit)
        out = out.mid(0, limit);
    return out;
}

} // namespace SlashCommands
} // namespace Qord
