/*
 * Qord, a native Discord client
 * Port of Equicord's ShowConnections plugin (src/plugins/showConnections)
 * Original: Copyright (c) Vendicated and contributors, author TheKodeToad
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

#include <QHash>
#include <QStringList>

#include <functional>
#include <memory>

namespace Qord {
namespace Plugins {

class PluginContext;

// Connected accounts in profiles as a row of icons, with the user's GitHub
// organizations after their GitHub account.
class ShowConnectionsPlugin : public Plugin
{
public:
    // getSpacingPx: Compact 4, Cozy 6, Roomy 8
    static int spacingPx(int spacing) { return spacing * 2 + 4; }
    // Logins from GitHub's /users/{name}/orgs; none if it isn't a list
    static QStringList parseOrgs(const QByteArray &json);

    [[nodiscard]] Hooks hooks() const override { return {}; }
    void start(PluginContext &context) override;
    void stop() override;
    void onSettingChanged(const QString &key) override;

private:
    void apply();
    void githubOrgs(const QString &name, std::function<void(const QStringList &)> done);

    PluginContext *context = nullptr;
    QHash<QString, QStringList> orgs; // by GitHub name, for this run
    QHash<QString, QList<std::function<void(const QStringList &)>>> waiting;
    std::shared_ptr<bool> alive;
};

} // namespace Plugins
} // namespace Qord
