#include "Permission.hpp"

#include <QCoreApplication>

namespace Qord {
namespace Plugins {

namespace {
struct PermissionInfo
{
    Permission permission;
    const char *name;
    const char *description;
};

const PermissionInfo permissionTable[] = {
    { Permission::ModifyOutgoingMessages, "modify_outgoing_messages",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Change messages you send or edit") },
    { Permission::ModifyDisplayedMessages, "modify_displayed_messages",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Change how received messages are shown to you") },
    { Permission::ModifyMessageLinks, "modify_message_links",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Change message links you copy") },
    { Permission::ReplyMentions, "reply_mentions",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Decide whether replies ping the author") },
    { Permission::ReadGuildMembers, "read_guild_members",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Read server members' roles") },
    { Permission::Network, "network",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Download data from the internet") },
    { Permission::AddUiElements, "add_ui_elements",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Add buttons and menu items") },
    { Permission::WriteClipboard, "write_clipboard",
      QT_TRANSLATE_NOOP("Qord::Plugins", "Copy text to your clipboard") },
};

struct HookInfo
{
    Hook hook;
    const char *name;
    Permission permission;
};

const HookInfo hookTable[] = {
    { Hook::BeforeSend, "onBeforeSend", Permission::ModifyOutgoingMessages },
    { Hook::BeforeEdit, "onBeforeEdit", Permission::ModifyOutgoingMessages },
    { Hook::ShouldMentionOnReply, "shouldMentionOnReply", Permission::ReplyMentions },
    { Hook::TransformCopiedMessageLink, "transformCopiedMessageLink", Permission::ModifyMessageLinks },
    { Hook::TransformIncomingContent, "transformIncomingContent", Permission::ModifyDisplayedMessages },
    { Hook::ChatBarButtons, "chatBarButtons", Permission::AddUiElements },
    { Hook::UserContextMenu, "userContextMenu", Permission::AddUiElements },
};
static_assert(std::size(hookTable) == HookCount);
} // namespace

std::optional<Permission> permissionFromString(const QString &name)
{
    for (const auto &info : permissionTable)
        if (name == QLatin1String(info.name))
            return info.permission;
    return std::nullopt;
}

QString permissionToString(Permission permission)
{
    for (const auto &info : permissionTable)
        if (info.permission == permission)
            return QString::fromLatin1(info.name);
    return {};
}

QString permissionDescription(Permission permission)
{
    for (const auto &info : permissionTable)
        if (info.permission == permission)
            return QCoreApplication::translate("Qord::Plugins", info.description);
    return {};
}

QStringList permissionsToStrings(Permissions permissions)
{
    QStringList out;
    for (const auto &info : permissionTable)
        if (permissions.testFlag(info.permission))
            out.append(QString::fromLatin1(info.name));
    return out;
}

const QList<Permission> &allPermissions()
{
    static const QList<Permission> list = [] {
        QList<Permission> l;
        for (const auto &info : permissionTable)
            l.append(info.permission);
        return l;
    }();
    return list;
}

std::optional<Hook> hookFromString(const QString &name)
{
    for (const auto &info : hookTable)
        if (name == QLatin1String(info.name))
            return info.hook;
    return std::nullopt;
}

QString hookToString(Hook hook)
{
    for (const auto &info : hookTable)
        if (info.hook == hook)
            return QString::fromLatin1(info.name);
    return {};
}

Permission requiredPermission(Hook hook)
{
    for (const auto &info : hookTable)
        if (info.hook == hook)
            return info.permission;
    Q_UNREACHABLE();
}

const QList<Hook> &allHooks()
{
    static const QList<Hook> list = [] {
        QList<Hook> l;
        for (const auto &info : hookTable)
            l.append(info.hook);
        return l;
    }();
    return list;
}

int hookIndex(Hook hook)
{
    for (int i = 0; i < HookCount; ++i)
        if (hookTable[i].hook == hook)
            return i;
    Q_UNREACHABLE();
}

} // namespace Plugins
} // namespace Qord
