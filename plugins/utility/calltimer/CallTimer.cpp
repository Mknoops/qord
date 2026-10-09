/*
 * Qord, a native Discord client
 * Port of Equicord's CallTimer plugin (src/plugins/callTimer)
 * Original: Copyright (c) Vendicated and contributors, authors Ven, MaxHerbold, D3SOX
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "CallTimer.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {
namespace Plugins {

QString CallTimer::formatDurationMs(qint64 ms, bool human, bool seconds)
{
    const auto format = [human](qint64 n) {
        return human ? QString::number(n) : QStringLiteral("%1").arg(n, 2, 10, QLatin1Char('0'));
    };
    const auto unit = [human](const char *s) { return human ? QString::fromLatin1(s) : QString(); };
    const QString delim = human ? QStringLiteral(" ") : QStringLiteral(":");

    const qint64 d = ms / 86400000;
    const qint64 h = (ms % 86400000) / 3600000;
    const qint64 m = ((ms % 86400000) % 3600000) / 60000;
    const qint64 s = (((ms % 86400000) % 3600000) % 60000) / 1000;

    QString res;
    if (d)
        res += QString::number(d) + unit("d") + delim;
    if (h || !res.isEmpty() || !seconds)
        res += format(h) + unit("h") + delim;
    if (m || !res.isEmpty() || !human || !seconds)
        res += format(m) + unit("m");
    if (seconds && (m || !res.isEmpty() || !human))
        res += delim;
    if (seconds)
        res += format(s) + unit("s");
    return res;
}

// VOICE_STATE_UPDATES: a join or move starts the timer, a leave drops it
void CallTimerPlugin::onVoiceStateChanged(const VoiceStateChange &change)
{
    const QString k = key(change.accountId, change.userId);
    if (!change.channelId) {
        joinTimes.remove(k);
        return;
    }
    auto it = joinTimes.constFind(k);
    if (it != joinTimes.constEnd() && it->channelId == change.channelId)
        return;
    joinTimes.insert(k, { change.channelId, change.atMs });
}

// renderTimer(): only with allCallTimers, and for yourself only with trackSelf
std::optional<VoiceLabel> CallTimerPlugin::voiceParticipantLabel(const VoiceParticipantContext &participant)
{
    if (!context || !context->settings().boolValue(QStringLiteral("allCallTimers")))
        return std::nullopt;
    auto it = joinTimes.constFind(key(participant.accountId, participant.userId));
    if (it == joinTimes.constEnd() || !it->timeMs)
        return std::nullopt;
    if (participant.isSelf && !context->settings().boolValue(QStringLiteral("trackSelf")))
        return std::nullopt;
    auto &s = context->settings();
    VoiceLabel label;
    label.text = CallTimer::formatDurationMs(participant.nowMs - it->timeMs,
                                             s.stringValue(QStringLiteral("format")) == QLatin1String("human"),
                                             s.boolValue(QStringLiteral("showSeconds")));
    label.inlineText = s.boolValue(QStringLiteral("showWithoutHover"));
    return label;
}

// ConnectionTimer: always with seconds
std::optional<QString> CallTimerPlugin::voiceConnectionText(const VoiceConnectionContext &connection)
{
    if (!context)
        return std::nullopt;
    auto it = joinTimes.constFind(key(connection.accountId, connection.userId));
    if (it == joinTimes.constEnd())
        return std::nullopt;
    return CallTimer::formatDurationMs(connection.nowMs - it->timeMs,
                                       context->settings().stringValue(QStringLiteral("format")) ==
                                               QLatin1String("human"));
}

QByteArray qord_manifest_calltimer();

void qord_register_calltimer(Registry &registry)
{
    registry.add({ QStringLiteral("calltimer/plugin.json"), [] { return std::make_unique<CallTimerPlugin>(); },
                   qord_manifest_calltimer() });
}

} // namespace Plugins
} // namespace Qord
