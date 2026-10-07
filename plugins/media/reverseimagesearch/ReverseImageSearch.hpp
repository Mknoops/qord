/*
 * Qord, a native Discord client
 * Port of Equicord's ReverseImageSearch plugin (src/plugins/reverseImageSearch)
 * Original: Copyright (c) Vendicated and contributors, authors Ven, Nuckyz
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

#include <QUrl>

namespace Qord {
namespace Plugins {

class PluginContext;

namespace ReverseImageSearch {
struct Engine
{
    QString name;
    QString prefix;
};
const QList<Engine> &engines();
// engine + encodeURIComponent(src)
QUrl searchUrl(const Engine &engine, const QString &imageUrl);
} // namespace ReverseImageSearch

// "Search Image" submenu on images, opening the image in a search engine.
class ReverseImageSearchPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::MessageContextMenu; }
    void start(PluginContext &context) override { this->context = &context; }
    void stop() override { context = nullptr; }
    QList<MenuItem> messageContextMenuItems(const MessageContext &message) override;
    void onMessageContextMenuAction(const QString &id, const MessageContext &message) override;

private:
    PluginContext *context = nullptr;
};

} // namespace Plugins
} // namespace Qord
