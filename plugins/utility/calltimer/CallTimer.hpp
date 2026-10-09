#pragma once

#include "Plugins/Plugin.hpp"

#include <QHash>

namespace Qord {
namespace Plugins {

namespace CallTimer {
// Equicord's formatDurationMs (src/utils/text.ts)
QString formatDurationMs(qint64 ms, bool human = false, bool seconds = true);
} // namespace CallTimer

class CallTimerPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::VoiceStates; }
    void start(PluginContext &ctx) override { context = &ctx; }
    void stop() override
    {
        context = nullptr;
        joinTimes.clear();
    }

    void onVoiceStateChanged(const VoiceStateChange &change) override;
    std::optional<VoiceLabel> voiceParticipantLabel(const VoiceParticipantContext &participant) override;
    std::optional<QString> voiceConnectionText(const VoiceConnectionContext &connection) override;

private:
    struct Join
    {
        Id channelId = 0;
        qint64 timeMs = 0;
    };
    static QString key(Id accountId, Id userId) { return QString::number(accountId) + QLatin1Char(':') + QString::number(userId); }

    PluginContext *context = nullptr;
    QHash<QString, Join> joinTimes; // userJoinTimes
};

} // namespace Plugins
} // namespace Qord
