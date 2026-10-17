#include "Tokens.hpp"

#include <QHash>

namespace Acheron {
namespace Core {
namespace Theme {

const std::vector<TokenDescriptor> &registry()
{
    static const std::vector<TokenDescriptor> reg = {
        // backgrounds
        { Token::WindowBg, "window.bg", "Window background", "Backgrounds", QColor(24, 29, 39),
          QPalette::Window, false },
        { Token::BaseBg, "base.bg", "Base", "Backgrounds", QColor(29, 35, 48),
          QPalette::Base, false },
        { Token::AlternateBaseBg, "base.alt", "Alternate base", "Backgrounds", QColor(24, 29, 39),
          QPalette::AlternateBase, false },
        { Token::ButtonBg, "button.bg", "Button", "Backgrounds", QColor(38, 45, 59),
          QPalette::Button, false },
        { Token::TooltipBg, "tooltip.bg", "Tooltip background", "Backgrounds", QColor(14, 17, 23),
          QPalette::ToolTipBase, false },

        // text
        { Token::WindowText, "text.window", "Window text", "Text", QColor(214, 220, 230),
          QPalette::WindowText, false },
        { Token::PrimaryText, "text.primary", "Primary text", "Text", QColor(214, 220, 230),
          QPalette::Text, false },
        { Token::ButtonText, "text.button", "Button text", "Text", QColor(214, 220, 230),
          QPalette::ButtonText, false },
        { Token::TooltipText, "text.tooltip", "Tooltip text", "Text", QColor(214, 220, 230),
          QPalette::ToolTipText, false },
        { Token::PlaceholderText, "text.placeholder", "Placeholder text", "Text",
          QColor(107, 116, 135), QPalette::PlaceholderText, false },
        { Token::DisabledText, "text.disabled", "Disabled text", "Text", QColor(123, 132, 150),
          std::nullopt, false },
        { Token::BrightText, "text.bright", "Bright text", "Text", QColor(243, 246, 250),
          QPalette::BrightText, false },

        // accents
        { Token::Highlight, "accent.highlight", "Highlight", "Accents",
          QColor(35, 133, 126), QPalette::Highlight, false },
        { Token::HighlightedText, "accent.highlightText", "Highlighted text", "Accents",
          QColor(255, 255, 255), QPalette::HighlightedText, false },
        { Token::LinkText, "accent.link", "Links", "Accents", QColor(111, 207, 199), QPalette::Link,
          false },
        { Token::Divider, "accent.divider", "Dividers", "Accents", QColor(42, 50, 66),
          QPalette::Mid, false },

        // extended chat elements
        { Token::ChatError, "chat.error", "Failed message", "Chat", QColor(240, 96, 93),
          std::nullopt, false },
        { Token::EmbedDefault, "chat.embedDefault", "Default embed color", "Chat",
          QColor(35, 133, 126), std::nullopt, true },
        { Token::MentionText, "chat.mentionText", "Mention text", "Chat", QColor(243, 201, 126),
          std::nullopt, false },
        { Token::MentionBg, "chat.mentionBg", "Mention background", "Chat",
          QColor(233, 162, 59, 46), std::nullopt, true },

        // presence
        { Token::StatusOnline, "status.online", "Online", "Presence", QColor(59, 182, 115),
          std::nullopt, false },
        { Token::StatusIdle, "status.idle", "Idle", "Presence", QColor(233, 162, 59),
          std::nullopt, false },
        { Token::StatusDnd, "status.dnd", "Do not disturb", "Presence", QColor(240, 96, 93),
          std::nullopt, false },
        { Token::StatusOffline, "status.offline", "Offline", "Presence", QColor(123, 132, 150),
          std::nullopt, false },
        { Token::StatusStreaming, "status.streaming", "Streaming", "Presence", QColor(124, 92, 196),
          std::nullopt, false },

        // Qord: surfaces
        { Token::RailBg, "surface.rail", "Server rail", "Surfaces", QColor(18, 22, 30), std::nullopt, false },
        { Token::SidebarBg, "surface.sidebar", "Sidebars", "Surfaces", QColor(24, 29, 39), std::nullopt, false },
        { Token::InputBg, "surface.input", "Message box", "Surfaces", QColor(37, 44, 58), std::nullopt, false },
        { Token::HoverBg, "surface.hover", "Hovered item", "Surfaces", QColor(34, 41, 55), std::nullopt, false },
        { Token::SelectedBg, "surface.selected", "Selected item", "Surfaces", QColor(42, 51, 68), std::nullopt, false },
        { Token::MutedText, "text.muted", "Muted text", "Surfaces", QColor(139, 149, 167), std::nullopt, false },
        { Token::HeaderText, "text.header", "Headers", "Surfaces", QColor(243, 246, 250), std::nullopt, false },
        { Token::MentionedMessageBg, "chat.mentionedBg", "Message that mentions you", "Chat",
          QColor(233, 162, 59, 20), std::nullopt, true },
        { Token::MentionedMessageBar, "chat.mentionedBar", "Mention bar", "Chat", QColor(233, 162, 59),
          std::nullopt, false },
    };
    return reg;
}

// Qord: Discord's colours, the palette before Qord's own (Appearance's "Discord colors")
QHash<Token, QColor> discordPalette()
{
    return {
        { Token::WindowBg, QColor(43, 45, 49) },
        { Token::BaseBg, QColor(49, 51, 56) },
        { Token::AlternateBaseBg, QColor(43, 45, 49) },
        { Token::ButtonBg, QColor(56, 58, 64) },
        { Token::TooltipBg, QColor(17, 18, 20) },
        { Token::WindowText, QColor(219, 222, 225) },
        { Token::PrimaryText, QColor(219, 222, 225) },
        { Token::ButtonText, QColor(219, 222, 225) },
        { Token::TooltipText, QColor(219, 222, 225) },
        { Token::PlaceholderText, QColor(109, 111, 120) },
        { Token::DisabledText, QColor(128, 132, 142) },
        { Token::BrightText, QColor(242, 243, 245) },
        { Token::Highlight, QColor(88, 101, 242) },
        { Token::HighlightedText, QColor(255, 255, 255) },
        { Token::LinkText, QColor(0, 168, 252) },
        { Token::Divider, QColor(63, 65, 71) },
        { Token::ChatError, QColor(242, 63, 67) },
        { Token::EmbedDefault, QColor(88, 101, 242) },
        { Token::MentionText, QColor(201, 205, 251) },
        { Token::MentionBg, QColor(88, 101, 242, 77) },
        { Token::StatusOnline, QColor(35, 165, 90) },
        { Token::StatusIdle, QColor(240, 178, 50) },
        { Token::StatusDnd, QColor(242, 63, 67) },
        { Token::StatusOffline, QColor(128, 132, 142) },
        { Token::StatusStreaming, QColor(89, 54, 149) },
        { Token::RailBg, QColor(30, 31, 34) },
        { Token::SidebarBg, QColor(43, 45, 49) },
        { Token::InputBg, QColor(56, 58, 64) },
        { Token::HoverBg, QColor(53, 55, 60) },
        { Token::SelectedBg, QColor(64, 66, 73) },
        { Token::MutedText, QColor(148, 155, 164) },
        { Token::HeaderText, QColor(242, 243, 245) },
        { Token::MentionedMessageBg, QColor(240, 177, 50, 20) },
        { Token::MentionedMessageBar, QColor(240, 177, 50) },
    };
}

const TokenDescriptor *findById(const QString &id)
{
    for (const TokenDescriptor &d : registry()) {
        if (id == QLatin1String(d.id))
            return &d;
    }
    return nullptr;
}

const TokenDescriptor &descriptor(Token token)
{
    for (const TokenDescriptor &d : registry()) {
        if (d.token == token)
            return d;
    }
    return registry().front(); // just in case
}

} // namespace Theme
} // namespace Core
} // namespace Acheron
