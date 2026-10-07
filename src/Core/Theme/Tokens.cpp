#include "Tokens.hpp"

namespace Acheron {
namespace Core {
namespace Theme {

const std::vector<TokenDescriptor> &registry()
{
    static const std::vector<TokenDescriptor> reg = {
        // backgrounds
        { Token::WindowBg, "window.bg", "Window background", "Backgrounds", QColor(43, 45, 49),
          QPalette::Window, false },
        { Token::BaseBg, "base.bg", "Base", "Backgrounds", QColor(49, 51, 56),
          QPalette::Base, false },
        { Token::AlternateBaseBg, "base.alt", "Alternate base", "Backgrounds", QColor(43, 45, 49),
          QPalette::AlternateBase, false },
        { Token::ButtonBg, "button.bg", "Button", "Backgrounds", QColor(56, 58, 64),
          QPalette::Button, false },
        { Token::TooltipBg, "tooltip.bg", "Tooltip background", "Backgrounds", QColor(17, 18, 20),
          QPalette::ToolTipBase, false },

        // text
        { Token::WindowText, "text.window", "Window text", "Text", QColor(219, 222, 225),
          QPalette::WindowText, false },
        { Token::PrimaryText, "text.primary", "Primary text", "Text", QColor(219, 222, 225),
          QPalette::Text, false },
        { Token::ButtonText, "text.button", "Button text", "Text", QColor(219, 222, 225),
          QPalette::ButtonText, false },
        { Token::TooltipText, "text.tooltip", "Tooltip text", "Text", QColor(219, 222, 225),
          QPalette::ToolTipText, false },
        { Token::PlaceholderText, "text.placeholder", "Placeholder text", "Text",
          QColor(109, 111, 120), QPalette::PlaceholderText, false },
        { Token::DisabledText, "text.disabled", "Disabled text", "Text", QColor(128, 132, 142),
          std::nullopt, false },
        { Token::BrightText, "text.bright", "Bright text", "Text", QColor(242, 243, 245),
          QPalette::BrightText, false },

        // accents
        { Token::Highlight, "accent.highlight", "Highlight", "Accents",
          QColor(88, 101, 242), QPalette::Highlight, false },
        { Token::HighlightedText, "accent.highlightText", "Highlighted text", "Accents",
          QColor(255, 255, 255), QPalette::HighlightedText, false },
        { Token::LinkText, "accent.link", "Links", "Accents", QColor(0, 168, 252), QPalette::Link,
          false },
        { Token::Divider, "accent.divider", "Dividers", "Accents", QColor(63, 65, 71),
          QPalette::Mid, false },

        // extended chat elements
        { Token::ChatError, "chat.error", "Failed message", "Chat", QColor(242, 63, 67),
          std::nullopt, false },
        { Token::EmbedDefault, "chat.embedDefault", "Default embed color", "Chat",
          QColor(88, 101, 242), std::nullopt, true },
        { Token::MentionText, "chat.mentionText", "Mention text", "Chat", QColor(201, 205, 251),
          std::nullopt, false },
        { Token::MentionBg, "chat.mentionBg", "Mention background", "Chat",
          QColor(88, 101, 242, 77), std::nullopt, true },

        // presence
        { Token::StatusOnline, "status.online", "Online", "Presence", QColor(35, 165, 90),
          std::nullopt, false },
        { Token::StatusIdle, "status.idle", "Idle", "Presence", QColor(240, 178, 50),
          std::nullopt, false },
        { Token::StatusDnd, "status.dnd", "Do not disturb", "Presence", QColor(242, 63, 67),
          std::nullopt, false },
        { Token::StatusOffline, "status.offline", "Offline", "Presence", QColor(128, 132, 142),
          std::nullopt, false },
        { Token::StatusStreaming, "status.streaming", "Streaming", "Presence", QColor(89, 54, 149),
          std::nullopt, false },

        // Qord: Discord-style surfaces
        { Token::RailBg, "surface.rail", "Server rail", "Surfaces", QColor(30, 31, 34), std::nullopt, false },
        { Token::SidebarBg, "surface.sidebar", "Sidebars", "Surfaces", QColor(43, 45, 49), std::nullopt, false },
        { Token::InputBg, "surface.input", "Message box", "Surfaces", QColor(56, 58, 64), std::nullopt, false },
        { Token::HoverBg, "surface.hover", "Hovered item", "Surfaces", QColor(53, 55, 60), std::nullopt, false },
        { Token::SelectedBg, "surface.selected", "Selected item", "Surfaces", QColor(64, 66, 73), std::nullopt, false },
        { Token::MutedText, "text.muted", "Muted text", "Surfaces", QColor(148, 155, 164), std::nullopt, false },
        { Token::HeaderText, "text.header", "Headers", "Surfaces", QColor(242, 243, 245), std::nullopt, false },
        { Token::MentionedMessageBg, "chat.mentionedBg", "Message that mentions you", "Chat",
          QColor(240, 177, 50, 20), std::nullopt, true },
        { Token::MentionedMessageBar, "chat.mentionedBar", "Mention bar", "Chat", QColor(240, 177, 50),
          std::nullopt, false },
    };
    return reg;
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
