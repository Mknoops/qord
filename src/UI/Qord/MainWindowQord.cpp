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
#include "Core/Emoji/EmojiManager.hpp"
#include "Core/MessageManager.hpp"
#include "UI/Chat/ChatView.hpp"
#include "UI/Qord/EmojiPicker.hpp"
#include "UI/Qord/FriendsPage.hpp"
#include "UI/Settings/SettingsWindow.hpp"
#include "Core/RelationshipManager.hpp"
#include "Core/UserManager.hpp"
#include "Plugins/Client/ClientPlugins.hpp"
#include "Plugins/PluginHost.hpp"
#include "Plugins/Client/MessageContexts.hpp"
#include "UI/TypingIndicator.hpp"
#include "Core/TypingTracker.hpp"
#include "UI/Qord/AddServerDialog.hpp"
#include "UI/Qord/DesktopNotifier.hpp"
#include "UI/Qord/MessageListPopup.hpp"
#include "UI/Qord/UserPanel.hpp"
#include "UI/Qord/NameDecorations.hpp"
#include "UI/Qord/PluginSurfaces.hpp"
#include "UI/ChannelList/ChannelTreeModel.hpp"
#include "UI/MemberList/MemberListView.hpp"
#include "UI/ImageViewer.hpp"
#include "Discord/CdnUrls.hpp"
#ifndef ACHERON_NO_VOICE
#include "Core/Audio/VoiceManager.hpp"
#endif
#include "Core/MemberListManager.hpp"

#include <QDateTime>
#include <QJsonObject>
#include <QMenu>
#include "Core/ReadStateManager.hpp"
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
        // Plugins (SilentTyping) may keep the typing indicator to ourselves
        if (auto *host = Qord::Plugins::PluginHost::instance(); host && host->hasHook(Qord::Plugins::Hook::Typing)) {
            Qord::Plugins::TypingContext typing;
            typing.accountId = quint64(currentInstance->accountId());
            typing.channelId = Qord::Plugins::pluginId(channelId);
            typing.guildId = Qord::Plugins::pluginId(chatModel->getActiveGuildId());
            typing.connectedVoiceChannelId = Qord::Plugins::pluginId(currentInstance->voiceChannelId());
            for (int row = chatModel->rowCount() - 1; row >= 0; --row) {
                const QModelIndex idx = chatModel->index(row, 0);
                if (Snowflake(idx.data(ChatModel::UserIdRole).toULongLong()) == currentInstance->accountId()) {
                    typing.lastOwnMessageAt = idx.data(ChatModel::TimestampRole).toDateTime();
                    break;
                }
            }
            if (!host->dispatchShouldSendTyping(typing))
                return;
        }
        currentInstance->discord()->sendTyping(channelId);
    });
    // The user panel follows the first connected account until a channel is opened
    connect(session, &Core::Session::ready, this, [this](const Discord::Ready &ready) {
        if (!currentInstance)
            userPanel->setInstance(session->client(ready.user->id));
    });

    // Emoji picker: the message box button and Add Reaction
    emojiPicker = new EmojiPicker(session->getImageManager(), this);
    connect(messageInput, &MessageInput::emojiButtonClicked, this, [this](QWidget *anchor) {
        if (!currentInstance || !chatModel->getActiveChannelId().isValid())
            return;
        disconnect(emojiPicker, &EmojiPicker::emojiPicked, nullptr, nullptr);
        connect(emojiPicker, &EmojiPicker::emojiPicked, this, [this](const Core::EmojiMatch &match) {
            messageInput->insertText(match.isCustom() ? match.insertText : match.surrogates);
            messageInput->setFocus();
        });
        emojiPicker->open(currentInstance, chatModel->getActiveChannelId(), false, anchor);
    });
    connect(chatView, &ChatView::addReactionRequested, this, [this](Snowflake channelId, Snowflake messageId) {
        if (!currentInstance)
            return;
        disconnect(emojiPicker, &EmojiPicker::emojiPicked, nullptr, nullptr);
        connect(emojiPicker, &EmojiPicker::emojiPicked, this, [this, channelId, messageId](const Core::EmojiMatch &match) {
            if (currentInstance)
                currentInstance->messages()->addReaction(channelId, messageId, EmojiPicker::reactionKey(match), false);
        });
        emojiPicker->open(currentInstance, channelId, true, chatView);
        emojiPicker->moveNear(QCursor::pos()); // next to the pointer, like Discord's reaction picker
    });

    // Desktop notifications for DMs and mentions
    notifier = new DesktopNotifier(this);
    connect(notifier, &DesktopNotifier::openRequested, this,
            [this](Snowflake, Snowflake channelId, Snowflake messageId) { jumpToMessageLink(channelId, messageId); });

    // Add a Server
    connect(addServerButton, &QToolButton::clicked, this, [this]() {
        Core::ClientInstance *instance = session->client(railSelectedAccountId);
        if (!instance)
            instance = currentInstance;
        if (!instance)
            return;
        (new AddServerDialog(instance, this))->open();
    });

    // Mute / notification settings on server and channel menus
    serverRail->setMenuExtender([this](QMenu *menu, Snowflake accountId, Snowflake guildId) {
        addNotificationMenu(menu, session->client(accountId), guildId, Snowflake::Invalid);
        addPluginChannelItems(menu, accountId, guildId, Snowflake::Invalid);
    });
    channelTree->setMenuExtender([this](QMenu *menu, Snowflake accountId, Snowflake guildId, Snowflake channelId) {
        addNotificationMenu(menu, session->client(accountId), guildId, channelId);
        addPluginChannelItems(menu, accountId, guildId, channelId);
    });

    // Friends page: open a DM from it
    connect(friendsPage, &FriendsPage::openDmRequested, this, [this](Snowflake userId) {
        Core::ClientInstance *instance = currentInstance ? currentInstance : session->client(railSelectedAccountId);
        if (!instance)
            return;
        const QPointer<MainWindow> guard(this);
        const Snowflake accountId = instance->accountId();
        instance->openDmChannel(userId, [guard, accountId](const Core::Result<Snowflake> &result) {
            if (!guard || !result.success())
                return;
            guard->hideFriendsPage();
            guard->selectChannelInTree(accountId, *result.value);
        });
    });

    chatModel->setReplyNameResolver([this](const Discord::User &author, Snowflake guildId) {
        return currentInstance ? qordAuthorName(author, guildId, true) : QString();
    });

    // Plugins: message box insertion, re-render on presentation changes
    connect(this, &QObject::destroyed, []() {
        Qord::Plugins::QtUiService::pluginSettingsOpener() = nullptr;
        Qord::Plugins::QtUiService::messageBoxInserter() = nullptr;
        Qord::Plugins::QtUiService::imageOpener() = nullptr;
        Qord::Plugins::QtReadStates::allServersMarker() = nullptr;
    });
    Qord::Plugins::QtUiService::imageOpener() = [this](const QUrl &url) { openImageInViewer(url); };
    // ReadAllNotificationsButton: every server of the account, like Mark As Read on each
    Qord::Plugins::QtReadStates::allServersMarker() = [this](quint64 accountId) {
        if (Core::ClientInstance *instance = session->client(Snowflake(accountId)))
            instance->markGuildsAsRead(channelTreeModel->guildIds(Snowflake(accountId)));
    };

    // Plugin surfaces: icons after names, member counts, server list buttons
    nameDecorations = new NameDecorations(session, this);
    for (const auto &instance : session->getClients())
        nameDecorations->watch(instance);
    connect(nameDecorations, &NameDecorations::changed, this, [this]() {
        memberListView->viewport()->update();
        chatView->viewport()->update();
    });
    memberCountBar = new MemberCountBar(memberListView);
    memberListView->setHeaderWidget(memberCountBar);
    serverRail->setTooltipExtender([this](Snowflake accountId, Snowflake guildId) {
        return memberCountTooltip(accountId, guildId);
    });
    connect(session, &Core::Session::ready, this, [this]() { refreshPluginSurfaces(); });
    refreshPluginSurfaces();
    Qord::Plugins::QtUiService::pluginSettingsOpener() = [this](const QString &pluginId) {
        openSettingsWindow();
        if (settingsWindow)
            settingsWindow->showPlugin(pluginId);
    };
    Qord::Plugins::QtUiService::messageBoxInserter() = [this](const QString &text) {
        messageInput->insertText(text);
        messageInput->setFocus();
    };
    if (auto *host = Qord::Plugins::PluginHost::instance()) {
        // Display plugins changed (TextReplace, Unindent, DontRoundMyTimestamps):
        // re-render the messages on screen from their stored content
        connect(host, &Qord::Plugins::PluginHost::displayTransformsChanged, this, [this]() {
            if (!currentInstance)
                return;
            const Snowflake channelId = chatModel->getActiveChannelId();
            QList<Snowflake> ids;
            for (int row = 0; row < chatModel->rowCount(); ++row)
                ids.append(Snowflake(chatModel->index(row, 0).data(ChatModel::MessageIdRole).toULongLong()));
            for (Snowflake id : ids)
                currentInstance->messages()->refreshMessage(channelId, id);
        });
        connect(host, &Qord::Plugins::PluginHost::presentationChanged, this, [this]() {
            chatModel->invalidateDocCache();
            chatModel->invalidateLayout();
            chatView->viewport()->update();
            memberListView->viewport()->update();
            refreshTypers();
            refreshPluginSurfaces();
        });
        connect(host, &Qord::Plugins::PluginHost::pluginStateChanged, this, [this]() { refreshPluginSurfaces(); });
    }

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

void MainWindow::refreshPluginSurfaces()
{
    if (serverListButtons) {
        Snowflake accountId = railSelectedAccountId;
        if (!accountId.isValid() && currentInstance)
            accountId = currentInstance->accountId();
        if (!accountId.isValid())
            for (const auto &instance : session->getClients())
                if (instance) {
                    accountId = instance->accountId();
                    break;
                }
        serverListButtons->refresh(accountId);
    }
    refreshMemberCountBar();
}

void MainWindow::refreshMemberCountBar()
{
    if (!memberCountBar)
        return;
    auto *host = Qord::Plugins::PluginHost::instance();
    const Snowflake guildId = chatModel->getActiveGuildId();
    QList<Qord::Plugins::CountBadge> badges;
    if (host && host->hasHook(Qord::Plugins::Hook::MemberCounts) && currentInstance && guildId.isValid()) {
        Qord::Plugins::MemberCountContext ctx;
        ctx.accountId = quint64(currentInstance->accountId());
        ctx.guildId = Qord::Plugins::pluginId(guildId);
        ctx.channelId = Qord::Plugins::pluginId(chatModel->getActiveChannelId());
        ctx.place = Qord::Plugins::MemberCountContext::Place::MemberList;
        Core::MemberListManager *list = currentInstance->memberList();
        ctx.total = list->guildMemberCount(guildId);
        ctx.online = list->currentGuildId() == guildId ? list->activeListOnlineCount() : -1;
        if (ctx.online < 0)
            ctx.online = list->guildOnlineCount(guildId);
        ctx.inVoice = currentInstance->guildVoiceUserCount(guildId);
        badges = host->dispatchMemberCountBadges(ctx);
    }
    memberCountBar->setBadges(badges);
    memberListView->updateHeader();
}

QString MainWindow::memberCountTooltip(Snowflake accountId, Snowflake guildId)
{
    auto *host = Qord::Plugins::PluginHost::instance();
    Core::ClientInstance *instance = session->client(accountId);
    if (!host || !instance || !host->hasHook(Qord::Plugins::Hook::MemberCounts))
        return {};
    Qord::Plugins::MemberCountContext ctx;
    ctx.accountId = quint64(accountId);
    ctx.guildId = Qord::Plugins::pluginId(guildId);
    ctx.place = Qord::Plugins::MemberCountContext::Place::ServerTooltip;
    ctx.total = instance->memberList()->guildMemberCount(guildId);
    ctx.online = instance->memberList()->guildOnlineCount(guildId);
    return MemberCountBar::badgesHtml(host->dispatchMemberCountBadges(ctx));
}

void MainWindow::openImageInViewer(const QUrl &url)
{
    Core::ClientInstance *instance = currentInstance;
    if (!instance)
        for (const auto &client : session->getClients())
            if (client) {
                instance = client;
                break;
            }
    if (!instance)
        return;
    auto *viewer = new ImageViewer(session->getImageManager(), instance->accountId(), this);
    viewer->showImage(url, QPixmap());
}

void MainWindow::connectQordInstance(Core::ClientInstance *instance)
{
    const QPointer<Core::ClientInstance> guard(instance);
    if (nameDecorations)
        nameDecorations->watch(instance);
    // MemberCount: new numbers from Discord, people joining or leaving voice
    connect(instance->memberList(), &Core::MemberListManager::countsChanged, this, [this, guard](Snowflake guildId) {
        if (guard && guard == currentInstance && guildId == chatModel->getActiveGuildId())
            refreshMemberCountBar();
    });
#ifndef ACHERON_NO_VOICE
    connect(instance->voice(), &Core::Audio::VoiceManager::channelVoiceMemberChanged, this, [this, guard]() {
        if (guard && guard == currentInstance)
            refreshMemberCountBar();
    });
#endif
    connect(instance, &Core::ClientInstance::notifiableMessage, this,
            [this, guard](const Discord::Message &message, bool mentionsMe, bool wasUnread) {
                if (guard && notifier)
                    notifier->consider(guard, message, mentionsMe, wasUnread, chatModel->getActiveChannelId());
            });
}

void MainWindow::addNotificationMenu(QMenu *menu, Core::ClientInstance *instance, Snowflake guildId,
                                     Snowflake channelId)
{
    if (!instance)
        return;
    if (channelId.isValid()) {
        const auto channel = instance->getChannel(channelId);
        guildId = channel && channel->guildId.hasValue() ? channel->guildId.get() : Snowflake::Invalid;
    }
    const QPointer<Core::ClientInstance> guard(instance);
    const auto patch = [guard, guildId](const QJsonObject &body) {
        if (guard)
            guard->discord()->updateGuildNotificationSettings(guildId, body, [](const Core::Result<void> &) {});
    };

    const bool isChannel = channelId.isValid();
    const bool muted = isChannel ? instance->readState()->isChannelMuted(channelId)
                                 : instance->readState()->isGuildMuted(guildId);
    const QString what = isChannel ? (guildId.isValid() ? tr("Channel") : tr("Conversation")) : tr("Server");
    QAction *mute = menu->addAction(muted ? tr("Unmute %1").arg(what) : tr("Mute %1").arg(what));
    connect(mute, &QAction::triggered, this, [patch, channelId, isChannel, muted]() {
        QJsonObject state{ { "muted", !muted }, { "mute_config", QJsonValue::Null } };
        if (isChannel)
            patch(QJsonObject{ { "channel_overrides", QJsonObject{ { QString::number(quint64(channelId)), state } } } });
        else
            patch(state);
    });

    if (!guildId.isValid())
        return; // DMs only mute
    QMenu *levels = menu->addMenu(tr("Notification Settings"));
    int current = -1;
    if (const auto settings = instance->readState()->guildSettings(guildId)) {
        if (isChannel && settings->channelOverrides.hasValue()) {
            for (const auto &o : settings->channelOverrides.get()) {
                if (o.channelId.hasValue() && o.channelId.get() == channelId && o.messageNotifications.hasValue())
                    current = int(o.messageNotifications.get());
            }
        } else if (!isChannel && settings->messageNotifications.hasValue()) {
            current = int(settings->messageNotifications.get());
        }
    }
    const QList<QPair<int, QString>> options = {
        { isChannel ? 3 : -2, isChannel ? tr("Use Server Default") : QString() },
        { 0, tr("All Messages") },
        { 1, tr("Only @mentions") },
        { 2, tr("Nothing") },
    };
    for (const auto &[level, label] : options) {
        if (label.isEmpty())
            continue;
        QAction *a = levels->addAction(label);
        a->setCheckable(true);
        a->setChecked(level == current || (level == 3 && current == -1 && isChannel));
        connect(a, &QAction::triggered, this, [patch, channelId, isChannel, level]() {
            if (isChannel)
                patch(QJsonObject{ { "channel_overrides",
                                     QJsonObject{ { QString::number(quint64(channelId)),
                                                    QJsonObject{ { "message_notifications", level } } } } } });
            else
                patch(QJsonObject{ { "message_notifications", level } });
        });
    }
}

void MainWindow::refreshTypers()
{
    if (auto *host = Qord::Plugins::PluginHost::instance(); host && host->hasHook(Qord::Plugins::Hook::Typing)) {
        Qord::Plugins::ChatContext chat;
        chat.accountId = currentInstance ? quint64(currentInstance->accountId()) : 0;
        chat.channelId = Qord::Plugins::pluginId(chatModel->getActiveChannelId());
        chat.guildId = Qord::Plugins::pluginId(chatModel->getActiveGuildId());
        if (!host->dispatchShouldShowTypers(chat)) {
            typingIndicator->setTypers({});
            return;
        }
    }
    typingIndicator->setTypers(typingTracker->getActiveTypers());
}

QString MainWindow::qordAuthorName(const Discord::User &author, Snowflake guildId, bool reply)
{
    const QString fallback = currentInstance->users()->getAuthorDisplayName(author, guildId);
    auto *host = Qord::Plugins::PluginHost::instance();
    if (!host || !host->hasHook(Qord::Plugins::Hook::AuthorNames))
        return fallback;
    Qord::Plugins::AuthorContext ctx;
    ctx.accountId = quint64(currentInstance->accountId());
    ctx.guildId = Qord::Plugins::pluginId(guildId);
    ctx.channelId = Qord::Plugins::pluginId(chatModel->getActiveChannelId());
    ctx.userId = quint64(author.id.get());
    ctx.username = author.username.get();
    ctx.globalName = author.globalName.hasValue() ? author.globalName.get() : QString();
    if (guildId.isValid())
        ctx.nick = currentInstance->users()->getNickname(author.id.get(), guildId);
    ctx.friendNickname = currentInstance->relationships()->getNickname(author.id.get());
    ctx.isBot = author.bot.hasValue() && author.bot.get();
    ctx.discriminator = author.discriminator.hasValue() ? author.discriminator.get() : QStringLiteral("0");
    ctx.place = reply ? Qord::Plugins::AuthorContext::Place::Reply : Qord::Plugins::AuthorContext::Place::Message;
    return host->dispatchAuthorDisplayName(ctx).value_or(fallback);
}

void MainWindow::addPluginChannelItems(QMenu *menu, Snowflake accountId, Snowflake guildId, Snowflake channelId)
{
    auto *host = Qord::Plugins::PluginHost::instance();
    Core::ClientInstance *instance = session->client(accountId);
    if (!host || !instance || !host->hasHook(Qord::Plugins::Hook::ChannelContextMenu))
        return;
    Qord::Plugins::ChannelContext ctx;
    ctx.accountId = quint64(accountId);
    ctx.channelId = Qord::Plugins::pluginId(channelId);
    if (channelId.isValid()) {
        if (const auto channel = instance->getChannel(channelId)) {
            ctx.guildId = channel->guildId.hasValue() ? quint64(channel->guildId.get()) : 0;
            ctx.name = channel->name.hasValue() ? channel->name.get() : QString();
            ctx.type = channel->type.hasValue() ? int(channel->type.get()) : -1;
            if (channel->type.hasValue() && channel->type.get() == Discord::ChannelType::GROUP_DM)
                ctx.iconUrl = Discord::Cdn::channelIcon(channelId, channel->icon.valueOr(), 128).toString();
        }
    } else {
        ctx.guildId = Qord::Plugins::pluginId(guildId);
        if (const auto guild = instance->getGuild(guildId)) {
            ctx.name = guild->name.get();
            ctx.iconUrl = Discord::Cdn::guildIcon(guildId, guild->icon.valueOr(), 128).toString();
            ctx.bannerUrl = Discord::Cdn::guildBanner(guildId, guild->banner.valueOr(), 480).toString();
        }
    }
    const auto items = host->dispatchChannelContextMenuItems(ctx);
    if (items.isEmpty())
        return;
    menu->addSeparator();
    std::function<void(QMenu *, const QString &, const Qord::Plugins::MenuItem &)> addItem =
            [&](QMenu *target, const QString &pluginId, const Qord::Plugins::MenuItem &item) {
                if (!item.children.isEmpty()) {
                    QMenu *sub = target->addMenu(item.label);
                    for (const auto &child : item.children)
                        addItem(sub, pluginId, child);
                    return;
                }
                QAction *action = target->addAction(item.label);
                connect(action, &QAction::triggered, this, [pluginId, itemId = item.id, ctx]() {
                    if (auto *h = Qord::Plugins::PluginHost::instance())
                        h->dispatchChannelContextMenuAction(pluginId, itemId, ctx);
                });
            };
    for (const auto &entry : items)
        addItem(menu, entry.pluginId, entry.item);
}

void MainWindow::showFriendsPage()
{
    Core::ClientInstance *instance = session->client(railSelectedAccountId);
    if (!instance)
        instance = currentInstance;
    friendsPage->setInstance(instance);
    if (!friendsPage->isVisible())
        tabBar->setProperty("qordWasVisible", tabBar->isVisible());
    tabBar->hide();
    channelToolbar->hide();
    centerSplitter->hide();
    setMemberListVisible(false);
    friendsPage->show();
    friendsButton->setChecked(true);
}

void MainWindow::hideFriendsPage()
{
    if (!friendsPage || !friendsPage->isVisible())
        return;
    friendsPage->hide();
    friendsButton->setChecked(false);
    tabBar->setVisible(tabBar->property("qordWasVisible").toBool());
    centerSplitter->show();
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
