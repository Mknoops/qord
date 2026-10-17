#pragma once

#include "Permission.hpp"
#include "Settings.hpp"

#include <QJsonArray>
#include <QString>
#include <QStringList>

namespace Qord {
namespace Plugins {

// A slash command (Vencord's commands API, Discord's built-in commands).
// `options` are Discord's application command options (type, name,
// description, required, choices).
struct PluginCommand
{
    QString name;
    QString description;
    QJsonArray options;
};

// Parsed plugin.json. This file is the single source of truth for both the
// runtime and tools/check_equicord_plugins.py.
struct PluginManifest
{
    QString id;
    QString equicordName;
    QString equicordPath;
    QString name;
    QString description;
    QStringList categories;
    QStringList tags;
    QStringList authors;
    QStringList contributors; // Qord: who added to the port beyond upstream
    Permissions permissions;
    Hooks hooks;
    SettingsSchema settings;
    bool enabledByDefault = false;
    QString target;
    QStringList dependencies;
    QList<PluginCommand> commands;
    QString notes;
    // Upstream's note above the plugin's settings (Vencord's settingsAboutComponent text)
    QString settingsNote;
    QStringList gaps;
    // Upstream behavior aimed at a Discord feature Qord doesn't have at all
    QStringList notApplicable;

    // Parses and validates; on failure returns false and fills `error`.
    static bool parse(const QByteArray &json, PluginManifest &out, QString &error);
};

} // namespace Plugins
} // namespace Qord
