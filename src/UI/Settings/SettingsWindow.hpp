#pragma once

#include <QtWidgets>

namespace Acheron {
namespace UI {

class SettingsWindow : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsWindow(QWidget *parent = nullptr);
    // Qord: open the Plugins page at a plugin
    void showPlugin(const QString &pluginId);

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

    QListWidget *categoryList;
    QStackedWidget *pages;
    class PluginsPage *pluginsPage = nullptr;
};

} // namespace UI
} // namespace Acheron
