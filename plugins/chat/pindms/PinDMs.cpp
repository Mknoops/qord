/*
 * Qord, a native Discord client
 * Port of Equicord's PinDMs plugin (src/plugins/pinDms)
 * Original: Copyright (c) Vendicated and contributors, authors Ven, Aria
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "PinDMs.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QJsonArray>
#include <QUuid>

namespace Qord {
namespace Plugins {

namespace {
const QString ListKey = QStringLiteral("userBasedCategoryList");
const QString MenuPrefix = QStringLiteral("vc-pin-dm:");
constexpr int DmType = 1, GroupDmType = 3;

QJsonObject toJson(const PinDMsPlugin::Category &c)
{
    QJsonArray channels;
    for (Id id : c.channels)
        channels.append(QString::number(id));
    return { { "id", c.id }, { "name", c.name }, { "color", double(c.color) }, { "collapsed", c.collapsed },
             { "channels", channels } };
}

int indexOfChannel(const QList<PinDMsPlugin::Category> &list, Id channelId)
{
    for (int i = 0; i < list.size(); ++i)
        if (list[i].channels.contains(channelId))
            return i;
    return -1;
}

int indexOfCategory(const QList<PinDMsPlugin::Category> &list, const QString &id)
{
    for (int i = 0; i < list.size(); ++i)
        if (list[i].id == id)
            return i;
    return -1;
}
} // namespace

const QList<quint32> &PinDMsPlugin::swatches()
{
    // Upstream's SWATCHES (Discord's role colors)
    static const QList<quint32> list = { 1752220, 3066993, 3447003, 10181046, 15277667, 15844367, 15105570,
                                         15158332, 9807270, 6323595, 1146986, 2067276, 2123412, 7419530,
                                         11342935, 12745742, 11027200, 10038562, 9936031, 5533306 };
    return list;
}

void PinDMsPlugin::start(PluginContext &ctx)
{
    context = &ctx;
}

void PinDMsPlugin::stop()
{
    context = nullptr;
}

void PinDMsPlugin::onSettingChanged(const QString &key)
{
    if (context && key != QLatin1String("dmSectionCollapsed") && key != ListKey)
        context->refreshDmList();
}

bool PinDMsPlugin::customOrder() const
{
    return context && context->settings().stringValue(QStringLiteral("pinOrder")) == QLatin1String("custom");
}

QList<PinDMsPlugin::Category> PinDMsPlugin::categories(Id accountId) const
{
    QList<Category> out;
    if (!context)
        return out;
    const QJsonArray list =
            context->settings().value(ListKey).toObject().value(QString::number(accountId)).toArray();
    for (const auto &value : list) {
        const QJsonObject o = value.toObject();
        Category c;
        c.id = o.value(QLatin1String("id")).toString();
        c.name = o.value(QLatin1String("name")).toString();
        c.color = quint32(o.value(QLatin1String("color")).toDouble(DefaultColor));
        c.collapsed = o.value(QLatin1String("collapsed")).toBool();
        for (const auto &channel : o.value(QLatin1String("channels")).toArray())
            c.channels.append(channel.toString().toULongLong());
        if (!c.id.isEmpty())
            out.append(c);
    }
    return out;
}

void PinDMsPlugin::save(Id accountId, const QList<Category> &list)
{
    if (!context)
        return;
    QJsonObject all = context->settings().value(ListKey).toObject();
    QJsonArray array;
    for (const auto &c : list)
        array.append(toJson(c));
    all.insert(QString::number(accountId), array);
    context->settings().setValue(ListKey, all);
    context->refreshDmList();
}

std::optional<DmPinLayout> PinDMsPlugin::dmPinLayout(Id accountId)
{
    if (!context)
        return std::nullopt;
    const QList<Category> list = categories(accountId);
    if (list.isEmpty())
        return std::nullopt;
    DmPinLayout layout;
    for (const auto &c : list)
        layout.categories.append({ c.id, c.name, c.color, c.collapsed, c.channels });
    layout.sortByLastMessage = !customOrder();
    layout.dmSectionCollapsible = context->settings().boolValue(QStringLiteral("canCollapseDmSection"));
    layout.dmSectionCollapsed = context->settings().boolValue(QStringLiteral("dmSectionCollapsed"));
    return layout;
}

void PinDMsPlugin::onDmPinHeaderClicked(Id accountId, const QString &headerId)
{
    if (!context)
        return;
    if (headerId == QLatin1String(DmSectionHeaderId)) {
        if (!context->settings().boolValue(QStringLiteral("canCollapseDmSection")))
            return;
        context->settings().setValue(QStringLiteral("dmSectionCollapsed"),
                                     !context->settings().boolValue(QStringLiteral("dmSectionCollapsed")));
        context->refreshDmList();
        return;
    }
    QList<Category> list = categories(accountId);
    const int i = indexOfCategory(list, headerId);
    if (i < 0)
        return;
    list[i].collapsed = !list[i].collapsed;
    save(accountId, list);
}

QList<MenuItem> PinDMsPlugin::dmPinHeaderMenu(Id accountId, const QString &headerId)
{
    const QList<Category> list = categories(accountId);
    const int i = indexOfCategory(list, headerId);
    if (i < 0)
        return {};
    QList<MenuItem> items = { { QStringLiteral("vc-pindms-edit-category"), QStringLiteral("Edit Category"), {} } };
    if (i > 0)
        items.append({ QStringLiteral("vc-pindms-move-category-up"), QStringLiteral("Move Up"), {} });
    if (i < list.size() - 1)
        items.append({ QStringLiteral("vc-pindms-move-category-down"), QStringLiteral("Move Down"), {} });
    items.append({ QString(), QString(), {} }); // separator
    items.append({ QStringLiteral("vc-pindms-delete-category"), QStringLiteral("Delete Category"), {} });
    return items;
}

void PinDMsPlugin::onDmPinHeaderMenuAction(Id accountId, const QString &headerId, const QString &itemId)
{
    QList<Category> list = categories(accountId);
    const int i = indexOfCategory(list, headerId);
    if (i < 0)
        return;
    if (itemId == QLatin1String("vc-pindms-edit-category")) {
        openCategoryModal(accountId, headerId, 0);
        return;
    }
    if (itemId == QLatin1String("vc-pindms-move-category-up") && i > 0)
        list.swapItemsAt(i, i - 1);
    else if (itemId == QLatin1String("vc-pindms-move-category-down") && i < list.size() - 1)
        list.swapItemsAt(i, i + 1);
    else if (itemId == QLatin1String("vc-pindms-delete-category"))
        list.removeAt(i);
    else
        return;
    save(accountId, list);
}

QList<MenuItem> PinDMsPlugin::channelContextMenuItems(const ChannelContext &channel)
{
    if (!context || channel.guildId || !channel.channelId || (channel.type != DmType && channel.type != GroupDmType))
        return {};
    const QList<Category> list = categories(channel.accountId);
    const int pinnedIn = indexOfChannel(list, channel.channelId);
    MenuItem menu { QStringLiteral("vc-pin-dm"), QStringLiteral("Pin DMs"), {} };
    if (pinnedIn < 0) {
        menu.children.append({ MenuPrefix + QStringLiteral("add-category"), QStringLiteral("Add Category"), {} });
        if (!list.isEmpty())
            menu.children.append({ QString(), QString(), {} }); // separator
        for (const auto &c : list)
            menu.children.append({ MenuPrefix + QStringLiteral("pin-category-") + c.id, c.name, {} });
    } else {
        menu.children.append({ MenuPrefix + QStringLiteral("unpin"), QStringLiteral("Unpin DM"), {} });
        const auto &channels = list[pinnedIn].channels;
        const int at = int(channels.indexOf(channel.channelId));
        if (customOrder() && at > 0)
            menu.children.append({ MenuPrefix + QStringLiteral("move-up"), QStringLiteral("Move Up"), {} });
        if (customOrder() && at < channels.size() - 1)
            menu.children.append({ MenuPrefix + QStringLiteral("move-down"), QStringLiteral("Move Down"), {} });
    }
    return { menu };
}

void PinDMsPlugin::onChannelContextMenuAction(const QString &id, const ChannelContext &channel)
{
    if (!context || !id.startsWith(MenuPrefix))
        return;
    const QString action = id.mid(MenuPrefix.size());
    QList<Category> list = categories(channel.accountId);
    const int pinnedIn = indexOfChannel(list, channel.channelId);
    if (action == QLatin1String("add-category")) {
        openCategoryModal(channel.accountId, QString(), channel.channelId);
        return;
    }
    if (action.startsWith(QLatin1String("pin-category-"))) {
        const int i = indexOfCategory(list, action.mid(13));
        if (i < 0 || pinnedIn >= 0)
            return;
        list[i].channels.append(channel.channelId);
    } else if (pinnedIn < 0) {
        return;
    } else if (action == QLatin1String("unpin")) {
        list[pinnedIn].channels.removeAll(channel.channelId);
    } else {
        auto &channels = list[pinnedIn].channels;
        const int at = int(channels.indexOf(channel.channelId));
        const int to = action == QLatin1String("move-up") ? at - 1 : action == QLatin1String("move-down") ? at + 1 : -1;
        if (to < 0 || to >= channels.size())
            return;
        channels.swapItemsAt(at, to);
    }
    save(channel.accountId, list);
}

void PinDMsPlugin::openCategoryModal(Id accountId, const QString &categoryId, Id channelId)
{
    UiService *ui = context ? context->ui() : nullptr;
    if (!ui)
        return;
    const QList<Category> list = categories(accountId);
    Category category;
    const int existing = indexOfCategory(list, categoryId);
    if (existing >= 0) {
        category = list[existing];
    } else {
        category.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        category.name = QStringLiteral("Pin Category %1").arg(list.size() + 1);
        category.channels = { channelId };
    }
    UiService::FormField name;
    name.key = QStringLiteral("name");
    name.label = QStringLiteral("Name");
    name.kind = UiService::FormField::Kind::Text;
    name.value = category.name;
    UiService::FormField color;
    color.key = QStringLiteral("color");
    color.label = QStringLiteral("Color");
    color.kind = UiService::FormField::Kind::Color;
    color.colors = swatches();
    color.value = QString::number(category.color);

    ui->showFormDialog(existing >= 0 ? QStringLiteral("Edit Category") : QStringLiteral("New Category"),
                       { name, color }, existing >= 0 ? QStringLiteral("Save") : QStringLiteral("Create"),
                       [this, accountId, category](const QHash<QString, QString> &values) mutable {
                           if (!context || values.value(QStringLiteral("name")).trimmed().isEmpty())
                               return;
                           category.name = values.value(QStringLiteral("name"));
                           category.color = values.value(QStringLiteral("color")).toUInt();
                           QList<Category> current = categories(accountId);
                           const int i = indexOfCategory(current, category.id);
                           if (i >= 0) {
                               current[i].name = category.name;
                               current[i].color = category.color;
                           } else {
                               // A DM can only be in one category
                               for (auto &c : current)
                                   for (Id id : category.channels)
                                       c.channels.removeAll(id);
                               current.append(category);
                           }
                           save(accountId, current);
                       });
}

QByteArray qord_manifest_pindms();

void qord_register_pindms(Registry &registry)
{
    registry.add({ QStringLiteral("pindms/plugin.json"), [] { return std::make_unique<PinDMsPlugin>(); },
                   qord_manifest_pindms() });
}

} // namespace Plugins
} // namespace Qord
