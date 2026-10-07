#pragma once

#include <QPointer>
#include <QWidget>

#include "Core/Snowflake.hpp"

class QButtonGroup;
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

signals:
    void openDmRequested(Core::Snowflake userId);
    void profileRequested(Core::Snowflake userId);

private:
    enum class Tab { Online, All, Pending, Blocked, Add };
    void showTab(Tab tab);
    void refresh();
    void addRow(Core::Snowflake userId, int relationshipType);

    Core::ImageManager *images;
    QPointer<Core::ClientInstance> instance;
    QList<QMetaObject::Connection> connections;
    Tab current = Tab::Online;

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
