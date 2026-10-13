/*
 * Qord, a native Discord client
 * Port of Equicord's ShowConnections plugin (src/plugins/showConnections)
 * Original: Copyright (c) Vendicated and contributors, author TheKodeToad
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ShowConnections.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace Qord {
namespace Plugins {

QStringList ShowConnectionsPlugin::parseOrgs(const QByteArray &json)
{
    QStringList out;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isArray())
        return out;
    for (const QJsonValue &org : doc.array()) {
        const QString login = org.toObject().value(QStringLiteral("login")).toString();
        if (!login.isEmpty())
            out.append(login);
    }
    return out;
}

void ShowConnectionsPlugin::start(PluginContext &ctx)
{
    context = &ctx;
    alive = std::make_shared<bool>(true);
    apply();
}

void ShowConnectionsPlugin::stop()
{
    if (context && context->displayOptions())
        context->displayOptions()->setCompactConnections(std::nullopt, {});
    if (alive)
        *alive = false;
    alive.reset();
    waiting.clear();
    context = nullptr;
}

void ShowConnectionsPlugin::onSettingChanged(const QString &key)
{
    Q_UNUSED(key);
    apply();
}

void ShowConnectionsPlugin::apply()
{
    DisplayOptionsService *display = context ? context->displayOptions() : nullptr;
    if (!display)
        return;
    auto &s = context->settings();
    CompactConnections options;
    options.iconSize = s.intValue(QStringLiteral("iconSize"));
    options.spacing = spacingPx(s.stringValue(QStringLiteral("iconSpacing")).toInt());
    DisplayOptionsService::GithubOrgsLookup lookup;
    if (s.boolValue(QStringLiteral("showGithubOrgs")) && context->network()) {
        std::weak_ptr<bool> weak = alive;
        lookup = [this, weak](const QString &name, std::function<void(const QStringList &)> done) {
            if (weak.expired())
                return;
            githubOrgs(name, std::move(done));
        };
    }
    display->setCompactConnections(options, std::move(lookup));
}

void ShowConnectionsPlugin::githubOrgs(const QString &name, std::function<void(const QStringList &)> done)
{
    const QString key = name.toLower();
    if (const auto it = orgs.constFind(key); it != orgs.constEnd()) {
        done(*it);
        return;
    }
    NetworkService *network = context ? context->network() : nullptr;
    if (!network)
        return;
    const bool asked = waiting.contains(key);
    waiting[key].append(std::move(done));
    if (asked)
        return;
    const QUrl url(QStringLiteral("https://api.github.com/users/%1/orgs")
                           .arg(QString::fromUtf8(QUrl::toPercentEncoding(name))));
    std::weak_ptr<bool> weak = alive;
    network->get(url, [this, weak, key](const NetworkService::Reply &reply) {
        if (weak.expired())
            return;
        // Upstream shows none when the request fails
        const QStringList logins = reply.ok ? parseOrgs(reply.body) : QStringList();
        if (reply.ok)
            orgs.insert(key, logins);
        const auto callbacks = waiting.take(key);
        for (const auto &callback : callbacks)
            callback(logins);
    });
}

QByteArray qord_manifest_showconnections();

void qord_register_showconnections(Registry &registry)
{
    registry.add({ QStringLiteral("showconnections/plugin.json"),
                   [] { return std::make_unique<ShowConnectionsPlugin>(); }, qord_manifest_showconnections() });
}

} // namespace Plugins
} // namespace Qord
