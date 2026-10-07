#pragma once

#include "Permission.hpp"
#include "Types.hpp"

#include <optional>

namespace Qord {
namespace Plugins {

class PluginContext;

// Base class of every native plugin. All hooks default to no-ops; a plugin
// overrides the ones it needs and lists them in hooks() and in its manifest.
// The host refuses to start a plugin whose hooks() and manifest disagree, and
// only dispatches a hook if its permission is declared and granted.
//
// A plugin is constructed when it's enabled and destroyed when disabled, so
// disabled plugins cost nothing. Allocate in start(), free in stop().
class Plugin
{
public:
    virtual ~Plugin() = default;

    [[nodiscard]] virtual Hooks hooks() const = 0;

    virtual void start(PluginContext &context) { Q_UNUSED(context); }
    virtual void stop() {}
    virtual void onSettingChanged(const QString &key) { Q_UNUSED(key); }

    // Hook::BeforeSend, Hook::BeforeEdit
    virtual void onBeforeSend(OutgoingMessage &message) { Q_UNUSED(message); }
    virtual void onBeforeEdit(OutgoingMessage &message) { Q_UNUSED(message); }

    // Hook::ShouldMentionOnReply. nullopt = no opinion.
    virtual std::optional<bool> shouldMentionOnReply(const ReplyContext &reply)
    {
        Q_UNUSED(reply);
        return std::nullopt;
    }

    // Hook::TransformCopiedMessageLink
    virtual QString transformCopiedMessageLink(const QString &url) { return url; }

    // Hook::TransformIncomingContent
    virtual void transformIncomingContent(IncomingMessage &message) { Q_UNUSED(message); }
};

} // namespace Plugins
} // namespace Qord
