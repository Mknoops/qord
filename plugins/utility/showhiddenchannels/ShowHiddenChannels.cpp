/*
 * Qord, a native Discord client
 * Port of Equicord's ShowHiddenChannels plugin (src/plugins/showHiddenChannels)
 * Original: Copyright (c) Vendicated and contributors, authors BigDuck, Average React Enjoyer, D3SOX, Ven,
 * Nuckyz, Nickyux, rini, Oggetto
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ShowHiddenChannels.hpp"

#include "Plugins/PermissionViews.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QTimeZone>
#include <QtMath>

namespace Qord {
namespace Plugins {

namespace {
constexpr quint64 Administrator = quint64(1) << 3;
constexpr quint64 ViewChannel = quint64(1) << 10;
constexpr quint64 Connect = quint64(1) << 20;
constexpr quint64 RequireTag = quint64(1) << 4;
constexpr Id DiscordEpochMs = 1420070400000ULL;

enum ChannelType { GuildText = 0, GuildVoice = 2, GuildAnnouncement = 5, GuildStageVoice = 13, GuildForum = 15 };

QString typeName(int type)
{
    switch (type) {
    case GuildText:
        return QStringLiteral("text");
    case GuildAnnouncement:
        return QStringLiteral("announcement");
    case GuildForum:
        return QStringLiteral("forum");
    case GuildVoice:
        return QStringLiteral("voice");
    case GuildStageVoice:
        return QStringLiteral("stage");
    default:
        return {};
    }
}

QString unitName(const char *plural, bool one)
{
    QString unit = QString::fromLatin1(plural);
    return one ? unit.chopped(1) : unit;
}

QString memberName(const MemberInfo &member)
{
    if (!member.nick.isEmpty())
        return member.nick;
    return member.globalName.isEmpty() ? member.username : member.globalName;
}
} // namespace

void ShowHiddenChannelsPlugin::start(PluginContext &context)
{
    this->context = &context;
    alive = std::make_shared<bool>(true);
}

void ShowHiddenChannelsPlugin::stop()
{
    if (alive)
        *alive = false;
    alive.reset();
    context = nullptr;
}

QDateTime ShowHiddenChannelsPlugin::snowflakeTime(Id id)
{
    return QDateTime::fromMSecsSinceEpoch(qint64((id >> 22) + DiscordEpochMs)).toUTC();
}

QString ShowHiddenChannelsPlugin::formatDuration(qint64 amount, Unit unit)
{
    // moment.duration's components, as upstream reads them
    qint64 seconds = unit == Unit::Minutes ? amount * 60 : amount;
    const qint64 secs = seconds % 60;
    qint64 minutes = seconds / 60;
    const qint64 mins = minutes % 60;
    qint64 hours = minutes / 60;
    const qint64 hrs = hours % 24;
    qint64 days = hours / 24;
    const qint64 monthsFromDays = days * 4800 / 146097;
    days -= qint64(qCeil(double(monthsFromDays) * 146097.0 / 4800.0));
    const qint64 years = monthsFromDays / 12;
    const qint64 months = monthsFromDays % 12;
    const qint64 weeks = days / 7;

    struct Amount
    {
        qint64 amount;
        const char *unit;
    };
    QList<Amount> amounts = { { years, "years" }, { months, "months" }, { weeks, "weeks" }, { days, "days" },
                              { hrs, "hours" },   { mins, "minutes" },  { secs, "seconds" } };

    // Drop the zero units after the last non-zero one
    int removed = 0;
    for (int i = 0; i < amounts.size(); ++i) {
        if (amounts[i].amount == 0 || i + 1 >= amounts.size())
            continue;
        bool laterNonZero = false;
        for (int v = i + 1; v < amounts.size(); ++v)
            laterNonZero = laterNonZero || amounts[v].amount != 0;
        if (!laterNonZero)
            removed = int(amounts.size()) - (i + 1);
    }
    amounts = amounts.mid(0, amounts.size() - removed);

    // Days beyond whole weeks
    for (int i = 0; i < amounts.size(); ++i) {
        if (qstrcmp(amounts[i].unit, "days") != 0)
            continue;
        const qint64 mod = amounts[i].amount % 7;
        if (mod == 0)
            amounts.removeAt(i);
        else
            amounts[i].amount = mod;
        break;
    }

    QString out;
    while (!amounts.isEmpty()) {
        const Amount a = amounts.takeFirst();
        if (!out.isEmpty())
            out += amounts.isEmpty() ? QStringLiteral(" and ") : QStringLiteral(", ");
        if (a.amount > 0 || !out.isEmpty())
            out += QStringLiteral("%1 %2").arg(a.amount).arg(unitName(a.unit, a.amount == 1));
    }
    if (out.isEmpty())
        return QStringLiteral("0 ") + unitName(unit == Unit::Minutes ? "minutes" : "seconds", false);
    return out;
}

QList<PermissionsEntry> ShowHiddenChannelsPlugin::allowed(const GuildInfo &guild, const ChannelInfo &channel,
                                                          quint64 permission, const MemberLookup &member)
{
    quint64 everyonePermissions = 0;
    for (const RoleInfo &role : guild.roles)
        if (role.id == guild.id)
            everyonePermissions = role.permissions;
    auto overwriteFor = [&](PermissionOverwriteInfo::Type type, Id id) -> const PermissionOverwriteInfo * {
        for (const auto &overwrite : channel.overwrites)
            if (overwrite.type == type && overwrite.id == id)
                return &overwrite;
        return nullptr;
    };
    const PermissionOverwriteInfo *everyoneOverwrite = overwriteFor(PermissionOverwriteInfo::Type::Role, guild.id);

    // Each role as if a member had only it (Discord's forceRoles check)
    QList<PermissionsEntry> roles;
    for (const RoleInfo &role : guild.roles) {
        const bool everyone = role.id == guild.id;
        quint64 permissions = everyonePermissions | (everyone ? 0 : role.permissions);
        bool can = (permissions & Administrator) != 0;
        if (!can) {
            if (everyoneOverwrite)
                permissions = (permissions & ~everyoneOverwrite->deny) | everyoneOverwrite->allow;
            if (!everyone)
                if (const auto *own = overwriteFor(PermissionOverwriteInfo::Type::Role, role.id))
                    permissions = (permissions & ~own->deny) | own->allow;
            can = (permissions & permission) == permission;
        }
        if (!can)
            continue;
        if (everyone) {
            roles = { roleEntry(role) }; // everyone can: no other role needs listing
            break;
        }
        roles.append(roleEntry(role));
    }

    // The owner, then members given access by an overwrite
    QList<Id> users;
    if (guild.ownerId)
        users.append(guild.ownerId);
    for (const auto &overwrite : channel.overwrites)
        if (overwrite.type == PermissionOverwriteInfo::Type::Member && (overwrite.allow & permission) == permission &&
            !users.contains(overwrite.id))
            users.append(overwrite.id);

    QList<PermissionsEntry> out;
    for (Id userId : users) {
        PermissionsEntry entry;
        entry.kind = PermissionsEntry::Kind::Member;
        entry.id = userId;
        const auto info = member ? member(userId) : std::nullopt;
        entry.name = info ? memberName(*info) : QStringLiteral("Unknown User");
        if (info)
            entry.imageUrl = info->avatarUrl;
        out.append(entry);
    }
    return out + roles;
}

std::optional<HiddenChannelDisplay> ShowHiddenChannelsPlugin::hiddenChannelDisplay()
{
    if (!context)
        return std::nullopt;
    HiddenChannelDisplay display;
    const QString style = context->settings().stringValue(QStringLiteral("channelStyle"));
    if (style == QLatin1String("muted"))
        display.style = HiddenChannelDisplay::Style::Muted;
    else if (style == QLatin1String("unread"))
        display.style = HiddenChannelDisplay::Style::Unread;
    else if (style == QLatin1String("mutedUnread"))
        display.style = HiddenChannelDisplay::Style::MutedUnread;
    const QString mode = context->settings().stringValue(QStringLiteral("showMode"));
    if (mode == QLatin1String("eyeIconRight"))
        display.icon = HiddenChannelDisplay::Icon::EyeRight;
    else if (mode == QLatin1String("lockIconRight"))
        display.icon = HiddenChannelDisplay::Icon::LockRight;
    return display;
}

std::optional<LockScreen> ShowHiddenChannelsPlugin::hiddenChannelLockScreen(const HiddenChannelContext &hidden)
{
    GuildDirectory *guilds = context ? context->guilds() : nullptr;
    if (!guilds)
        return std::nullopt;
    const auto channel = guilds->channel(hidden.accountId, hidden.channelId);
    if (!channel || !channel->guildId)
        return std::nullopt;
    const auto guild = guilds->guild(hidden.accountId, channel->guildId);

    const bool voice = channel->type == GuildVoice || channel->type == GuildStageVoice;
    const bool forum = channel->type == GuildForum;
    const QString posts = forum ? QStringLiteral("post") : QStringLiteral("message");

    LockScreen screen;
    screen.heading = QStringLiteral("This is a %1 %2 channel")
                             .arg(hidden.canView ? QStringLiteral("locked") : QStringLiteral("hidden"),
                                  typeName(channel->type))
                             .simplified();
    screen.nsfw = channel->nsfw;
    if (!voice) {
        screen.subheading = QStringLiteral("You can not see the %1s of this channel.").arg(posts);
        if (forum && !channel->topic.isEmpty())
            screen.subheading += QStringLiteral(" However you may see its guidelines:");
    }
    if (forum)
        screen.topic = channel->topic;

    auto add = [&](const QString &text, const QDateTime &time = {}) { screen.details.append({ text, time }); };
    if (channel->lastMessageId)
        add(QStringLiteral("Last %1 created:").arg(posts), snowflakeTime(channel->lastMessageId));
    if (channel->lastPinTimestamp.isValid())
        add(QStringLiteral("Last message pin:"), channel->lastPinTimestamp);
    if (channel->rateLimitPerUser > 0)
        add(QStringLiteral("Slowmode: ") + formatDuration(channel->rateLimitPerUser, Unit::Seconds));
    if (channel->defaultThreadRateLimitPerUser > 0)
        add(QStringLiteral("Default thread slowmode: ") +
            formatDuration(channel->defaultThreadRateLimitPerUser, Unit::Seconds));
    if (voice && channel->bitrate)
        add(QStringLiteral("Bitrate: %1 bits").arg(*channel->bitrate));
    if (channel->hasRtcRegion)
        add(QStringLiteral("Region: ") +
            (channel->rtcRegion.isEmpty() ? QStringLiteral("Automatic") : channel->rtcRegion));
    if (voice)
        add(QStringLiteral("Video quality mode: ") +
            (channel->videoQualityMode.value_or(1) == 2 ? QStringLiteral("720p") : QStringLiteral("Automatic")));
    if (channel->defaultAutoArchiveDuration > 0)
        add(QStringLiteral("Default inactivity duration before archiving %1: %2")
                    .arg(forum ? QStringLiteral("posts") : QStringLiteral("threads"),
                         formatDuration(channel->defaultAutoArchiveDuration, Unit::Minutes)));
    if (channel->defaultForumLayout) {
        static const QStringList layouts = { QStringLiteral("Not set"), QStringLiteral("List view"),
                                             QStringLiteral("Gallery view") };
        add(QStringLiteral("Default layout: ") + layouts.value(*channel->defaultForumLayout));
    }
    if (channel->defaultSortOrder) {
        static const QStringList orders = { QStringLiteral("Latest activity"), QStringLiteral("Creation date") };
        add(QStringLiteral("Default sort order: ") + orders.value(*channel->defaultSortOrder));
    }
    if (!channel->defaultReactionEmojiName.isEmpty())
        add(QStringLiteral("Default reaction emoji: ") + channel->defaultReactionEmojiName);
    else if (channel->defaultReactionEmojiId)
        add(QStringLiteral("Default reaction emoji: a custom emoji"));
    if (channel->flags & RequireTag)
        add(QStringLiteral("Posts on this forum require a tag to be set."));
    screen.tags = channel->availableTags;

    // Who can see it (join it, for a locked voice channel); members that
    // aren't loaded are asked for, as upstream does
    if (guild) {
        const Id accountId = hidden.accountId;
        const Id guildId = channel->guildId;
        QList<Id> missing;
        auto lookup = [&](Id userId) -> std::optional<MemberInfo> {
            auto info = guilds->member(accountId, guildId, userId);
            if (!info || !info->isMember)
                missing.append(userId);
            return info;
        };
        screen.allowed = allowed(*guild, *channel, hidden.canView ? Connect : ViewChannel, lookup);
        if (!missing.isEmpty())
            guilds->requestMembers(accountId, guildId, missing);
    }

    screen.allowedExpanded = context->settings().boolValue(QStringLiteral("defaultAllowedUsersAndRolesDropdownState"));
    std::weak_ptr<bool> weak = alive;
    screen.toggleAllowed = [this, weak]() {
        if (auto a = weak.lock(); !a || !*a || !context)
            return;
        const QString key = QStringLiteral("defaultAllowedUsersAndRolesDropdownState");
        context->settings().setValue(key, !context->settings().boolValue(key));
        if (UiService *ui = context->ui())
            ui->refreshLockScreen();
    };
    const Id accountId = hidden.accountId;
    const Id channelId = hidden.channelId;
    screen.showPermissionDetails = [this, weak, accountId, channelId]() {
        if (auto a = weak.lock(); !a || !*a || !context)
            return;
        GuildDirectory *directory = context->guilds();
        UiService *ui = context->ui();
        if (!directory || !ui)
            return;
        if (const auto view = channelPermissionsView(*directory, accountId, channelId))
            ui->showPermissions(*view);
    };
    return screen;
}

QByteArray qord_manifest_showhiddenchannels();

void qord_register_showhiddenchannels(Registry &registry)
{
    registry.add({ QStringLiteral("showhiddenchannels/plugin.json"),
                   [] { return std::make_unique<ShowHiddenChannelsPlugin>(); }, qord_manifest_showhiddenchannels() });
}

} // namespace Plugins
} // namespace Qord
