/*
 * Qord, a native Discord client
 * Port of Equicord's ReverseImageSearch plugin (src/plugins/reverseImageSearch)
 * Original: Copyright (c) Vendicated and contributors, authors Ven, Nuckyz
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ReverseImageSearch.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

namespace Qord {
namespace Plugins {

namespace ReverseImageSearch {

const QList<Engine> &engines()
{
    static const QList<Engine> list = {
        { QStringLiteral("Google"), QStringLiteral("https://lens.google.com/uploadbyurl?url=") },
        { QStringLiteral("Yandex"), QStringLiteral("https://yandex.com/images/search?rpt=imageview&url=") },
        { QStringLiteral("SauceNAO"), QStringLiteral("https://saucenao.com/search.php?url=") },
        { QStringLiteral("IQDB"), QStringLiteral("https://iqdb.org/?url=") },
        { QStringLiteral("Bing"), QStringLiteral("https://www.bing.com/images/search?view=detailv2&iss=sbi&q=imgurl:") },
        { QStringLiteral("TinEye"), QStringLiteral("https://www.tineye.com/search?url=") },
        { QStringLiteral("ImgOps"), QStringLiteral("https://imgops.com/start?url=") },
    };
    return list;
}

QUrl searchUrl(const Engine &engine, const QString &imageUrl)
{
    // encodeURIComponent leaves A-Z a-z 0-9 - _ . ! ~ * ' ( ) as they are
    const QByteArray encoded = QUrl::toPercentEncoding(imageUrl, "!~*'()");
    return QUrl::fromEncoded(engine.prefix.toUtf8() + encoded);
}

} // namespace ReverseImageSearch

namespace {
const QString Prefix = QStringLiteral("search-image-");
const QString All = QStringLiteral("search-image-all");
} // namespace

QList<MenuItem> ReverseImageSearchPlugin::messageContextMenuItems(const MessageContext &message)
{
    if (message.imageUrl.isEmpty())
        return {};
    MenuItem menu { QStringLiteral("search-image"), QStringLiteral("Search Image"), {} };
    for (const auto &engine : ReverseImageSearch::engines())
        menu.children.append({ Prefix + engine.name, engine.name, {} });
    menu.children.append({ All, QStringLiteral("All"), {} });
    return { menu };
}

void ReverseImageSearchPlugin::onMessageContextMenuAction(const QString &id, const MessageContext &message)
{
    UiService *ui = context->ui();
    if (!ui || message.imageUrl.isEmpty())
        return;
    for (const auto &engine : ReverseImageSearch::engines())
        if (id == All || id == Prefix + engine.name)
            ui->openUrl(ReverseImageSearch::searchUrl(engine, message.imageUrl));
}

QByteArray qord_manifest_reverseimagesearch();

void qord_register_reverseimagesearch(Registry &registry)
{
    registry.add({ QStringLiteral("reverseimagesearch/plugin.json"),
                   [] { return std::make_unique<ReverseImageSearchPlugin>(); }, qord_manifest_reverseimagesearch() });
}

} // namespace Plugins
} // namespace Qord
