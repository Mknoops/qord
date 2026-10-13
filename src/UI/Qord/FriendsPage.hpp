#pragma once

#include <QPointer>
#include <QSet>
#include <functional>
#include <QWidget>

#include "Core/Snowflake.hpp"

class QButtonGroup;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;

namespace Acheron {
namespace Core {
class ClientInstance;
class ImageManager;
} // namespace Core

namespace UI {

// Discord's Friends page (Home): Online / All / Pending / Blocked tabs and
// Add Friend, with accept, ignore, message, remove and unblock actions.
class FriendsPage : public QWidget
{
    Q_OBJECT
public:
    FriendsPage(Core::ImageManager *images, QWidget *parent = nullptr);

    void setInstance(Core::ClientInstance *instance);
    // Asks Discord for users the page wants to show but doesn't know (plugin
    // tabs); set by the main window
    std::function<void(Core::ClientInstance *instance, const QList<Core::Snowflake> &userIds)> requestUsers;

signals:
    void openDmRequested(Core::Snowflake userId);
    void profileRequested(Core::Snowflake userId);

protected:
    void showEvent(QShowEvent *event) override;

private:
    // Plugin tabs (Hook::FriendsSections) come after Blocked, as Tab::Plugin
    enum class Tab { Online, All, Pending, Blocked, Add, Plugin };
    void rebuildPluginTabs();
    void pagesOpened();
    void showTab(Tab tab);
    void refresh();
    void addRow(Core::Snowflake userId, int relationshipType);

    Core::ImageManager *images;
    QPointer<Core::ClientInstance> instance;
    QList<QMetaObject::Connection> connections;
    Tab current = Tab::Online;
    QString currentSection; // the plugin tab's id, with Tab::Plugin
    QList<QPushButton *> pluginTabs;
    QHBoxLayout *headerLayout = nullptr;
    QPushButton *addFriendTab = nullptr;
    QSet<quint64> requestedUsers; // asked for once per account

    QButtonGroup *tabs;
    QPushButton *pendingTab;
    QStackedWidget *stack;
    QLabel *countLabel;
    QListWidget *list;
    QLineEdit *addField;
    QLabel *addStatus;
};

} // namespace UI
} // namespace Acheron
