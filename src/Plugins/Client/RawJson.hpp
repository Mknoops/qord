#pragma once

// Qord: the client's cached Discord objects as JSON (ViewRaw). Fields are
// the ones the client reads from Discord, under Discord's names; nothing
// private (email, phone, MFA) is kept on these objects.

#include "Discord/Entities.hpp"

#include <QJsonObject>

namespace Acheron {
namespace Discord {
namespace RawJson {

QJsonObject toJson(const User &value);
QJsonObject toJson(const Member &value);
QJsonObject toJson(const RoleColors &value);
QJsonObject toJson(const RoleTags &value);
QJsonObject toJson(const Role &value);
QJsonObject toJson(const ThreadMetadata &value);
QJsonObject toJson(const ForumTag &value);
QJsonObject toJson(const Channel &value);
QJsonObject toJson(const Emoji &value);
QJsonObject toJson(const Sticker &value);
QJsonObject toJson(const Guild &value);
QJsonObject toJson(const PermissionOverwrite &value);
QJsonObject toJson(const ConnectedAccount &value);
QJsonObject toJson(const MutualGuildEntry &value);
QJsonObject toJson(const UserProfileBadge &value);
QJsonObject toJson(const UserProfileData &value);
QJsonObject toJson(const GuildMemberProfile &value);
QJsonObject toJson(const UserProfile &value);

} // namespace RawJson
} // namespace Discord
} // namespace Acheron
