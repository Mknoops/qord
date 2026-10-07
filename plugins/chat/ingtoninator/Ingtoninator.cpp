/*
 * Qord, a native Discord client
 * Port of Equicord's Ingtoninator plugin (src/equicordplugins/ingtoninator)
 * Original: Copyright (c) Vendicated and contributors, authors zyqunix, BioTomateDE
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Ingtoninator.hpp"

#include "Plugins/JsCompat.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>

#include <cmath>

namespace Qord {
namespace Plugins {
namespace Ingtoninator {

namespace {
struct WordMatch
{
    QString word;
    qsizetype start = 0;
};

bool isLegal(const QString &word)
{
    if (word == QLatin1String("i") || word == QLatin1String("I"))
        return false;
    const QString last = word.right(1).toLower();
    return !(last == QLatin1String("a") || last == QLatin1String("e") || last == QLatin1String("o") ||
             last == QLatin1String("u") || last == QLatin1String("y"));
}

QList<WordMatch> words(const QString &s)
{
    static const QRegularExpression link(QStringLiteral("https?:\\/\\/[^%1]+\\.[^%1]+").arg(QLatin1String(Js::WhitespaceClass)));
    static const QRegularExpression letters(QStringLiteral("\\p{L}+"));
    QSet<qsizetype> linkRanges;
    for (auto it = link.globalMatch(s); it.hasNext();) {
        const auto m = it.next();
        for (qsizetype i = m.capturedStart(); i < m.capturedEnd(); ++i)
            linkRanges.insert(i);
    }
    QList<WordMatch> out;
    for (auto it = letters.globalMatch(s); it.hasNext();) {
        const auto m = it.next();
        if (!linkRanges.contains(m.capturedStart()))
            out.append({ m.captured(), m.capturedStart() });
    }
    return out;
}
} // namespace

QString ington(const QString &w)
{
    if (w.endsWith(QLatin1String("INGTON")))
        return {};
    if (w.endsWith(QLatin1String("INGTO")))
        return QStringLiteral("N");
    if (w.endsWith(QLatin1String("INGT")))
        return QStringLiteral("ON");
    if (w.endsWith(QLatin1String("ING")))
        return QStringLiteral("TON");
    if (w.endsWith(QLatin1String("IN")))
        return QStringLiteral("GTON");
    if (w.endsWith(QLatin1String("I")))
        return QStringLiteral("NGTON");
    return QStringLiteral("INGTON");
}

QString apply(const QString &msg, const Random &random)
{
    if (msg.isEmpty() || Js::trim(msg).isEmpty())
        return msg;

    // chooseRandomWord(): pick at random, discarding illegal picks
    QList<WordMatch> candidates = words(msg);
    std::optional<WordMatch> chosen;
    while (!candidates.isEmpty()) {
        const qsizetype index = qsizetype(std::floor(random() * double(candidates.size())));
        if (!isLegal(candidates.at(index).word)) {
            candidates.removeAt(index);
            continue;
        }
        chosen = candidates.at(index);
        break;
    }
    if (!chosen)
        return msg;

    const QString upper = chosen->word.toUpper();
    QString insertion = ington(upper);
    if (chosen->word != upper)
        insertion = insertion.toLower();
    const qsizetype at = chosen->start + chosen->word.size();
    return msg.left(at) + insertion + msg.mid(at);
}

} // namespace Ingtoninator

namespace {
QByteArray icon(bool enabled)
{
    // Upstream's leaf icon; red when the plugin is switched off
    const QByteArray fill = enabled ? "currentColor" : "#f23f43";
    return "<svg width=\"20\" height=\"20\" xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 256 256\">"
           "<path transform=\"translate(351 -153)\" fill=\"" + fill + "\" d=\"M-177.7,334.5c6.3-2.3,12.6-5.2,19.8-8.6c31.9-16.4,51.7-41.7,51.7-41.7s-32.5,0.6-64.4,17 c-4,1.7-7.5,4-10.9,5.7c5.7-7.5,12.1-16.4,18.7-25c25-37.1,31.3-77.3,31.3-77.3s-34.8,21-59.2,58.6c-5.2,7.5-9.8,14.9-13.8,22.7 c1.1-10.3,1.1-22.1,1.1-33.6c0-50-19.8-91.1-19.8-91.1s-19.8,40.5-19.8,91.1c0,12.1,0.6,23.3,1.1,33.6c-4-7.5-8.6-14.9-13.8-22.7 c-25-37.1-59.2-58.6-59.2-58.6s6.3,40,31.3,77.3c6.3,9.2,12.1,17.5,18.7,25c-3.4-2.3-7.5-4-10.9-5.7c-31.9-16.4-64.4-17-64.4-17 s19.8,25.6,51.7,41.7c6.9,3.4,13.2,6.3,19.8,8.6c-4,0.6-8,1.1-12.1,2.3c-30.5,6.4-53.2,23.9-53.2,23.9s27.3,7.5,58.6,1.1 c9.8-2.3,19.8-4.6,27.3-7.5c-1.1,1.1,15.8-8.6,21.6-14.4v60.4h8.6v-61.8c6.3,6.3,22.7,16.4,22.1,14.9c8,2.9,17.5,5.2,27.3,7.5 c30.8,6.3,58.6-1.1,58.6-1.1s-22.1-17.5-53.4-23.8C-169.6,335.7-173.7,335.1-177.7,334.5z\"/></svg>";
}
} // namespace

void IngtoninatorPlugin::onSettingChanged(const QString &key)
{
    Q_UNUSED(key);
    context->refreshChatBar();
}

void IngtoninatorPlugin::onBeforeSend(OutgoingMessage &message)
{
    if (!context->settings().boolValue(QStringLiteral("isEnabled")))
        return;
    message.content = Ingtoninator::apply(message.content, [] { return QRandomGenerator::global()->generateDouble(); });
}

QList<ChatBarButton> IngtoninatorPlugin::chatBarButtons(const ChatContext &chat)
{
    Q_UNUSED(chat);
    if (!context->settings().boolValue(QStringLiteral("showIcon")))
        return {};
    const bool on = context->settings().boolValue(QStringLiteral("isEnabled"));
    ChatBarButton b;
    b.id = QStringLiteral("toggle");
    b.tooltip = on ? QStringLiteral("Ingtoninator Enabled") : QStringLiteral("Ingtoninator Disabled");
    b.svg = icon(on);
    b.active = on;
    return { b };
}

void IngtoninatorPlugin::onChatBarButtonClicked(const QString &id, const ChatContext &chat)
{
    Q_UNUSED(chat);
    if (id == QLatin1String("toggle"))
        context->settings().setValue(QStringLiteral("isEnabled"), !context->settings().boolValue(QStringLiteral("isEnabled")));
}

QByteArray qord_manifest_ingtoninator();

void qord_register_ingtoninator(Registry &registry)
{
    registry.add({ QStringLiteral("ingtoninator/plugin.json"),
                   [] { return std::make_unique<IngtoninatorPlugin>(); }, qord_manifest_ingtoninator() });
}

} // namespace Plugins
} // namespace Qord
