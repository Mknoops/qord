#pragma once

#include <QtWidgets>

namespace Acheron {
namespace Core {
class ClientInstance;
} // namespace Core
namespace UI {

class SettingsForm;

class SettingsWindow : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsWindow(QWidget *parent = nullptr);
    // Qord: open the Plugins page at a plugin
    void showPlugin(const QString &pluginId);
    // Qord: Discord's Devices page, for the account in view
    void setInstance(Core::ClientInstance *instance);
    void showDevices();

signals:
    void channelListModeChanged(bool classic);
    void channelListIndentChanged();
    void roleStyleChanged();
    void animationCacheLimitChanged(int megabytes);
    void animationSizeLimitChanged(int megabytes);

private:
    void setupUi();
    void showPage(QWidget *page);
    void filterPages(const QString &query);

    // Qord: the pages made of preference rows (SettingsPages.cpp)
    SettingsForm *buildAccessibilityPage();
    SettingsForm *buildVoicePage();
    SettingsForm *buildChatPage();
    SettingsForm *buildNotificationsPage();
    SettingsForm *buildKeybindsPage();
    SettingsForm *buildWindowPage();
    SettingsForm *buildStreamerPage();
    SettingsForm *buildAdvancedPage();

    QListWidget *categoryList;
    QStackedWidget *pages;
    QLabel *pageTitle = nullptr;
    QLineEdit *search = nullptr;
    QHash<QWidget *, QStringList> pageWords; // what the search finds on each page
    class PluginsPage *pluginsPage = nullptr;
    class DevicesPage *devicesPage = nullptr;
};

} // namespace UI
} // namespace Acheron
