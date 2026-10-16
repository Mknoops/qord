#pragma once

#include "Permission.hpp"
#include "Settings.hpp"

#include <QString>
#include <QStringList>

namespace Qord {
namespace Plugins {

struct PluginCommand
{
    QString name;
    QString description;
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
    Permissions permissions;
    Hooks hooks;
    SettingsSchema settings;
    bool enabledByDefault = false;
    QString target;
    QStringList dependencies;
    QList<PluginCommand> commands;
    QString notes;
    QStringList gaps;
    // Upstream behavior aimed at a Discord feature Qord doesn't have at all
    QStringList notApplicable;

    // Parses and validates; on failure returns false and fills `error`.
    static bool parse(const QByteArray &json, PluginManifest &out, QString &error);
};

} // namespace Plugins
} // namespace Qord
