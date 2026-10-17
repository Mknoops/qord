#pragma once

#include <QtWidgets>

namespace Acheron {
namespace Core {
class ClientInstance;
} // namespace Core
namespace UI {

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
    void animateEmojiChanged(bool enabled);
    void animateStickersChanged(bool enabled);
    void animationCacheLimitChanged(int megabytes);
    void animationSizeLimitChanged(int megabytes);

private:
    void setupUi();
    void showPage(QWidget *page);

    QListWidget *categoryList;
    QStackedWidget *pages;
    QLabel *pageTitle = nullptr;
    class PluginsPage *pluginsPage = nullptr;
    class DevicesPage *devicesPage = nullptr;
};

} // namespace UI
} // namespace Acheron
