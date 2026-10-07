/*
 * Qord, a native Discord client
 * Port of Equicord's TalkInReverse plugin (src/equicordplugins/talkInReverse)
 * Original: Copyright (c) Vendicated and contributors, author Tolgchu
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "TalkInReverse.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QTextBoundaryFinder>

namespace Qord {
namespace Plugins {

namespace {
// Icon from upstream (Material Symbols "sync"), recolored when active
QByteArray icon(bool active)
{
    const QByteArray fill = active ? "#23a55a" : "currentColor";
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" height=\"20\" viewBox=\"0 -960 960 960\" width=\"20\">"
           "<path fill=\"" + fill + "\" d=\"M482-160q-134 0-228-93t-94-227v-7l-36 36q-11 11-28 11t-28-11q-11-11-11-28t11-28l104-104q12-12 28-12t28 12l104 104q11 11 11 28t-11 28q-11 11-28 11t-28-11l-36-36v7q0 100 70.5 170T482-240q16 0 31.5-2t30.5-7q17-5 32 1t23 21q8 16 1.5 31.5T577-175q-23 8-47 11.5t-48 3.5Zm-4-560q-16 0-31.5 2t-30.5 7q-17 5-32.5-1T360-733q-8-15-1.5-30.5T381-784q24-8 48-12t49-4q134 0 228 93t94 227v7l36-36q11-11 28-11t28 11q11 11 11 28t-11 28L788-349q-12 12-28 12t-28-12L628-453q-11-11-11-28t11-28q11-11 28-11t28 11l36 36v-7q0-100-70.5-170T478-720Z\"/></svg>";
}
} // namespace

namespace TalkInReverse {

QString reverse(const QString &text)
{
    QStringList graphemes;
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    qsizetype last = 0;
    while (finder.toNextBoundary() != -1) {
        const qsizetype pos = finder.position();
        graphemes.prepend(text.mid(last, pos - last));
        last = pos;
    }
    return graphemes.join(QString());
}

} // namespace TalkInReverse

void TalkInReversePlugin::onBeforeSend(OutgoingMessage &message)
{
    if (enabled && !message.content.isEmpty())
        message.content = TalkInReverse::reverse(message.content);
}

QList<ChatBarButton> TalkInReversePlugin::chatBarButtons(const ChatContext &chat)
{
    Q_UNUSED(chat);
    ChatBarButton b;
    b.id = QStringLiteral("toggle");
    b.tooltip = enabled ? QStringLiteral("Disable Reverse Message") : QStringLiteral("Enable Reverse Message");
    b.svg = icon(enabled);
    b.active = enabled;
    return { b };
}

void TalkInReversePlugin::onChatBarButtonClicked(const QString &id, const ChatContext &chat)
{
    Q_UNUSED(chat);
    if (id != QLatin1String("toggle"))
        return;
    enabled = !enabled;
    if (context)
        context->refreshChatBar();
}

QByteArray qord_manifest_talkinreverse();

void qord_register_talkinreverse(Registry &registry)
{
    registry.add({ QStringLiteral("talkinreverse/plugin.json"),
                   [] { return std::make_unique<TalkInReversePlugin>(); }, qord_manifest_talkinreverse() });
}

} // namespace Plugins
} // namespace Qord
