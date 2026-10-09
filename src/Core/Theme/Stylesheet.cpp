#include "Core/Theme/Stylesheet.hpp"

#include "Core/Theme/Fonts.hpp"
#include "Core/Theme/Manager.hpp"
#include "Core/Theme/Tokens.hpp"

#include <QColor>

namespace Acheron {
namespace Core {
namespace Theme {

namespace {
QString hex(const QColor &c)
{
    return c.name(QColor::HexRgb);
}

QString rgba(const QColor &c, int alpha)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}
} // namespace

// Qord: Discord-style surfaces for the main window and shared popups
static QString buildQordStyleSheet()
{
    const Manager &m = Manager::instance();
    const QColor rail = m.color(Token::RailBg);
    const QColor sidebar = m.color(Token::SidebarBg);
    const QColor chat = m.color(Token::BaseBg);
    const QColor hover = m.color(Token::HoverBg);
    const QColor selected = m.color(Token::SelectedBg);
    const QColor header = m.color(Token::HeaderText);
    const QColor muted = m.color(Token::MutedText);
    const QColor text = m.color(Token::PrimaryText);
    const QColor divider = m.color(Token::Divider);
    const QColor accent = m.color(Token::Highlight);
    const QColor menuBg = m.color(Token::TooltipBg);

    QString qss;
    qss += QStringLiteral("#ServerRail { background-color: %1; border: none; }").arg(hex(rail));
    qss += QStringLiteral("#ChannelTree, #MemberList { background-color: %1; border: none; }").arg(hex(sidebar));
    qss += QStringLiteral("#ChannelSidebar { background-color: %1; }"
                          "#GuildHeader { background-color: %1; color: %2; font-weight: 600;"
                          "  border-bottom: 1px solid rgba(0, 0, 0, 0.35); }"
                          "#ChatArea { background-color: %3; }"
                          "#ChannelHeaderName { color: %2; font-weight: 600; }"
                          "#ChannelHeaderDivider { background-color: %4; border: none; }")
                   .arg(hex(sidebar), hex(header), hex(chat), hex(divider));
    qss += QStringLiteral("#UserPanelIdentity { border-radius: 4px; }"
                          "#UserPanelIdentity:hover { background-color: %1; }"
                          "#UserPanelName { color: %2; font-weight: 600; }"
                          "#UserPanelSubtitle { color: %3; }"
                          "#UserPanelButton { border: none; border-radius: 4px; background: transparent; }"
                          "#UserPanelButton:hover { background-color: %1; }")
                   .arg(hex(selected), hex(header), hex(muted));
    qss += QStringLiteral("#ChannelSearch { background-color: %1; color: %2; border: none;"
                          "  border-radius: 4px; padding: 3px 6px; }")
                   .arg(hex(rail), hex(text));
    qss += QStringLiteral("#MessageListPopup { background-color: %1; border: 1px solid %2; border-radius: 8px; }"
                          "#MessageListPopupHeader { background-color: %2; border-top-left-radius: 8px;"
                          "  border-top-right-radius: 8px; }"
                          "#MessageListPopupStatus { color: %3; }"
                          "#MessageListPopupList { background: transparent; border: none; }"
                          "#MessageListPopupList::item { background-color: %4; color: %5; border-radius: 6px;"
                          "  padding: 8px 10px; margin: 2px 8px; }"
                          "#MessageListPopupList::item:hover { background-color: %6; }"
                          "#MessageListPopupList::item:selected { background-color: %6; color: %5; }")
                   .arg(hex(sidebar), hex(rail), hex(muted), hex(chat), hex(text), hex(hover));
    qss += QStringLiteral("#FriendsButton { background: transparent; color: %1; border: none; border-radius: 4px;"
                          "  text-align: left; padding: 8px 10px; margin: 8px 8px 4px 8px; font-weight: 500; }"
                          "#FriendsButton:hover { background-color: %2; color: %3; }"
                          "#FriendsButton:checked { background-color: %4; color: %5; }"
                          "#FriendsPage { background-color: %6; }"
                          "#FriendsHeader { border-bottom: 1px solid rgba(0, 0, 0, 0.35); }"
                          "#FriendsTab { background: transparent; color: %1; border: none; border-radius: 4px;"
                          "  padding: 2px 8px; font-weight: 500; }"
                          "#FriendsTab:hover { background-color: %2; color: %3; }"
                          "#FriendsTab:checked { background-color: %4; color: %5; }"
                          "#AddFriendTab { background-color: #248046; color: #ffffff; }"
                          "#AddFriendTab:checked { background: transparent; color: #2dc770; }"
                          "#FriendsCount { color: %1; font-weight: 600; }"
                          "#FriendsList { background: transparent; border: none; }"
                          "#FriendRow { border-top: 1px solid %7; }"
                          "#FriendAction { background-color: %8; border: none; border-radius: 18px; }"
                          "#AddFriendBox { background-color: %8; border: 1px solid %8; border-radius: 8px; }"
                          "#AddFriendField { background: transparent; border: none; color: %3; }"
                          "#PrimaryButton { background-color: %9; color: #ffffff; border: none; border-radius: 3px;"
                          "  padding: 6px 14px; font-weight: 500; }"
                          "#PrimaryButton:disabled { background-color: rgba(88, 101, 242, 0.5); color: rgba(255, 255, 255, 0.6); }")
                   .arg(hex(muted), hex(hover), hex(text), hex(selected), hex(header), hex(chat), hex(divider), hex(rail),
                        hex(accent));
    qss += QStringLiteral("#ServerRailColumn { background-color: %1; }"
                          "#AddServerButton { background-color: %2; border: none; border-radius: 24px; }"
                          "#AddServerButton:hover { background-color: #23a55a; border-radius: 16px; }")
                   .arg(hex(rail), hex(chat));
    // GIF / sticker buttons in the message box and the picker tabs
    qss += QStringLiteral("#MessageBoxTextButton { border: none; background: transparent; color: %1;"
                          "  font-weight: bold; font-size: 12px; }"
                          "#MessageBoxTextButton:hover { color: %2; }"
                          "#PickerTab { border: none; background: transparent; color: %1; padding: 4px 10px;"
                          "  border-radius: 4px; font-weight: 600; }"
                          "#PickerTab:checked { background: %3; color: %2; }"
                          "#PickerTab:hover { color: %2; }")
                   .arg(hex(muted), hex(text), hex(m.color(Token::SelectedBg)));
    // Plugin text buttons above the servers (ReadAllNotificationsButton)
    qss += QStringLiteral("#ServerListTextButton { background: transparent; border: none; color: %1;"
                          "  font-size: 13px; padding: 4px 0; }"
                          "#ServerListTextButton:hover { color: %2; }"
                          "#MemberCountBar { background: transparent; }")
                   .arg(hex(muted), hex(text));
    qss += QStringLiteral("#EmojiPicker { background-color: %1; border: 1px solid %2; border-radius: 8px; }"
                          "#EmojiPickerSearch { background-color: %2; color: %3; border: none; border-radius: 4px;"
                          "  padding: 6px 8px; }"
                          "#EmojiPickerGrid { background: transparent; border: none; }"
                          "#EmojiPickerPreview { color: %4; padding-left: 4px; }")
                   .arg(hex(sidebar), hex(rail), hex(text), hex(muted));
    qss += QStringLiteral("QMenu { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px;"
                          "  padding: 6px; }"
                          "QMenu::item { padding: 6px 24px 6px 10px; border-radius: 2px; }"
                          "QMenu::item:selected { background-color: %4; color: #ffffff; }"
                          "QMenu::item:disabled { color: %5; }"
                          "QMenu::separator { height: 1px; background: %3; margin: 4px 6px; }")
                   .arg(hex(menuBg), hex(text), hex(divider), hex(accent), hex(muted));
    qss += QStringLiteral("QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }"
                          "QScrollBar::handle:vertical { background: %1; border-radius: 4px; min-height: 30px; }"
                          "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
                          "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
                          "QScrollBar:horizontal { background: transparent; height: 8px; margin: 0; }"
                          "QScrollBar::handle:horizontal { background: %1; border-radius: 4px; min-width: 30px; }"
                          "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
                          "QSplitter::handle { background-color: %2; }")
                   .arg(hex(rail), hex(rail));
    return qss;
}

QString buildStyleSheet()
{
    const Manager &m = Manager::instance();

    const QColor baseBg = m.color(Token::BaseBg);
    const QColor buttonBg = m.color(Token::ButtonBg);
    const QColor primaryText = m.color(Token::PrimaryText);
    const QColor tooltipBg = m.color(Token::TooltipBg);
    const QColor tooltipText = m.color(Token::TooltipText);
    const QColor divider = m.color(Token::Divider);
    const QColor highlight = m.color(Token::Highlight);

    QString qss;

    constexpr qreal tooltipFontScale = 0.9;
    QString tooltipFontSize;
    const qreal uiPointSize = m.font(FontRole::Ui).pointSizeF();
    if (uiPointSize > 0)
        tooltipFontSize = QStringLiteral("  font-size: %1pt;").arg(uiPointSize * tooltipFontScale);

    qss += QStringLiteral("QToolTip {"
                          "  background-color: %1;"
                          "  color: %2;"
                          "  border: 1px solid %3;"
                          "  padding: 2px 6px;"
                          "%4"
                          "}")
                   .arg(hex(tooltipBg), hex(tooltipText), hex(divider), tooltipFontSize);

    qss += "#MemberList QScrollBar::handle:vertical { min-height: 40px; }";

    // Qord: Discord's borderless rounded message box
    Q_UNUSED(baseBg);
    Q_UNUSED(highlight);
    qss += QStringLiteral("#MessageInputBox {"
                          "  background-color: %1;"
                          "  border-radius: 8px; }"
                          "#MessageInput {"
                          "  background-color: transparent;"
                          "  border: none;"
                          "  padding: 11px 4px; }")
                   .arg(hex(m.color(Token::InputBg)));

    qss += buildQordStyleSheet();

    qss += QStringLiteral("#EmojiAutocompletePopup QListView {"
                          "  background: transparent;"
                          "  border: none; }");

    qss += QStringLiteral("#MessageActionBar {"
                          "  background-color: %1;"
                          "  border: 1px solid %2;"
                          "  border-radius: 8px;"
                          "  padding: 2px; }"
                          "#MessageActionBar QToolButton {"
                          "  background: transparent;"
                          "  border: none;"
                          "  border-radius: 6px; }"
                          "#MessageActionBar QToolButton:hover { background-color: %3; }"
                          "#MessageActionBar QToolButton:pressed { background-color: %4; }")
                   .arg(hex(buttonBg), hex(divider), rgba(primaryText, 24), rgba(primaryText, 40));

    return qss;
}

} // namespace Theme
} // namespace Core
} // namespace Acheron
