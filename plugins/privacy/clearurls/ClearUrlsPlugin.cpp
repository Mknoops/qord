/*
 * Qord, a native Discord client
 * Port of Equicord's ClearURLs plugin (src/plugins/clearURLs)
 * Original: Copyright (c) 2022 Vendicated and contributors, authors adryd and thororen
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ClearUrlsPlugin.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace Qord {
namespace Plugins {

void ClearUrlsPlugin::start(PluginContext &ctx)
{
    context = &ctx;
    const QString cachePath = QDir(ctx.dataDirectory()).filePath(QStringLiteral("data.min.json"));

    QFile cache(cachePath);
    if (cache.open(QIODevice::ReadOnly)) {
        QString error;
        if (!rules.load(cache.readAll(), &error))
            ctx.logWarning(QStringLiteral("Ignoring unreadable rules cache: %1").arg(error));
    }

    const QDateTime modified = QFileInfo(cachePath).lastModified();
    const bool stale = rules.isEmpty() || !modified.isValid() ||
                       modified.secsTo(QDateTime::currentDateTime()) > RefreshAfterSeconds;
    if (!stale)
        return;

    NetworkService *network = ctx.network();
    if (!network) {
        if (rules.isEmpty())
            ctx.logWarning(QStringLiteral("No cached rules and no network permission; URLs are left as they are"));
        return;
    }
    request = network->get(QUrl(QString::fromLatin1(ClearUrls::RulesUrl)),
                           [this, cachePath](const NetworkService::Reply &reply) {
                               request = 0;
                               if (!reply.ok) {
                                   context->logWarning(QStringLiteral("Could not download rules: %1").arg(reply.error));
                                   return;
                               }
                               ClearUrls::RuleSet fresh;
                               QString error;
                               if (!fresh.load(reply.body, &error)) {
                                   context->logWarning(QStringLiteral("Downloaded rules are invalid: %1").arg(error));
                                   return;
                               }
                               rules = std::move(fresh);
                               QSaveFile out(cachePath);
                               if (out.open(QIODevice::WriteOnly)) {
                                   out.write(reply.body);
                                   out.commit();
                               }
                               context->logInfo(QStringLiteral("Loaded %1 providers").arg(rules.providerCount()));
                           });
}

void ClearUrlsPlugin::stop()
{
    if (request && context && context->network())
        context->network()->cancel(request);
    request = 0;
    rules = {};
    context = nullptr;
}

void ClearUrlsPlugin::onBeforeSend(OutgoingMessage &message)
{
    message.content = rules.cleanMessage(message.content);
}

void ClearUrlsPlugin::onBeforeEdit(OutgoingMessage &message)
{
    message.content = rules.cleanMessage(message.content);
}

QByteArray qord_manifest_clearurls();

void qord_register_clearurls(Registry &registry)
{
    registry.add({ QStringLiteral("clearurls/plugin.json"),
                   [] { return std::make_unique<ClearUrlsPlugin>(); }, qord_manifest_clearurls() });
}

} // namespace Plugins
} // namespace Qord
