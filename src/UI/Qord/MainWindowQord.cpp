// Qord additions to Acheron's MainWindow: Discord-style surfaces, the typing
// indicator, pinned messages, search and the mentions inbox. Kept out of
// MainWindow.cpp so the edits to Acheron's file stay small.

#include "UI/MainWindow.hpp"

#include "Core/ClientInstance.hpp"
#include "Core/Session.hpp"
#include "Discord/Events.hpp"
#include "Core/Theme/Manager.hpp"
#include "Discord/Client.hpp"
#include "UI/Chat/ChatModel.hpp"
#include "UI/ChannelList/ChannelTreeView.hpp"
#include "UI/ChannelList/ServerRailView.hpp"
#include "UI/Qord/MessageListPopup.hpp"
#include "UI/Qord/UserPanel.hpp"

#include <QDateTime>
#include <QPointer>
#include <QSettings>

namespace Acheron {
namespace UI {

using Core::Snowflake;
using Core::Theme::Token;

void MainWindow::setupQordFeatures()
{
    applyQordSurfaces();
    // Hover highlights on channel rows, like Discord
    channelTree->setMouseTracking(true);
    channelTree->viewport()->setAttribute(Qt::WA_Hover);
    connect(&Core::Theme::Manager::instance(), &Core::Theme::Manager::themeChanged, this,
            &MainWindow::applyQordSurfaces);

    // Typing indicator: Discord expects one POST per ~10s while typing
    connect(messageInput, &MessageInput::userTyping, this, [this]() {
        if (!currentInstance)
            return;
        const Snowflake channelId = chatModel->getActiveChannelId();
        if (!channelId.isValid())
            return;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (channelId == lastTypingChannel && now - lastTypingSentMs < 9000)
            return;
        lastTypingChannel = channelId;
        lastTypingSentMs = now;
        currentInstance->discord()->sendTyping(channelId);
    });
    // The user panel follows the first connected account until a channel is opened
    connect(session, &Core::Session::ready, this, [this](const Discord::Ready &ready) {
        if (!currentInstance)
            userPanel->setInstance(session->client(ready.user->id));
    });

    // Sending a message ends the typing state, like the official client
    connect(messageInput, &MessageInput::sendMessage, this, [this]() { lastTypingSentMs = 0; });
}

void MainWindow::applyQordSurfaces()
{
    const auto &theme = Core::Theme::Manager::instance();
    const auto setSurface = [](QWidget *w, const QColor &color) {
        if (!w)
            return;
        QPalette p = w->palette();
        p.setColor(QPalette::Base, color);
        p.setColor(QPalette::Window, color);
        w->setPalette(p);
    };
    setSurface(serverRail, theme.color(Token::RailBg));
    setSurface(channelTree, theme.color(Token::SidebarBg));
    setSurface(memberListView, theme.color(Token::SidebarBg));
    setSurface(userPanel, theme.color(Token::SidebarBg).darker(115));
}

MessageListPopup *MainWindow::ensureMessagePopup()
{
    if (!messagePopup) {
        messagePopup = new MessageListPopup(this);
        connect(messagePopup, &MessageListPopup::jumpRequested, this, &MainWindow::jumpToMessageLink);
    }
    messagePopup->setSearchHandler({}, {});
    messagePopup->setChannelNameResolver({});
    return messagePopup;
}

QString MainWindow::channelLabel(Snowflake channelId) const
{
    if (!currentInstance)
        return {};
    const auto channel = currentInstance->getChannel(channelId);
    if (!channel)
        return {};
    QString label;
    if (channel->name.hasValue() && !channel->name->isEmpty())
        label = QStringLiteral("#") + channel->name.get();
    if (channel->guildId.hasValue()) {
        if (const auto guild = currentInstance->getGuild(channel->guildId.get()))
            label += QStringLiteral("  ·  ") + guild->name.get();
    }
    return label;
}

void MainWindow::openPinnedMessages()
{
    if (!currentInstance || !chatModel->getActiveChannelId().isValid())
        return;
    MessageListPopup *popup = ensureMessagePopup();
    popup->setTitle(tr("Pinned Messages"));
    popup->showLoading();
    popup->popup(pinsButton);

    const QPointer<MessageListPopup> guard(popup);
    currentInstance->discord()->fetchPinnedMessages(
            chatModel->getActiveChannelId(), [guard](const Core::Result<QList<Discord::Message>> &result) {
                if (!guard)
                    return;
                if (!result.success()) {
                    guard->showError(tr("Couldn't load pinned messages: %1").arg(result.error));
                    return;
                }
                guard->setMessages(*result.value, tr("This channel doesn't have any pinned messages... yet."));
            });
}

void MainWindow::openMessageSearch(const QString &query)
{
    if (!currentInstance)
        return;
    const Snowflake guildId = chatModel->getActiveGuildId();
    const Snowflake channelId = chatModel->getActiveChannelId();
    if (!guildId.isValid() && !channelId.isValid())
        return;

    MessageListPopup *popup = ensureMessagePopup();
    popup->setTitle(guildId.isValid() ? tr("Search this server") : tr("Search this conversation"));
    popup->setChannelNameResolver([this](Snowflake id) { return channelLabel(id); });
    popup->setSearchHandler(tr("Search"), [this](const QString &text) {
        searchField->setText(text);
        openMessageSearch(text);
    });
    popup->setSearchText(query);
    popup->showLoading();
    if (!popup->isVisible())
        popup->popup(searchField);

    // Discord's search syntax: from:<id> and in:<id> are passed through as filters
    QUrlQuery params;
    QStringList words;
    for (const QString &word : query.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (word.startsWith(QLatin1String("from:")))
            params.addQueryItem(QStringLiteral("author_id"), word.mid(5));
        else if (word.startsWith(QLatin1String("in:")))
            params.addQueryItem(QStringLiteral("channel_id"), word.mid(3));
        else if (word == QLatin1String("has:link") || word == QLatin1String("has:image")
                 || word == QLatin1String("has:file") || word == QLatin1String("has:embed"))
            params.addQueryItem(QStringLiteral("has"), word.mid(4));
        else
            words.append(word);
    }
    if (!words.isEmpty())
        params.addQueryItem(QStringLiteral("content"), words.join(QLatin1Char(' ')));

    const QPointer<MessageListPopup> guard(popup);
    currentInstance->discord()->searchMessages(
            guildId, channelId, params, [guard](const Core::Result<QJsonDocument> &result) {
                if (!guard)
                    return;
                if (!result.success()) {
                    guard->showError(result.code == 202 ? tr("This server is still being indexed. Try again in a moment.")
                                                        : tr("Search failed: %1").arg(result.error));
                    return;
                }
                const QJsonObject obj = result.value->object();
                QList<Discord::Message> messages;
                for (const QJsonValue &group : obj.value(QStringLiteral("messages")).toArray()) {
                    const QJsonArray arr = group.toArray();
                    if (!arr.isEmpty())
                        messages.append(Discord::Message::fromJson(arr.first().toObject()));
                }
                guard->setMessages(messages, tr("No results found."),
                                   obj.value(QStringLiteral("total_results")).toInt(int(messages.size())));
            });
}

void MainWindow::openMentionsInbox()
{
    if (!currentInstance)
        return;
    MessageListPopup *popup = ensureMessagePopup();
    popup->setTitle(tr("Inbox — Mentions"));
    popup->setChannelNameResolver([this](Snowflake id) { return channelLabel(id); });
    popup->showLoading();
    popup->popup(inboxButton);

    const QPointer<MessageListPopup> guard(popup);
    currentInstance->discord()->fetchRecentMentions(25, true, [guard](const Core::Result<QList<Discord::Message>> &result) {
        if (!guard)
            return;
        if (!result.success()) {
            guard->showError(tr("Couldn't load mentions: %1").arg(result.error));
            return;
        }
        guard->setMessages(*result.value, tr("You're all caught up! No recent mentions."));
    });
}

} // namespace UI
} // namespace Acheron
