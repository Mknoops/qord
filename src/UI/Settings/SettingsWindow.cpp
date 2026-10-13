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
    resize(760, 540);

    setupUi();
}

void SettingsWindow::showPlugin(const QString &pluginId)
{
    categoryList->setCurrentRow(pages->indexOf(pluginsPage));
    pluginsPage->selectPlugin(pluginId);
}

void SettingsWindow::setInstance(Core::ClientInstance *instance)
{
    devicesPage->setInstance(instance);
}

void SettingsWindow::showDevices()
{
    categoryList->setCurrentRow(pages->indexOf(devicesPage));
}

void SettingsWindow::setupUi()
{
    auto *mainLayout = new QHBoxLayout(this);

    categoryList = new QListWidget(this);
    categoryList->setFixedWidth(150);

    pages = new QStackedWidget(this);

    auto addPage = [this](const QString &name, QWidget *page) {
        categoryList->addItem(name);
        pages->addWidget(page);
    };

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

    // Qord: Discord's Devices
    devicesPage = new DevicesPage(this);
    addPage(tr("Devices"), devicesPage);

    pluginsPage = new PluginsPage(this);
    addPage(tr("Plugins"), pluginsPage);

    categoryList->setCurrentRow(0);

    connect(categoryList, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);

    mainLayout->addWidget(categoryList);
    mainLayout->addWidget(pages, 1);
}

} // namespace UI
} // namespace Acheron
