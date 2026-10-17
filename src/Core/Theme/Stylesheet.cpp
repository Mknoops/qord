#include "Core/Theme/Stylesheet.hpp"

#include "Core/Theme/Fonts.hpp"
#include "Core/Theme/Manager.hpp"
#include "Core/Theme/Tokens.hpp"

#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QStringList>

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

// An SVG for a stylesheet's image: url(), written once to the cache (QSS
// images can't be recoloured, so each colour gets its own file)
QString themedImage(const QString &svg)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/theme");
    const QString name = QString::fromLatin1(
            QCryptographicHash::hash(svg.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
    const QString path = dir + QLatin1Char('/') + name + QStringLiteral(".svg");
    if (!QFile::exists(path)) {
        QDir().mkpath(dir);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
            file.write(svg.toUtf8());
    }
    return QDir::fromNativeSeparators(path);
}

QString chevron(const QColor &color, bool up)
{
    return themedImage(QStringLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24">)"
                                      R"(<path d="%1" fill="none" stroke="%2" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/></svg>)")
                               .arg(up ? QStringLiteral("M6 15l6-6 6 6") : QStringLiteral("M6 9l6 6 6-6"), hex(color)));
}

// The rule for each selector inside dialogs and the windows marked
// qordForm (settings): Discord's controls there, Qt's defaults elsewhere
QString scoped(const QString &selectors, const QString &body)
{
    QStringList out;
    for (const QString &selector : selectors.split(QLatin1Char(','))) {
        out << QStringLiteral("QDialog %1").arg(selector.trimmed());
        out << QStringLiteral("*[qordForm=\"true\"] %1").arg(selector.trimmed());
    }
    return out.join(QStringLiteral(", ")) + QStringLiteral(" { ") + body + QStringLiteral(" }");
}

// Discord's buttons, fields, checkboxes, menus and sliders for forms
QString buildFormStyleSheet()
{
    const Manager &m = Manager::instance();
    const QColor surface = m.color(Token::BaseBg);
    const QColor field = m.color(Token::RailBg);
    const QColor text = m.color(Token::PrimaryText);
    const QColor header = m.color(Token::HeaderText);
    const QColor muted = m.color(Token::MutedText);
    const QColor accent = m.color(Token::Highlight);
    const QColor secondary = m.color(Token::ButtonBg).lighter(140);
    const QColor divider = m.color(Token::Divider);
    const QColor danger = m.color(Token::StatusDnd);
    const QString check = themedImage(QStringLiteral(
            R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24">)"
            R"(<path d="M5 12.5l4.5 4.5L19 7.5" fill="none" stroke="#ffffff" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/></svg>)"));

    QString qss;
    qss += QStringLiteral("QDialog, *[qordForm=\"true\"] { background-color: %1; color: %2; }").arg(hex(surface), hex(text));
    qss += QStringLiteral("QDialogButtonBox { dialogbuttonbox-buttons-have-icons: 0; }");
    qss += scoped(QStringLiteral("QPushButton"),
                  QStringLiteral("background-color: %1; color: #ffffff; border: none; border-radius: 4px;"
                                 " padding: 6px 16px; min-height: 20px; font-weight: 500;")
                          .arg(hex(secondary)));
    qss += scoped(QStringLiteral("QPushButton:hover"), QStringLiteral("background-color: %1;").arg(hex(secondary.lighter(118))));
    qss += scoped(QStringLiteral("QPushButton:pressed"), QStringLiteral("background-color: %1;").arg(hex(secondary.darker(115))));
    qss += scoped(QStringLiteral("QPushButton:default, QPushButton#PrimaryButton"),
                  QStringLiteral("background-color: %1;").arg(hex(accent)));
    qss += scoped(QStringLiteral("QPushButton:default:hover, QPushButton#PrimaryButton:hover"),
                  QStringLiteral("background-color: %1;").arg(hex(accent.darker(118))));
    qss += scoped(QStringLiteral("QPushButton#DangerButton"), QStringLiteral("background-color: %1;").arg(hex(danger)));
    qss += scoped(QStringLiteral("QPushButton#DangerButton:hover"), QStringLiteral("background-color: %1;").arg(hex(danger.darker(125))));
    qss += scoped(QStringLiteral("QPushButton:disabled"), QStringLiteral("background-color: %1; color: %2;").arg(rgba(secondary, 120), hex(muted)));
    qss += scoped(QStringLiteral("QPushButton:flat"), QStringLiteral("background: transparent; color: %1; padding: 4px 8px;").arg(hex(text)));
    qss += scoped(QStringLiteral("QPushButton:flat:hover"), QStringLiteral("text-decoration: underline;"));

    // Text tool buttons (Reset, Inside) as small secondary buttons; auto-raise
    // ones are icons and stay flat
    qss += scoped(QStringLiteral("QToolButton[autoRaise=\"false\"]"),
                  QStringLiteral("background-color: %1; color: #ffffff; border: none; border-radius: 4px; padding: 4px 10px;")
                          .arg(hex(secondary)));
    qss += scoped(QStringLiteral("QToolButton[autoRaise=\"false\"]:hover"),
                  QStringLiteral("background-color: %1;").arg(hex(secondary.lighter(118))));
    qss += scoped(QStringLiteral("QToolButton[autoRaise=\"false\"]:checked"),
                  QStringLiteral("background-color: %1;").arg(hex(accent)));
    qss += scoped(QStringLiteral("QToolButton[autoRaise=\"false\"]:disabled"),
                  QStringLiteral("background-color: %1; color: %2;").arg(rgba(secondary, 120), hex(muted)));
    qss += scoped(QStringLiteral("QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox, QComboBox, QKeySequenceEdit"),
                  QStringLiteral("background-color: %1; color: %2; border: 1px solid %1; border-radius: 4px;"
                                 " padding: 6px 8px; selection-background-color: %3;")
                          .arg(hex(field), hex(text), hex(accent)));
    qss += scoped(QStringLiteral("QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus"),
                  QStringLiteral("border-color: %1;").arg(hex(accent)));
    qss += scoped(QStringLiteral("QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled"),
                  QStringLiteral("color: %1;").arg(hex(muted)));
    qss += scoped(QStringLiteral("QComboBox"), QStringLiteral("padding-right: 28px;"));
    qss += scoped(QStringLiteral("QComboBox::drop-down"),
                  QStringLiteral("border: none; width: 26px; subcontrol-origin: padding; subcontrol-position: center right;"));
    qss += scoped(QStringLiteral("QComboBox::down-arrow"),
                  QStringLiteral("image: url(%1); width: 14px; height: 14px;").arg(chevron(muted, false)));
    qss += scoped(QStringLiteral("QComboBox QAbstractItemView"),
                  QStringLiteral("background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 4px;"
                                 " outline: none; selection-background-color: %4; selection-color: %5;")
                          .arg(hex(m.color(Token::SidebarBg)), hex(text), hex(field), hex(m.color(Token::HoverBg)), hex(header)));
    qss += scoped(QStringLiteral("QSpinBox, QDoubleSpinBox"), QStringLiteral("padding-right: 22px;"));
    qss += scoped(QStringLiteral("QSpinBox::up-button, QDoubleSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::down-button"),
                  QStringLiteral("border: none; background: transparent; width: 20px;"));
    qss += scoped(QStringLiteral("QSpinBox::up-arrow, QDoubleSpinBox::up-arrow"),
                  QStringLiteral("image: url(%1); width: 10px; height: 10px;").arg(chevron(muted, true)));
    qss += scoped(QStringLiteral("QSpinBox::down-arrow, QDoubleSpinBox::down-arrow"),
                  QStringLiteral("image: url(%1); width: 10px; height: 10px;").arg(chevron(muted, false)));

    qss += scoped(QStringLiteral("QCheckBox, QRadioButton"), QStringLiteral("color: %1; spacing: 10px; padding: 2px 0;").arg(hex(text)));
    qss += scoped(QStringLiteral("QCheckBox::indicator, QRadioButton::indicator"),
                  QStringLiteral("width: 18px; height: 18px; border: 2px solid %1; background: transparent;").arg(hex(muted)));
    qss += scoped(QStringLiteral("QCheckBox::indicator"), QStringLiteral("border-radius: 5px;"));
    qss += scoped(QStringLiteral("QRadioButton::indicator"), QStringLiteral("border-radius: 11px;"));
    qss += scoped(QStringLiteral("QCheckBox::indicator:hover, QRadioButton::indicator:hover"),
                  QStringLiteral("border-color: %1;").arg(hex(header)));
    qss += scoped(QStringLiteral("QCheckBox::indicator:checked"),
                  QStringLiteral("background-color: %1; border-color: %1; image: url(%2);").arg(hex(accent), check));
    qss += scoped(QStringLiteral("QRadioButton::indicator:checked"),
                  QStringLiteral("border: 6px solid %1; background-color: #ffffff;").arg(hex(accent)));
    qss += scoped(QStringLiteral("QCheckBox::indicator:disabled"), QStringLiteral("border-color: %1;").arg(hex(divider)));

    qss += scoped(QStringLiteral("QAbstractItemView::indicator"),
                  QStringLiteral("width: 16px; height: 16px; border: 2px solid %1; border-radius: 4px; background: transparent;")
                          .arg(hex(muted)));
    qss += scoped(QStringLiteral("QAbstractItemView::indicator:checked"),
                  QStringLiteral("background-color: %1; border-color: %1; image: url(%2);").arg(hex(accent), check));
    qss += scoped(QStringLiteral("QGroupBox"),
                  QStringLiteral("background-color: %1; border: none; border-radius: 8px; margin-top: 28px; padding: 12px 14px 10px 14px;"
                                 " color: %2;")
                          .arg(hex(m.color(Token::SidebarBg)), hex(text)));
    qss += scoped(QStringLiteral("QGroupBox::title"),
                  QStringLiteral("subcontrol-origin: margin; subcontrol-position: top left; left: 2px; top: 6px;"
                                 " color: %1; font-weight: 700; font-size: 11px;")
                          .arg(hex(header)));
    qss += scoped(QStringLiteral("QSlider::groove:horizontal"),
                  QStringLiteral("height: 8px; background: %1; border-radius: 4px;").arg(hex(field)));
    qss += scoped(QStringLiteral("QSlider::sub-page:horizontal"),
                  QStringLiteral("background: %1; border-radius: 4px;").arg(hex(accent)));
    qss += scoped(QStringLiteral("QSlider::handle:horizontal"),
                  QStringLiteral("background: #ffffff; width: 10px; margin: -6px 0; border-radius: 3px;"));
    qss += scoped(QStringLiteral("QProgressBar"),
                  QStringLiteral("background-color: %1; border: none; border-radius: 4px; color: %2; text-align: center; height: 8px;")
                          .arg(hex(field), hex(text)));
    qss += scoped(QStringLiteral("QProgressBar::chunk"), QStringLiteral("background-color: %1; border-radius: 4px;").arg(hex(accent)));
    qss += scoped(QStringLiteral("QTabBar::tab"),
                  QStringLiteral("background: transparent; color: %1; padding: 6px 12px; border: none; border-bottom: 2px solid transparent;")
                          .arg(hex(muted)));
    qss += scoped(QStringLiteral("QTabBar::tab:selected"),
                  QStringLiteral("color: %1; border-bottom-color: %2;").arg(hex(header), hex(accent)));
    qss += scoped(QStringLiteral("QTabBar::tab:hover"), QStringLiteral("color: %1;").arg(hex(text)));
    qss += scoped(QStringLiteral("QTabWidget::pane"), QStringLiteral("border: none; border-top: 1px solid %1;").arg(hex(divider)));
    qss += scoped(QStringLiteral("QScrollArea"), QStringLiteral("background: transparent; border: none;"));
    qss += scoped(QStringLiteral("QScrollArea > QWidget > QWidget"), QStringLiteral("background: transparent;"));
    return qss;
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
    // Settings: Discord's sidebar of pages and the page under its title
    qss += QStringLiteral("#SettingsSidebar { background-color: %1; }"
                          "#SettingsContent { background-color: %2; }"
                          "#SettingsNav { background: transparent; border: none; outline: none; }"
                          "#GuildSettingsNav { background-color: %1; border: none; outline: none; padding: 24px 8px 8px 16px; }"
                          "#SettingsNav::item, #GuildSettingsNav::item { padding: 0 10px; margin: 1px 0; border-radius: 4px; }"
                          "#SettingsNav::item { color: %3; }"
                          "#SettingsNav::item:hover, #GuildSettingsNav::item:hover { background-color: %4; }"
                          "#SettingsNav::item:hover { color: %5; }"
                          "#SettingsNav::item:selected, #GuildSettingsNav::item:selected { background-color: %6; }"
                          "#SettingsNav::item:selected { color: %7; }"
                          "#SettingsNav::item:disabled, #GuildSettingsNav::item:disabled { background: transparent; }"
                          "#SettingsNav::item:disabled { color: %3; }"
                          "QPushButton#SettingsCardButton { background-color: %1; padding: 0; min-height: 48px; border-radius: 4px; text-align: left; }"
                          "QPushButton#SettingsCardButton:hover { background-color: %4; }"
                          "QPushButton#SettingsCardButton:checked { background-color: %6; }"
                          "#SettingsPageTitle { color: %7; font-size: 20px; font-weight: 600; }"
                          "#SettingsClose { color: %3; background: transparent; border: 2px solid %3; padding: 0;"
                          "  border-radius: 18px; font-size: 14px; }"
                          "#SettingsClose:hover { color: %7; border-color: %7; background-color: %4; }"
                          "#SettingsEsc { color: %3; font-size: 11px; font-weight: 600; }"
                          "#SettingsHelp { color: %3; }"
                          "#SettingsSectionTitle { color: %7; font-size: 12px; font-weight: 700; }"
                          "#PluginList { background-color: %1; border: none; border-radius: 8px; padding: 6px; outline: none; }"
                          "#PluginList::item { color: %5; padding-left: 6px; border-radius: 4px; }"
                          "#PluginList::item:hover { background-color: %4; }"
                          "#PluginList::item:selected { background-color: %6; color: %7; }"
                          "#SettingsContent QSplitter::handle { background: transparent; }")
                   .arg(hex(sidebar), hex(chat), hex(muted), hex(hover), hex(text), hex(selected), hex(header));
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
    qss += buildFormStyleSheet();

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
