#include "RawJson.hpp"

#include <QDateTime>
#include <QJsonArray>

#include <type_traits>

namespace Acheron {
namespace Discord {
namespace RawJson {

namespace {

template <typename T>
struct IsList : std::false_type
{
};
template <typename T>
struct IsList<QList<T>> : std::true_type
{
};

template <typename T>
std::optional<QJsonValue> value(const T &v);

template <typename T>
std::optional<QJsonValue> value(const T &v)
{
    if constexpr (std::is_same_v<T, QString>) {
        return QJsonValue(v);
    } else if constexpr (std::is_same_v<T, bool>) {
        return QJsonValue(v);
    } else if constexpr (std::is_same_v<T, Core::Snowflake>) {
        return QJsonValue(QString::number(quint64(v)));
    } else if constexpr (std::is_integral_v<T>) {
        return QJsonValue(qint64(v));
    } else if constexpr (std::is_floating_point_v<T>) {
        return QJsonValue(double(v));
    } else if constexpr (std::is_enum_v<T>) {
        return QJsonValue(qint64(v));
    } else if constexpr (std::is_same_v<T, QDateTime>) {
        return QJsonValue(v.toUTC().toString(Qt::ISODateWithMs));
    } else if constexpr (std::is_same_v<T, Permissions>) {
        return QJsonValue(QString::number(quint64(v.toInt())));
    } else if constexpr (std::is_same_v<T, QStringList>) {
        return QJsonValue(QJsonArray::fromStringList(v));
    } else if constexpr (IsList<T>::value) {
        QJsonArray array;
        for (const auto &element : v)
            if (const auto item = value(element))
                array.append(*item);
        return QJsonValue(array);
    } else if constexpr (requires { toJson(v); }) {
        return QJsonValue(toJson(v));
    } else {
        return std::nullopt; // a type the client keeps for itself
    }
}

template <typename Field>
void put(QJsonObject &o, const char *key, const Field &field)
{
    if (field.isUndefined())
        return;
    if (field.isNull()) {
        o.insert(QLatin1String(key), QJsonValue::Null);
        return;
    }
    if (const auto v = value(field.get()))
        o.insert(QLatin1String(key), *v);
}

} // namespace

QJsonObject toJson(const PermissionOverwrite &value)
{
    QJsonObject o;
    put(o, "id", value.id);
    put(o, "type", value.type);
    put(o, "allow", value.allow);
    put(o, "deny", value.deny);
    return o;
}

QJsonObject toJson(const User &value)
{
    QJsonObject o;
    put(o, "id", value.id);
    put(o, "username", value.username);
    put(o, "global_name", value.globalName);
    put(o, "avatar", value.avatar);
    put(o, "bot", value.bot);
    put(o, "banner", value.banner);
    put(o, "accent_color", value.accentColor);
    put(o, "public_flags", value.publicFlags);
    put(o, "premium_type", value.premiumType);
    put(o, "discriminator", value.discriminator);
    put(o, "mfa_enabled", value.mfaEnabled);
    return o;
}

QJsonObject toJson(const Member &value)
{
    QJsonObject o;
    put(o, "user", value.user);
    put(o, "nick", value.nick);
    put(o, "avatar", value.avatar);
    put(o, "roles", value.roles);
    put(o, "joined_at", value.joinedAt);
    put(o, "premium_since", value.premiumSince);
    put(o, "deaf", value.deaf);
    put(o, "mute", value.mute);
    put(o, "flags", value.flags);
    put(o, "pending", value.pending);
    put(o, "communication_disabled_until", value.communicationDisabledUntil);
    put(o, "unusual_dm_activity_until", value.unusualDmActivityUntil);
    put(o, "user_id", value.userId);
    put(o, "presence", value.presence);
    return o;
}

QJsonObject toJson(const RoleColors &value)
{
    QJsonObject o;
    put(o, "primary_color", value.primaryColor);
    put(o, "secondary_color", value.secondaryColor);
    put(o, "tertiary_color", value.tertiaryColor);
    return o;
}

QJsonObject toJson(const RoleTags &value)
{
    QJsonObject o;
    put(o, "bot_id", value.botId);
    put(o, "integration_id", value.integrationId);
    put(o, "subscription_listing_id", value.subscriptionListingId);
    return o;
}

QJsonObject toJson(const Role &value)
{
    QJsonObject o;
    put(o, "id", value.id);
    put(o, "name", value.name);
    put(o, "position", value.position);
    put(o, "color", value.color);
    put(o, "colors", value.colors);
    put(o, "hoist", value.hoist);
    put(o, "icon", value.icon);
    put(o, "unicode_emoji", value.unicodeEmoji);
    put(o, "managed", value.managed);
    put(o, "mentionable", value.mentionable);
    put(o, "flags", value.flags);
    put(o, "tags", value.tags);
    put(o, "permissions", value.permissions);
    return o;
}

QJsonObject toJson(const ThreadMetadata &value)
{
    QJsonObject o;
    put(o, "archived", value.archived);
    put(o, "auto_archive_duration", value.autoArchiveDuration);
    put(o, "archive_timestamp", value.archiveTimestamp);
    return o;
}

QJsonObject toJson(const ForumTag &value)
{
    QJsonObject o;
    put(o, "id", value.id);
    put(o, "name", value.name);
    put(o, "emoji_id", value.emojiId);
    put(o, "emoji_name", value.emojiName);
    return o;
}

QJsonObject toJson(const Channel &value)
{
    QJsonObject o;
    put(o, "id", value.id);
    put(o, "type", value.type);
    put(o, "name", value.name);
    put(o, "topic", value.topic);
    put(o, "position", value.position);
    put(o, "guild_id", value.guildId);
    put(o, "parent_id", value.parentId);
    put(o, "permission_overwrites", value.permissionOverwrites);
    put(o, "recipients", value.recipients);
    put(o, "recipient_ids", value.recipientIds);
    put(o, "last_message_id", value.lastMessageId);
    put(o, "icon", value.icon);
    put(o, "owner_id", value.ownerId);
    put(o, "rate_limit_per_user", value.rateLimitPerUser);
    put(o, "user_limit", value.userLimit);
    put(o, "is_message_request", value.isMessageRequest);
    put(o, "is_spam", value.isSpam);
    put(o, "thread_metadata", value.threadMetadata);
    put(o, "member", value.member);
    put(o, "available_tags", value.availableTags);
    put(o, "applied_tags", value.appliedTags);
    put(o, "message_count", value.messageCount);
    put(o, "member_count", value.memberCount);
    put(o, "total_message_sent", value.totalMessageSent);
    put(o, "flags", value.flags);
    put(o, "default_sort_order", value.defaultSortOrder);
    return o;
}

QJsonObject toJson(const Emoji &value)
{
    QJsonObject o;
    put(o, "id", value.id);
    put(o, "name", value.name);
    put(o, "animated", value.animated);
    put(o, "roles", value.roles);
    put(o, "managed", value.managed);
    put(o, "available", value.available);
    put(o, "user", value.user);
    return o;
}

QJsonObject toJson(const Sticker &value)
{
    QJsonObject o;
    put(o, "id", value.id);
    put(o, "name", value.name);
    put(o, "description", value.description);
    put(o, "tags", value.tags);
    put(o, "format_type", value.formatType);
    put(o, "available", value.available);
    put(o, "guild_id", value.guildId);
    put(o, "user", value.user);
    return o;
}

QJsonObject toJson(const Guild &value)
{
    QJsonObject o;
    put(o, "id", value.id);
    put(o, "name", value.name);
    put(o, "icon", value.icon);
    put(o, "owner_id", value.ownerId);
    put(o, "roles", value.roles);
    put(o, "premium_tier", value.premiumTier);
    put(o, "premium_subscription_count", value.premiumSubscriptionCount);
    put(o, "rules_channel_id", value.rulesChannelId);
    put(o, "default_message_notifications", value.defaultMessageNotifications);
    put(o, "features", value.features);
    put(o, "description", value.description);
    put(o, "banner", value.banner);
    put(o, "splash", value.splash);
    put(o, "afk_channel_id", value.afkChannelId);
    put(o, "afk_timeout", value.afkTimeout);
    put(o, "system_channel_id", value.systemChannelId);
    put(o, "system_channel_flags", value.systemChannelFlags);
    put(o, "verification_level", value.verificationLevel);
    put(o, "explicit_content_filter", value.explicitContentFilter);
    put(o, "mfa_level", value.mfaLevel);
    put(o, "premium_progress_bar_enabled", value.premiumProgressBarEnabled);
    put(o, "premium_features", value.premiumFeatures);
    put(o, "incidents_data", value.incidentsData);
    return o;
}

QJsonObject toJson(const ConnectedAccount &value)
{
    QJsonObject o;
    put(o, "id", value.id);
    put(o, "type", value.type);
    put(o, "name", value.name);
    put(o, "verified", value.verified);
    return o;
}

QJsonObject toJson(const MutualGuildEntry &value)
{
    QJsonObject o;
    put(o, "id", value.id);
    put(o, "nick", value.nick);
    return o;
}

QJsonObject toJson(const UserProfileBadge &value)
{
    QJsonObject o;
    put(o, "id", value.id);
    put(o, "description", value.description);
    put(o, "icon", value.icon);
    put(o, "link", value.link);
    return o;
}

QJsonObject toJson(const UserProfileData &value)
{
    QJsonObject o;
    put(o, "bio", value.bio);
    put(o, "accent_color", value.accentColor);
    put(o, "banner", value.banner);
    put(o, "pronouns", value.pronouns);
    return o;
}

QJsonObject toJson(const GuildMemberProfile &value)
{
    QJsonObject o;
    put(o, "guild_id", value.guildId);
    put(o, "bio", value.bio);
    put(o, "accent_color", value.accentColor);
    put(o, "banner", value.banner);
    put(o, "pronouns", value.pronouns);
    return o;
}

QJsonObject toJson(const UserProfile &value)
{
    QJsonObject o;
    put(o, "user", value.user);
    put(o, "user_profile", value.userProfile);
    put(o, "premium_type", value.premiumType);
    put(o, "premium_since", value.premiumSince);
    put(o, "premium_guild_since", value.premiumGuildSince);
    put(o, "connected_accounts", value.connectedAccounts);
    put(o, "mutual_guilds", value.mutualGuilds);
    put(o, "mutual_friends", value.mutualFriends);
    put(o, "mutual_friends_count", value.mutualFriendsCount);
    put(o, "guild_member", value.guildMember);
    put(o, "guild_member_profile", value.guildMemberProfile);
    put(o, "badges", value.badges);
    put(o, "legacy_username", value.legacyUsername);
    return o;
}

} // namespace RawJson
} // namespace Discord
} // namespace Acheron
