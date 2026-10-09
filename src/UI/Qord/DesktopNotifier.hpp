#pragma once

#include <QObject>
#include <QPointer>

#include "Core/Snowflake.hpp"

class QSystemTrayIcon;
class QWidget;

namespace Acheron {
namespace Core {
class ClientInstance;
} // namespace Core
namespace Discord {
struct Message;
}

namespace UI {

// Desktop notifications for DMs and mentions, like Discord's: a tray balloon
// / toast plus a taskbar flash. Respects Do Not Disturb, muted servers and
// channels, the channel you're looking at, and the user's setting.
class DesktopNotifier : public QObject
{
    Q_OBJECT
public:
    explicit DesktopNotifier(QWidget *window);

    static bool enabled();
    static void setEnabled(bool enabled);

    // Called for every new message from someone else that plugins did not silence
    void consider(Core::ClientInstance *instance, const Discord::Message &message, bool mentionsMe,
                  bool wasUnread, Core::Snowflake viewedChannelId);

signals:
    void openRequested(Core::Snowflake accountId, Core::Snowflake channelId, Core::Snowflake messageId);

private:
    class NotificationSound *sound = nullptr;
    QWidget *window;
    QSystemTrayIcon *tray = nullptr;
    Core::Snowflake lastAccount;
    Core::Snowflake lastChannel;
    Core::Snowflake lastMessage;
};

} // namespace UI
} // namespace Acheron
