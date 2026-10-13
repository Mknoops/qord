#pragma once

#include <QFrame>

#include <functional>

#include "Core/Snowflake.hpp"
#include "Discord/Entities.hpp"

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QTabBar;
class QToolButton;

namespace Acheron {
namespace UI {

// A Discord-style drop-down listing messages: pinned messages, search results
// and the mentions inbox. Activating a row asks the window to jump there.
class MessageListPopup : public QFrame
{
    Q_OBJECT
public:
    explicit MessageListPopup(QWidget *parent = nullptr);

    void setTitle(const QString &title);
    // Shows a search box; submitting it calls the handler with the text
    void setSearchHandler(const QString &placeholder, std::function<void(const QString &)> handler);
    void setSearchText(const QString &text);
    // Resolves "#channel" labels shown above each message (search, inbox)
    void setChannelNameResolver(std::function<QString(Core::Snowflake)> resolver);

    void showLoading();
    void showError(const QString &error);
    void setMessages(const QList<Discord::Message> &messages, const QString &emptyText, int totalResults = -1);

    void popup(QWidget *anchor);

    // Qord: tabs under the title (the inbox's Mentions and plugin tabs); an
    // empty list hides them
    void setTabs(const QStringList &labels, int current);
    // Qord: a "Clear All" button in the header, and "Remove" on each message
    // (plugin inbox tabs); null handlers hide them
    void setClearHandler(std::function<void()> handler);
    void setRemoveHandler(std::function<void(Core::Snowflake messageId)> handler);

signals:
    void jumpRequested(Core::Snowflake channelId, Core::Snowflake messageId);
    void tabSelected(int index);

private:
    void addMessageItem(const Discord::Message &message);

    QLabel *titleLabel;
    QLineEdit *searchField;
    QLabel *statusLabel;
    QListWidget *list;
    QTabBar *tabs;
    QToolButton *clearButton;
    std::function<void()> clearHandler;
    std::function<void(Core::Snowflake)> removeHandler;
    std::function<void(const QString &)> searchHandler;
    std::function<QString(Core::Snowflake)> channelNameResolver;
};

} // namespace UI
} // namespace Acheron
