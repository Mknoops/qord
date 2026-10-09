#pragma once

#include "Plugins/Plugin.hpp"

#include <QTimer>

#include <functional>
#include <memory>

namespace Qord {
namespace Plugins {

namespace MessageClickActions {
// Upstream normalizeEmoji(), regex and all: "<a:name:id>", "name:id" and "name~1:id" become "name:id";
// anything else (including "<:name:id>", which its pattern does not match) is just trimmed
QString normalizeEmoji(const QString &emoji);
// The first emoji, then the additional ones (deduplicated, at most 8 more)
QStringList configuredReactions(const QString &reactEmoji, bool addAdditional, const QString &additional);
// "> " before every line, ending with a newline
QString quote(const QString &content);
} // namespace MessageClickActions

class MessageClickActionsPlugin : public Plugin
{
public:
    MessageClickActionsPlugin();
    ~MessageClickActionsPlugin() override;

    [[nodiscard]] Hooks hooks() const override { return Hook::MessageClicks; }
    void start(PluginContext &ctx) override { context = &ctx; }
    void stop() override;

    void onChatMouseDown(qint64 timeMs) override;
    void onMessageClicked(const MessageClickContext &click) override;

private:
    enum class Modifier { None, Shift, Ctrl, Alt, Backspace, Delete };
    static Modifier modifierFrom(const QString &value);
    static bool isModifierPressed(Modifier modifier, const MessageClickContext &click);

    void executeAction(const QString &action, const MessageClickContext &click, Modifier trigger);
    void resetClickState();
    void clearClickTimeouts();
    [[nodiscard]] int setting(const char *key) const;
    [[nodiscard]] QString text(const char *key) const;

    PluginContext *context = nullptr;

    qint64 lastMouseDownTime = 0;
    int mouseDownCount = 0;
    bool doubleClickDetected = false;
    qint64 secondMouseDownTime = 0;
    bool doubleClickFired = false;

    std::unique_ptr<QTimer> singleClickTimeout;
    std::unique_ptr<QTimer> doubleClickTimeout;
    std::unique_ptr<QTimer> reactionDelay;
    QStringList pendingReactions;
    MessageContext pendingReactionMessage;
};

} // namespace Plugins
} // namespace Qord
