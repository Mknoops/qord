/*
 * Qord, a native Discord client
 * Port of Equicord's FavoriteEmojiFirst plugin (src/plugins/favEmojiFirst)
 * Original: Copyright (c) Vendicated and contributors, authors Aria, Ven, justjxke
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

#include <QJsonObject>
#include <QMap>

namespace Qord {
namespace Plugins {

class PluginContext;
class PluginSettings;

// Favorite emojis first in the emoji autocomplete, and aliases: your own
// names for emojis, suggested and sent as the emoji.
class FavoriteEmojiFirstPlugin : public Plugin
{
public:
    // An aliased emoji, as upstream stores it
    struct Ref
    {
        bool custom = false;
        Id id = 0;
        QString name;
        QString surrogates;
        bool animated = false;
        [[nodiscard]] QString key() const { return custom ? QString::number(id) : name; }
        [[nodiscard]] QString displayName() const;
    };
    using AliasMap = QMap<QString, Ref>; // sorted by alias

    static QString normalizeAlias(const QString &input);
    static QString validationError(const QString &input);
    static AliasMap aliases(const PluginSettings &settings);
    static void storeAliases(PluginSettings &settings, const AliasMap &map);

    [[nodiscard]] Hooks hooks() const override { return Hook::EmojiSuggestions | Hook::BeforeSend; }
    void start(PluginContext &context) override;
    void stop() override;

    void sortEmojiSuggestions(EmojiAutocompleteContext &context) override;
    QList<MenuItem> emojiContextMenuItems(const EmojiContext &emoji) override;
    void onEmojiContextMenuAction(const QString &id, const EmojiContext &emoji) override;
    void onBeforeSend(OutgoingMessage &message) override;

    // Upstream's saveAlias: an error message, or empty when saved
    QString saveAlias(const QString &input, const Ref &ref);

private:
    PluginContext *context = nullptr;
};

} // namespace Plugins
} // namespace Qord
