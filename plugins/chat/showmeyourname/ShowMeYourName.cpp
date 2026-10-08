/*
 * Qord, a native Discord client
 * Port of Equicord's ShowMeYourName plugin (src/plugins/showMeYourName)
 * Original: Copyright (c) Vendicated and contributors,
 * authors Etorix, rini, TheKodeToad, sadan, prism, penguinwokrs
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ShowMeYourName.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QJsonObject>
#include <QRegularExpression>

#include <array>
#include <optional>

namespace Qord {
namespace Plugins {

namespace ShowMeYourName {

namespace {

const QRegularExpression &templatePattern()
{
    static const QRegularExpression re(
            QStringLiteral(R"((?:\{(?:custom|friend|nick|display|user)(?:,\s*(?:custom|friend|nick|display|user))*\}))"),
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    return re;
}

const QStringList &knownNames()
{
    static const QStringList names { "custom", "friend", "nick", "display", "user" };
    return names;
}

bool isJsSpace(QChar c)
{
    return c.isSpace() || c == QChar(0xFEFF);
}

// String.prototype.replace(string, "") replaces the first occurrence only
QString removeFirst(QString s, const QString &what)
{
    const qsizetype i = s.indexOf(what);
    if (i >= 0)
        s.remove(i, what.size());
    return s;
}

} // namespace

QStringList splitTemplate(const QString &templ)
{
    // template.trim().split(/(?<!,\s*)\s+/): a whitespace run splits unless
    // the text before it ends with "," plus optional whitespace
    const QString t = templ.trimmed();
    QStringList out;
    QString current;
    qsizetype i = 0;
    while (i < t.size()) {
        if (!isJsSpace(t.at(i))) {
            current += t.at(i++);
            continue;
        }
        qsizetype j = i;
        while (j < t.size() && isJsSpace(t.at(j)))
            ++j;
        const bool afterComma = !current.isEmpty() && current.back() == QLatin1Char(',');
        if (afterComma)
            current += t.mid(i, j - i);
        else {
            out.append(current);
            current.clear();
        }
        i = j;
    }
    out.append(current);
    return out;
}

TemplateItem parseTemplateItem(const QString &entry)
{
    // const [prefix, suffix] = entry.split(templatePattern)
    QString prefix;
    std::optional<QString> suffix;
    const auto first = templatePattern().match(entry);
    if (!first.hasMatch()) {
        prefix = entry;
    } else {
        prefix = entry.left(first.capturedStart());
        const auto second = templatePattern().match(entry, first.capturedEnd());
        suffix = second.hasMatch() ? entry.mid(first.capturedEnd(), second.capturedStart() - first.capturedEnd())
                                   : entry.mid(first.capturedEnd());
    }
    QString rest = removeFirst(entry, prefix);
    if (suffix)
        rest = removeFirst(rest, *suffix);
    rest = rest.trimmed();
    rest.remove(QLatin1Char('{'));
    rest.remove(QLatin1Char('}'));

    TemplateItem item;
    item.prefix = prefix.trimmed();
    item.suffix = suffix ? suffix->trimmed() : QString();
    static const QRegularExpression comma(QStringLiteral(R"(,\s*)"));
    for (const QString &name : rest.split(comma)) {
        const QString n = name.trimmed();
        if (!n.isEmpty())
            item.names.append(n);
    }
    return item;
}

bool validTemplate(const QString &templ)
{
    const QStringList items = splitTemplate(templ);
    if (items.size() > 4 || items.isEmpty())
        return false;
    static const QRegularExpression symbol(QStringLiteral(R"(^[\p{S}\p{P}]{1,3}$)"),
                                           QRegularExpression::CaseInsensitiveOption
                                                   | QRegularExpression::UseUnicodePropertiesOption);
    for (const QString &entry : items) {
        const TemplateItem item = parseTemplateItem(entry);
        if (item.prefix.size() > 3 || item.suffix.size() > 3)
            return false;
        if (!item.prefix.isEmpty() && !symbol.match(item.prefix).hasMatch())
            return false;
        if (!item.suffix.isEmpty() && !symbol.match(item.suffix).hasMatch())
            return false;
        if (item.names.size() > 5)
            return false;
        for (const QString &n : item.names)
            if (!knownNames().contains(n.trimmed()))
                return false;
    }
    return true;
}

Names processNames(const AuthorContext &author, const QString &custom, const Options &options)
{
    Names n;
    QString globalName = author.globalName;
    QString discriminator;
    if (options.discriminators) {
        bool numeric = false;
        const int value = author.discriminator.toInt(&numeric);
        if (author.isBot && numeric && value != 0) {
            discriminator = author.discriminator;
            globalName = author.username; // upstream sets globalName = username for such bots
        }
    }
    const bool inGuild = author.guildId != 0;
    n.username = author.username.isEmpty() ? QString()
                                           : author.username + (discriminator.isEmpty() ? QString() : QLatin1Char('#') + discriminator);
    n.display = globalName;
    n.nick = author.nick;
    if (!(inGuild && options.friendNameOnlyInDirectMessages))
        n.friendName = author.friendNickname;
    if (!(inGuild && options.customNameOnlyInDirectMessages))
        n.custom = custom;
    return n;
}

QString compose(const Names &names, const Options &options)
{
    if (names.username.isEmpty())
        return {};

    struct Output
    {
        QString prefix;
        QString name;
        QString type;
        QString suffix;
    };
    const auto value = [&](const QString &type) -> QString {
        if (type == QLatin1String("user"))
            return names.username;
        if (type == QLatin1String("display"))
            return names.display;
        if (type == QLatin1String("nick"))
            return names.nick;
        if (type == QLatin1String("friend"))
            return names.friendName;
        if (type == QLatin1String("custom"))
            return names.custom;
        return {};
    };

    QList<Output> outputs;
    for (const QString &entry : splitTemplate(options.includedNames)) {
        const TemplateItem item = parseTemplateItem(entry);
        QString chosen;
        QString chosenType;
        for (const QString &name : item.names) {
            if (!knownNames().contains(name))
                continue;
            chosen = value(name);
            chosenType = name;
            if (!chosen.isEmpty())
                break;
        }
        if (chosen.isEmpty())
            continue;
        outputs.append({ item.prefix, chosen, chosenType, item.suffix });
    }
    if (outputs.isEmpty())
        outputs.append({ QString(), names.username, QStringLiteral("user"), QString() });

    // Five positions, like upstream (more template items are dropped)
    std::array<std::optional<Output>, 5> pos;
    for (int i = 0; i < 5 && i < outputs.size(); ++i)
        pos[i] = outputs[i];

    if (options.removeDuplicates) {
        QSet<QString> unique;
        for (const auto &p : pos)
            if (p)
                unique.insert(p->name.toLower());
        const bool prioritizeUsername = unique.size() > 1;
        // From back to front; keep the earlier name unless the later one is
        // the username and there is more than one unique name
        static const std::array<std::pair<int, int>, 10> order { { { 4, 3 }, { 4, 2 }, { 4, 1 }, { 4, 0 }, { 3, 2 },
                                                                    { 3, 1 }, { 3, 0 }, { 2, 1 }, { 2, 0 }, { 1, 0 } } };
        for (const auto &[later, earlier] : order) {
            if (pos[later] && pos[earlier] && pos[later]->name.toLower() == pos[earlier]->name.toLower()) {
                if (pos[later]->type == QLatin1String("user") && prioritizeUsername)
                    pos[earlier].reset();
                else
                    pos[later].reset();
            }
        }
    }

    QString text;
    bool first = true;
    for (const auto &p : pos) {
        if (!p)
            continue;
        if (first) {
            text = p->name;
            first = false;
        } else {
            text += options.nameSeparator + p->prefix + p->name + p->suffix;
        }
    }
    return text;
}

} // namespace ShowMeYourName

void ShowMeYourNamePlugin::start(PluginContext &ctx)
{
    context = &ctx;
    load();
}

void ShowMeYourNamePlugin::onSettingChanged(const QString &key)
{
    Q_UNUSED(key);
    load();
}

void ShowMeYourNamePlugin::load()
{
    auto &s = context->settings();
    options.includedNames = s.stringValue(QStringLiteral("includedNames"));
    // An invalid template is rejected by upstream's settings UI; fall back to the default
    if (!ShowMeYourName::validTemplate(options.includedNames))
        options.includedNames = ShowMeYourName::Options().includedNames;
    options.nameSeparator = s.stringValue(QStringLiteral("nameSeparator"));
    options.removeDuplicates = s.boolValue(QStringLiteral("removeDuplicates"));
    options.discriminators = s.boolValue(QStringLiteral("discriminators"));
    options.friendNameOnlyInDirectMessages = s.boolValue(QStringLiteral("friendNameOnlyInDirectMessages"));
    options.customNameOnlyInDirectMessages = s.boolValue(QStringLiteral("customNameOnlyInDirectMessages"));
    customNicknames.clear();
    const QJsonObject stored = s.value(QStringLiteral("customNicknames")).toObject();
    for (auto it = stored.begin(); it != stored.end(); ++it)
        customNicknames.insert(it.key().toULongLong(), it.value().toString());
}

std::optional<QString> ShowMeYourNamePlugin::authorDisplayName(const AuthorContext &author)
{
    const bool reply = author.place == AuthorContext::Place::Reply;
    if (!context->settings().boolValue(reply ? QStringLiteral("replies") : QStringLiteral("messages")))
        return std::nullopt;
    const QString text = ShowMeYourName::compose(
            ShowMeYourName::processNames(author, customNicknames.value(author.userId), options), options);
    if (text.isEmpty())
        return std::nullopt;
    return text;
}

QList<MenuItem> ShowMeYourNamePlugin::userContextMenuItems(const UserContext &user)
{
    if (!user.userId || user.userId == user.accountId)
        return {};
    return { { QStringLiteral("smyn-custom-nickname"),
               customNicknames.contains(user.userId) ? QStringLiteral("Change SMYN Nickname")
                                                     : QStringLiteral("Add SMYN Nickname"),
               {} } };
}

void ShowMeYourNamePlugin::onUserContextMenuAction(const QString &id, const UserContext &user)
{
    if (id != QLatin1String("smyn-custom-nickname"))
        return;
    UiService *ui = context->ui();
    if (!ui)
        return;
    const Id userId = user.userId;
    ui->promptText(customNicknames.contains(userId) ? QStringLiteral("Change SMYN Nickname")
                                                    : QStringLiteral("Add SMYN Nickname"),
                   QStringLiteral("Set a custom SMYN nickname for this user. Make use of it by specifying {custom} in "
                                  "the SMYN template settings. Leave it empty to reset it."),
                   customNicknames.value(userId), 32, [this, userId](const QString &text) {
                       // upstream: value.trim().slice(0, 32).trim(); empty deletes
                       const QString trimmed = text.trimmed().left(32).trimmed();
                       QJsonObject stored = context->settings().value(QStringLiteral("customNicknames")).toObject();
                       if (trimmed.isEmpty())
                           stored.remove(QString::number(userId));
                       else
                           stored.insert(QString::number(userId), trimmed);
                       context->settings().setValue(QStringLiteral("customNicknames"), stored);
                   });
}

QByteArray qord_manifest_showmeyourname();

void qord_register_showmeyourname(Registry &registry)
{
    registry.add({ QStringLiteral("showmeyourname/plugin.json"), [] { return std::make_unique<ShowMeYourNamePlugin>(); },
                   qord_manifest_showmeyourname() });
}

} // namespace Plugins
} // namespace Qord
