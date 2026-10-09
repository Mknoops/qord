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

Built build(const Index &index, const QString &text)
{
    Built built;
    const Command *command = find(index, text);
    if (!command) {
        built.error = QStringLiteral("Unknown command");
        return built;
    }
    built.command = command;
    const QString rest = text.mid(1 + commandName(text).size());
    const Path path = resolvePath(*command, rest);
    for (const auto &o : *path.options) {
        if (isSub(o)) {
            built.error = QStringLiteral("Choose a subcommand: ") + usage(*command, text);
            return built;
        }
    }

    // "name:value" pairs for the options at this level
    const QString args = rest.mid(path.consumed);
    struct Mark
    {
        int start;
        int valueStart;
        const Option *option;
    };
    QList<Mark> marks;
    static const QRegularExpression key(QStringLiteral("(?:^|\\s)([^\\s:]+):"));
    auto it = key.globalMatch(args);
    while (it.hasNext()) {
        const auto m = it.next();
        for (const auto &o : *path.options) {
            if (o.name.compare(m.captured(1), Qt::CaseInsensitive) == 0) {
                marks.append({ int(m.capturedStart(0)), int(m.capturedEnd(0)), &o });
                break;
            }
        }
    }
    if (marks.isEmpty() && !args.trimmed().isEmpty()) {
        // A lone value for a command with a single option, like Discord fills the first field
        const Option *only = nullptr;
        int count = 0;
        for (const auto &o : *path.options) {
            only = &o;
            ++count;
        }
        if (count == 1)
            marks.append({ 0, 0, only });
        else {
            built.error = QStringLiteral("Write options as name:value — ") + usage(*command, text);
            return built;
        }
    }

    QJsonArray options;
    QSet<QString> given;
    for (int i = 0; i < marks.size(); ++i) {
        const int end = i + 1 < marks.size() ? marks[i + 1].start : int(args.size());
        const QString raw = args.mid(marks[i].valueStart, end - marks[i].valueStart);
        if (raw.trimmed().isEmpty())
            continue;
        QJsonValue value;
        const QString error = convert(*marks[i].option, raw, value);
        if (!error.isEmpty()) {
            built.error = error;
            return built;
        }
        if (given.contains(marks[i].option->name))
            continue;
        given.insert(marks[i].option->name);
        options.append(QJsonObject { { QStringLiteral("type"), marks[i].option->type },
                                     { QStringLiteral("name"), marks[i].option->name },
                                     { QStringLiteral("value"), value } });
    }
    for (const auto &o : *path.options) {
        if (o.required && !given.contains(o.name)) {
            built.error = QStringLiteral("Missing %1 — %2").arg(o.name, usage(*command, text));
            return built;
        }
    }

    // Wrap in the subcommand path, innermost first
    QJsonArray wrapped = options;
    const QList<Option> *level = &command->options;
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

    built.data = QJsonObject {
        { QStringLiteral("version"), command->version },
        { QStringLiteral("id"), command->id },
        { QStringLiteral("name"), command->name },
        { QStringLiteral("type"), ChatInput },
        { QStringLiteral("options"), wrapped },
        { QStringLiteral("application_command"), command->raw },
        { QStringLiteral("attachments"), QJsonArray() },
    };
    built.ok = true;
    return built;
}

} // namespace SlashCommands
} // namespace Qord
