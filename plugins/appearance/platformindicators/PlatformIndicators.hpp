#pragma once

#include "Plugins/Plugin.hpp"

namespace Qord {
namespace Plugins {

class DisplayOptionsService;

namespace PlatformIndicators {
// useEnsureOwnStatus(): the current user's client statuses from their
// sessions. Sessions are sorted so online ones come last (then idle), and
// each client keeps the status of its last session; "unknown" clients are
// skipped. Platforms keep the order they first appear in.
QList<QPair<QString, QString>> ownClientStatus(const QList<SessionInfo> &sessions);

struct Icon
{
    QString viewBox;
    int width = 20;
    int height = 20;
    QString path;
};
// Icons[platform] ?? Icons.desktop, with the chosen console icon for "embedded"
Icon iconFor(const QString &platform, const QString &consoleIcon);
// platformMap[platform] ?? Platform (capitalised)
QString tooltipFor(const QString &platform);
} // namespace PlatformIndicators

class PlatformIndicatorsPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::UserDecorations; }
    void start(PluginContext &ctx) override;
    void stop() override;
    void onSettingChanged(const QString &key) override;

    QList<NameDecoration> nameDecorations(const DecorationContext &user) override;

private:
    void applyMobileIndicator(bool on);

    PluginContext *context = nullptr;
    DisplayOptionsService *display = nullptr;
};

} // namespace Plugins
} // namespace Qord
