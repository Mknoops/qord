#pragma once

#include <QWidget>

#include "Core/Snowflake.hpp"
#include "Plugins/Types.hpp"

class QHBoxLayout;
class QVBoxLayout;

namespace Acheron {
namespace UI {

// Plugin buttons above the servers in the server list
// (Hook::ServerListButtons, e.g. ReadAllNotificationsButton's "Read All").
// Hidden when no plugin adds one.
class ServerListButtonBar : public QWidget
{
    Q_OBJECT
public:
    explicit ServerListButtonBar(QWidget *parent = nullptr);
    void refresh(Core::Snowflake accountId, int serverCount = 0);

private:
    QVBoxLayout *layout;
    Core::Snowflake accountId;
};

// Member numbers above the member list (Hook::MemberCounts, MemberCount).
class MemberCountBar : public QWidget
{
    Q_OBJECT
public:
    explicit MemberCountBar(QWidget *parent = nullptr);
    void setBadges(const QList<Qord::Plugins::CountBadge> &badges);
    // The badges as rich text, for tooltips (server list)
    static QString badgesHtml(const QList<Qord::Plugins::CountBadge> &badges);

private:
    QHBoxLayout *layout;
    QStringList shown; // what is on screen, to skip identical refreshes
};

} // namespace UI
} // namespace Acheron
