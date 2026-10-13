/*
 * Qord, a native Discord client
 * Port of Equicord's FavoriteEmojiFirst plugin (src/plugins/favEmojiFirst)
 * Original: Copyright (c) Vendicated and contributors, authors Aria, Ven, justjxke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "FavoriteEmojiFirst.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QRegularExpression>
#include <QSet>

namespace Qord {
namespace Plugins {

namespace {
const QString AliasesKey = QStringLiteral("aliases");
const QString SetAliasId = QStringLiteral("vc-emoji-alias-set");

FavoriteEmojiFirstPlugin::Ref refOf(const EmojiInfo &emoji)
{
    FavoriteEmojiFirstPlugin::Ref ref;
    ref.custom = emoji.customId != 0;
    ref.id = emoji.customId;
    ref.name = emoji.name;
    ref.surrogates = emoji.surrogates;
    ref.animated = emoji.animated;
    return ref;
}

bool sameEmoji(const FavoriteEmojiFirstPlugin::Ref &a, const FavoriteEmojiFirstPlugin::Ref &b)
{
    return a.key() == b.key() && a.custom == b.custom;
}
} // namespace

QString FavoriteEmojiFirstPlugin::Ref::displayName() const
{
    if (custom)
        return QStringLiteral(":%1:").arg(name);
    return surrogates.isEmpty() ? QStringLiteral(":%1:").arg(name) : surrogates;
}

QString FavoriteEmojiFirstPlugin::normalizeAlias(const QString &input)
{
    QString out = input.trimmed();
    while (out.startsWith(QLatin1Char(':')))
        out.remove(0, 1);
    while (out.endsWith(QLatin1Char(':')))
        out.chop(1);
    return out.toLower();
}

QString FavoriteEmojiFirstPlugin::validationError(const QString &input)
{
    const QString normalized = normalizeAlias(input);
    if (normalized.isEmpty())
        return QStringLiteral("Alias is required.");
    static const QRegularExpression valid(QStringLiteral("^[a-z0-9_]{2,32}$"));
    if (!valid.match(normalized).hasMatch())
        return QStringLiteral(
                "Alias must be 2-32 characters and only use lowercase letters, numbers, or underscores.");
    return {};
}

FavoriteEmojiFirstPlugin::AliasMap FavoriteEmojiFirstPlugin::aliases(const PluginSettings &settings)
{
    AliasMap out;
    static const QRegularExpression valid(QStringLiteral("^[a-z0-9_]{2,32}$"));
    const QJsonObject stored = settings.value(AliasesKey).toObject();
    for (auto it = stored.begin(); it != stored.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        Ref ref;
        ref.custom = o.value(QLatin1String("kind")).toString() == QLatin1String("custom");
        ref.id = o.value(QLatin1String("id")).toString().toULongLong();
        ref.name = o.value(QLatin1String("name")).toString();
        ref.surrogates = o.value(QLatin1String("surrogates")).toString();
        ref.animated = o.value(QLatin1String("animated")).toBool();
        // Like upstream, invalid entries are ignored
        if (!valid.match(it.key()).hasMatch() || ref.name.isEmpty() || (ref.custom && !ref.id))
            continue;
        out.insert(it.key(), ref);
    }
    return out;
}

void FavoriteEmojiFirstPlugin::storeAliases(PluginSettings &settings, const AliasMap &map)
{
    QJsonObject out;
    for (auto it = map.begin(); it != map.end(); ++it) {
        QJsonObject o { { "kind", it->custom ? "custom" : "unicode" }, { "name", it->name } };
        if (it->custom) {
            o.insert(QStringLiteral("id"), QString::number(it->id));
            o.insert(QStringLiteral("animated"), it->animated);
        } else if (!it->surrogates.isEmpty()) {
            o.insert(QStringLiteral("surrogates"), it->surrogates);
        }
        out.insert(it.key(), o);
    }
    settings.setValue(AliasesKey, out);
}

void FavoriteEmojiFirstPlugin::start(PluginContext &ctx)
{
    context = &ctx;
}

void FavoriteEmojiFirstPlugin::stop()
{
    context = nullptr;
}

// Upstream's sortEmojis: aliases equal to the query, then favorites (in the
// order they came), then aliases starting with it, then the rest
void FavoriteEmojiFirstPlugin::sortEmojiSuggestions(EmojiAutocompleteContext &ctx)
{
    if (!context)
        return;
    const QString query = normalizeAlias(ctx.query);
    if (query.isEmpty())
        return;

    QHash<QString, int> priorities; // emoji key -> 2 exact alias, 1 prefix
    const AliasMap map = aliases(context->settings());
    for (auto it = map.begin(); it != map.end(); ++it) {
        if (!it.key().startsWith(query))
            continue;
        const int priority = it.key() == query ? 2 : 1;
        if (priority > priorities.value(it->key()))
            priorities.insert(it->key(), priority);
    }

    // Upstream's injectAliasResults: aliased emojis missing from the
    // results go in front, exact aliases first
    QSet<QString> present;
    for (const auto &emoji : ctx.results)
        present.insert(emoji.key);
    QList<EmojiInfo> injectedExact, injectedPrefix;
    for (auto it = map.begin(); it != map.end(); ++it) {
        if (!it.key().startsWith(query) || present.contains(it->key()))
            continue;
        EmojiInfo info;
        info.key = it->key();
        info.name = it->name;
        info.customId = it->custom ? it->id : 0;
        info.animated = it->animated;
        info.surrogates = it->surrogates;
        present.insert(info.key);
        (it.key() == query ? injectedExact : injectedPrefix).append(info);
    }
    const QList<EmojiInfo> all = injectedExact + injectedPrefix + ctx.results;

    QList<EmojiInfo> exact, prefix, rest;
    QSet<QString> seen;
    for (const auto &emoji : all) {
        if (seen.contains(emoji.key))
            continue;
        seen.insert(emoji.key);
        const int priority = priorities.value(emoji.key);
        if (priority == 2 || (priority == 0 && emoji.favorite))
            exact.append(emoji);
        else if (priority == 1)
            prefix.append(emoji);
        else
            rest.append(emoji);
    }
    ctx.results = exact + prefix + rest;
}

QList<MenuItem> FavoriteEmojiFirstPlugin::emojiContextMenuItems(const EmojiContext &emoji)
{
    if (!context)
        return {};
    const Ref ref = refOf(emoji.emoji);
    const AliasMap map = aliases(context->settings());
    for (auto it = map.begin(); it != map.end(); ++it)
        if (sameEmoji(*it, ref))
            return { { SetAliasId, QStringLiteral("Edit alias"), {} } };
    return { { SetAliasId, QStringLiteral("Set alias"), {} } };
}

void FavoriteEmojiFirstPlugin::onEmojiContextMenuAction(const QString &id, const EmojiContext &emoji)
{
    UiService *ui = context ? context->ui() : nullptr;
    if (id != SetAliasId || !ui)
        return;
    const Ref ref = refOf(emoji.emoji);
    QString existing;
    const AliasMap map = aliases(context->settings());
    for (auto it = map.begin(); it != map.end(); ++it)
        if (sameEmoji(*it, ref))
            existing = it.key();
    ui->promptText(QStringLiteral("Set alias"), QStringLiteral("Alias for %1, e.g. \"sob\"").arg(ref.displayName()),
                   existing, 32, [this, ref](const QString &input) {
                       UiService *ui = context ? context->ui() : nullptr;
                       const QString error = saveAlias(input, ref);
                       if (!ui)
                           return;
                       if (error.isEmpty())
                           ui->showToast(QStringLiteral("Alias set for %1.").arg(ref.displayName()),
                                         UiService::ToastType::Success);
                       else
                           ui->showToast(error, UiService::ToastType::Failure);
                   });
}

QString FavoriteEmojiFirstPlugin::saveAlias(const QString &input, const Ref &ref)
{
    if (!context)
        return QStringLiteral("Failed to save alias.");
    if (const QString error = validationError(input); !error.isEmpty())
        return error;
    const QString alias = normalizeAlias(input);
    AliasMap map = aliases(context->settings());
    if (map.contains(alias) && !sameEmoji(map.value(alias), ref))
        return QStringLiteral("Duplicate alias");
    // One alias per emoji: the old one goes
    for (auto it = map.begin(); it != map.end();) {
        if (it.key() != alias && sameEmoji(*it, ref))
            it = map.erase(it);
        else
            ++it;
    }
    map.insert(alias, ref);
    storeAliases(context->settings(), map);
    return {};
}

// `:alias:` in a message becomes the emoji (upstream's messageSendListener)
void FavoriteEmojiFirstPlugin::onBeforeSend(OutgoingMessage &message)
{
    if (!context || message.content.isEmpty())
        return;
    const AliasMap map = aliases(context->settings());
    if (map.isEmpty())
        return;
    static const QRegularExpression pattern(QStringLiteral(":([a-z0-9_]{2,32}):"),
                                            QRegularExpression::CaseInsensitiveOption);
    QString out;
    qsizetype last = 0;
    auto it = pattern.globalMatch(message.content);
    while (it.hasNext()) {
        const auto match = it.next();
        out += message.content.mid(last, match.capturedStart() - last);
        last = match.capturedEnd();
        const auto ref = map.constFind(match.captured(1).toLower());
        if (ref == map.constEnd())
            out += match.captured();
        else if (ref->custom)
            out += QStringLiteral("<%1:%2:%3>").arg(ref->animated ? QStringLiteral("a") : QString(), ref->name,
                                                    QString::number(ref->id));
        else
            out += ref->surrogates.isEmpty() ? match.captured() : ref->surrogates;
    }
    out += message.content.mid(last);
    message.content = out;
}

QByteArray qord_manifest_favoriteemojifirst();

void qord_register_favoriteemojifirst(Registry &registry)
{
    registry.add({ QStringLiteral("favoriteemojifirst/plugin.json"),
                   [] { return std::make_unique<FavoriteEmojiFirstPlugin>(); }, qord_manifest_favoriteemojifirst() });
}

} // namespace Plugins
} // namespace Qord
