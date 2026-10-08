/*
 * Qord, a native Discord client
 * Port of Equicord's ShowMeYourName plugin (src/plugins/showMeYourName)
 * Original: Copyright (c) Vendicated and contributors,
 * authors Etorix, rini, TheKodeToad, sadan, prism, penguinwokrs
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

#include <QHash>
#include <QStringList>

namespace Qord {
namespace Plugins {

namespace ShowMeYourName {

struct TemplateItem
{
    QString prefix;
    QString suffix;
    QStringList names; // custom, friend, nick, display, user
};

// upstream splitTemplate(): split on whitespace not preceded by a comma
QStringList splitTemplate(const QString &templ);
// upstream parseTemplateItem()
TemplateItem parseTemplateItem(const QString &entry);
// upstream validTemplate()
bool validTemplate(const QString &templ);

struct Names
{
    QString username;
    QString display;
    QString nick;
    QString friendName;
    QString custom;
};

struct Options
{
    QString includedNames = QStringLiteral("{custom, friend, nick} [{display}] (@{user})");
    QString nameSeparator = QStringLiteral(" ");
    bool removeDuplicates = true;
    bool discriminators = true;
    bool friendNameOnlyInDirectMessages = false;
    bool customNameOnlyInDirectMessages = false;
};

// upstream getProcessedNames() (Qord has no Streamer Mode)
Names processNames(const AuthorContext &author, const QString &custom, const Options &options);

// The text upstream's name element reads as: the first name, then each
// further one as separator + prefix + name + suffix. Empty if there is no
// username (the client's own name is used then).
QString compose(const Names &names, const Options &options);

} // namespace ShowMeYourName

class ShowMeYourNamePlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::AuthorNames | Hook::UserContextMenu; }

    void start(PluginContext &context) override;
    void onSettingChanged(const QString &key) override;

    std::optional<QString> authorDisplayName(const AuthorContext &author) override;
    QList<MenuItem> userContextMenuItems(const UserContext &user) override;
    void onUserContextMenuAction(const QString &id, const UserContext &user) override;

private:
    void load();

    PluginContext *context = nullptr;
    ShowMeYourName::Options options;
    QHash<Id, QString> customNicknames;
};

} // namespace Plugins
} // namespace Qord
