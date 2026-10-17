#include "SettingsWindow.hpp"

#include "AppearancePage.hpp"
#include "GeneralPage.hpp"
#include "PluginsPage.hpp"
#include "UI/Qord/DesktopNotifier.hpp"
#include "UI/Qord/DevicesPage.hpp"
#include "UI/Qord/NotificationSound.hpp"

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#ifdef ACHERON_HAVE_MINIAUDIO
#include "AudioPage.hpp"
#endif

namespace Acheron {
namespace UI {

SettingsWindow::SettingsWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle(tr("Settings"));
    setObjectName(QStringLiteral("SettingsWindow"));
    setProperty("qordForm", true); // Discord's controls (Core/Theme/Stylesheet.cpp)
    resize(1040, 720);

    setupUi();
}

void SettingsWindow::showPage(QWidget *page)
{
    for (int i = 0; i < categoryList->count(); ++i)
        if (categoryList->item(i)->data(Qt::UserRole).toInt() == pages->indexOf(page)
            && categoryList->item(i)->flags() & Qt::ItemIsEnabled) {
            categoryList->setCurrentRow(i);
            return;
        }
}

void SettingsWindow::showPlugin(const QString &pluginId)
{
    showPage(pluginsPage);
    pluginsPage->selectPlugin(pluginId);
}

void SettingsWindow::setInstance(Core::ClientInstance *instance)
{
    devicesPage->setInstance(instance);
}

void SettingsWindow::showDevices()
{
    showPage(devicesPage);
}

void SettingsWindow::setupUi()
{
    // Discord's settings: a sidebar of grouped pages, the page on the right
    // under its title, and an ESC button to close
    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto *sidebar = new QWidget(this);
    sidebar->setObjectName(QStringLiteral("SettingsSidebar"));
    sidebar->setAttribute(Qt::WA_StyledBackground);
    sidebar->setFixedWidth(232);
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(16, 40, 8, 16);
    categoryList = new QListWidget(sidebar);
    categoryList->setObjectName(QStringLiteral("SettingsNav"));
    categoryList->setFrameShape(QFrame::NoFrame);
    categoryList->setFocusPolicy(Qt::NoFocus);
    categoryList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    sidebarLayout->addWidget(categoryList);

    auto *content = new QWidget(this);
    content->setObjectName(QStringLiteral("SettingsContent"));
    content->setAttribute(Qt::WA_StyledBackground);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(40, 36, 24, 0);
    contentLayout->setSpacing(16);
    auto *titleRow = new QHBoxLayout;
    pageTitle = new QLabel(content);
    pageTitle->setObjectName(QStringLiteral("SettingsPageTitle"));
    titleRow->addWidget(pageTitle, 1);
    auto *close = new QToolButton(content);
    close->setObjectName(QStringLiteral("SettingsClose"));
    close->setText(QStringLiteral("\u2715"));
    close->setToolTip(tr("Close (Esc)"));
    close->setCursor(Qt::PointingHandCursor);
    close->setFixedSize(36, 36);
    connect(close, &QToolButton::clicked, this, &QWidget::close);
    auto *closeColumn = new QVBoxLayout;
    closeColumn->setSpacing(4);
    closeColumn->addWidget(close, 0, Qt::AlignHCenter);
    auto *escLabel = new QLabel(QStringLiteral("ESC"), content);
    escLabel->setObjectName(QStringLiteral("SettingsEsc"));
    closeColumn->addWidget(escLabel, 0, Qt::AlignHCenter);
    titleRow->addLayout(closeColumn);
    contentLayout->addLayout(titleRow);
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, &QWidget::close);

    pages = new QStackedWidget(content);
    contentLayout->addWidget(pages, 1);

    auto addHeader = [this](const QString &name) {
        auto *item = new QListWidgetItem(name.toUpper(), categoryList);
        item->setFlags(Qt::NoItemFlags);
        QFont font = categoryList->font();
        font.setPixelSize(11);
        font.setBold(true);
        item->setFont(font);
        item->setSizeHint(QSize(0, categoryList->count() > 1 ? 40 : 26));
        item->setTextAlignment(Qt::AlignLeft | Qt::AlignBottom);
    };
    auto addPage = [this](const QString &name, QWidget *page) {
        auto *item = new QListWidgetItem(name, categoryList);
        item->setData(Qt::UserRole, pages->addWidget(page));
        item->setSizeHint(QSize(0, 34));
    };

    // Qord: Discord's Devices, under User Settings
    addHeader(tr("User Settings"));
    devicesPage = new DevicesPage(this);
    addPage(tr("Devices"), devicesPage);

    addHeader(tr("App Settings"));
    auto *general = new GeneralPage(this);
    addPage(tr("General"), general);
    connect(general, &GeneralPage::animateEmojiChanged, this, &SettingsWindow::animateEmojiChanged);
    connect(general, &GeneralPage::animateStickersChanged, this, &SettingsWindow::animateStickersChanged);
    connect(general, &GeneralPage::animationCacheLimitChanged, this, &SettingsWindow::animationCacheLimitChanged);
    connect(general, &GeneralPage::animationSizeLimitChanged, this, &SettingsWindow::animationSizeLimitChanged);

    auto *appearance = new AppearancePage(this);
    addPage(tr("Appearance"), appearance);
    connect(appearance, &AppearancePage::channelListModeChanged, this, &SettingsWindow::channelListModeChanged);
    connect(appearance, &AppearancePage::channelListIndentChanged, this, &SettingsWindow::channelListIndentChanged);
    connect(appearance, &AppearancePage::roleStyleChanged, this, &SettingsWindow::roleStyleChanged);

#ifdef ACHERON_HAVE_MINIAUDIO
    addPage(tr("Audio"), new AudioPage(this));
#endif

    // Qord: Discord's Notifications settings
    {
        auto *page = new QWidget(this);
        auto *v = new QVBoxLayout(page);
        auto *desktop = new QCheckBox(tr("Enable Desktop Notifications"), page);
        desktop->setChecked(DesktopNotifier::enabled());
        connect(desktop, &QCheckBox::toggled, page, [](bool on) { DesktopNotifier::setEnabled(on); });
        v->addWidget(desktop);
        auto *desktopHelp = new QLabel(tr("Show a notification for direct messages and mentions when you are not "
                                          "looking at the conversation. Muted servers and channels, and Do Not "
                                          "Disturb, stay quiet."),
                                       page);
        desktopHelp->setWordWrap(true);
        desktopHelp->setForegroundRole(QPalette::PlaceholderText);
        v->addWidget(desktopHelp);
        auto *sound = new QCheckBox(tr("Message sound"), page);
        sound->setChecked(NotificationSound::enabled());
        connect(sound, &QCheckBox::toggled, page, [](bool on) { NotificationSound::setEnabled(on); });
        auto *preview = new QPushButton(tr("Play"), page);
        auto *player = new NotificationSound(page);
        connect(preview, &QPushButton::clicked, player, &NotificationSound::play);
        auto *soundRow = new QHBoxLayout;
        soundRow->addWidget(sound);
        soundRow->addWidget(preview);
        soundRow->addStretch();
        v->addLayout(soundRow);
        v->addStretch();
        addPage(tr("Notifications"), page);
    }

    addHeader(tr("Qord"));
    pluginsPage = new PluginsPage(this);
    addPage(tr("Plugins"), pluginsPage);

    connect(categoryList, &QListWidget::currentRowChanged, this, [this](int row) {
        const QListWidgetItem *item = categoryList->item(row);
        if (!item || !(item->flags() & Qt::ItemIsEnabled))
            return;
        pages->setCurrentIndex(item->data(Qt::UserRole).toInt());
        pageTitle->setText(item->text());
    });
    showPage(general);

    mainLayout->addWidget(sidebar);
    mainLayout->addWidget(content, 1);
}

} // namespace UI
} // namespace Acheron
