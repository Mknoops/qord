/*
 * Qord, a native Discord client
 * Port of Equicord's ImageZoom plugin (src/plugins/imageZoom)
 * Original: Copyright (c) Vendicated and contributors, author Aria
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Plugins/Plugin.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <memory>

namespace Qord {
namespace Plugins {

namespace {
const QString SquareId = QStringLiteral("vc-square");
const QString NearestId = QStringLiteral("vc-nearest-neighbour");
} // namespace

// A magnifying lens in the image viewer: hold the left button over the
// picture; the wheel zooms the lens, Shift+wheel resizes it.
class ImageZoomPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::MessageContextMenu; }

    void start(PluginContext &ctx) override
    {
        context = &ctx;
        options = ctx.displayOptions();
        alive = std::make_shared<bool>(true);
        apply();
    }
    void stop() override
    {
        if (alive)
            *alive = false;
        if (options)
            options->setImageLens(std::nullopt, {});
        options = nullptr;
        context = nullptr;
    }
    void onSettingChanged(const QString &key) override
    {
        Q_UNUSED(key);
        apply();
    }

    // Upstream's image menu: the two switches (its sliders are in settings)
    QList<MenuItem> messageContextMenuItems(const MessageContext &message) override
    {
        if (!context || message.imageUrl.isEmpty())
            return {};
        const PluginSettings &settings = context->settings();
        MenuItem square { SquareId, QStringLiteral("Square Lens"), {} };
        square.checked = settings.boolValue(QStringLiteral("square"));
        MenuItem nearest { NearestId, QStringLiteral("Nearest Neighbour"), {} };
        nearest.checked = settings.boolValue(QStringLiteral("nearestNeighbour"));
        return { square, nearest };
    }
    void onMessageContextMenuAction(const QString &id, const MessageContext &message) override
    {
        Q_UNUSED(message);
        if (!context)
            return;
        const QString key = id == SquareId ? QStringLiteral("square") : id == NearestId ? QStringLiteral("nearestNeighbour")
                                                                                         : QString();
        if (!key.isEmpty())
            context->settings().setValue(key, !context->settings().boolValue(key));
    }

private:
    void apply()
    {
        if (!options || !context)
            return;
        const PluginSettings &settings = context->settings();
        ImageLens lens;
        lens.zoom = settings.numberValue(QStringLiteral("zoom"));
        lens.size = int(settings.numberValue(QStringLiteral("size")));
        lens.zoomSpeed = settings.numberValue(QStringLiteral("zoomSpeed"));
        lens.square = settings.boolValue(QStringLiteral("square"));
        lens.nearestNeighbour = settings.boolValue(QStringLiteral("nearestNeighbour"));
        lens.invertScroll = settings.boolValue(QStringLiteral("invertScroll"));
        std::weak_ptr<bool> weak = alive;
        options->setImageLens(lens, [this, weak](double zoom, int size) {
            if (auto a = weak.lock(); !a || !*a || !context)
                return;
            // "Whether to save zoom and lens size values"
            if (!context->settings().boolValue(QStringLiteral("saveZoomValues")))
                return;
            context->settings().setValue(QStringLiteral("zoom"), qMin(zoom, 50.0));
            context->settings().setValue(QStringLiteral("size"), double(qMin(size, 1000)));
        });
    }

    PluginContext *context = nullptr;
    DisplayOptionsService *options = nullptr;
    std::shared_ptr<bool> alive;
};

QByteArray qord_manifest_imagezoom();

void qord_register_imagezoom(Registry &registry)
{
    registry.add({ QStringLiteral("imagezoom/plugin.json"), [] { return std::make_unique<ImageZoomPlugin>(); },
                   qord_manifest_imagezoom() });
}

} // namespace Plugins
} // namespace Qord
