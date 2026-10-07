#pragma once

// Client glue: Acheron messages as the plugin API's MessageContext.

#include "Plugins/Types.hpp"

namespace Acheron {
namespace Discord {
struct Message;
}
} // namespace Acheron

namespace Qord {
namespace Plugins {

// `withJson` fills rawJson (only needed for context menus).
MessageContext toMessageContext(const Acheron::Discord::Message &message, Id accountId, Id guildId,
                                Id parentChannelId = 0, bool withJson = false);

// The message as the client knows it, as indented JSON with sorted keys and
// without private author fields.
QString messageToJson(const Acheron::Discord::Message &message);

} // namespace Plugins
} // namespace Qord
