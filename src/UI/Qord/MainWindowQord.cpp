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
#include "UI/Qord/ExpressionPicker.hpp"
#include "UI/Qord/SlashCommandPopup.hpp"
#include <QJsonDocument>
#include <QToolTip>
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
#include "UI/ChannelList/ChannelFilterProxyModel.hpp"
#include "UI/ChannelList/ChannelNode.hpp"
#include "UI/MemberList/MemberListView.hpp"
#include "UI/ImageViewer.hpp"
#include "UI/ChannelList/ChannelDelegate.hpp"
#ifndef ACHERON_NO_VOICE
#include "UI/VoiceStatusBar.hpp"
#endif
#ifdef QORD_HAVE_GO_LIVE
#include "UI/Qord/CameraUi.hpp"
#include "UI/Qord/GoLiveUi.hpp"
#endif
#include <QTimer>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include "Plugins/Client/MessageContexts.hpp"
#include "Core/PermissionManager.hpp"
#include "Discord/CdnUrls.hpp"
#ifndef ACHERON_NO_VOICE
#include "Core/Audio/VoiceManager.hpp"
#endif
#include "Core/MemberListManager.hpp"

#include <QDateTime>
#include <QLineEdit>
#include <QMessageBox>
#include <QInputDialog>
#include <QJsonObject>
#include <QMenu>
#include "Core/ReadStateManager.hpp"
#include <QPointer>
#include <QSettings>

namespace Acheron {
namespace UI {

using Core::Snowflake;
using Core::Theme::Token;

namespace {
// MessageClickActions: every press in the chat, then clicks with their count
// (like the browser's click detail) and the keys held
class ChatClickWatcher : public QObject
{
public:
    using ClickHandler = std::function<void(const QPoint &pos, int detail, Qt::MouseButton button)>;

    ChatClickWatcher(QWidget *viewport, QObject *parent) : QObject(parent), viewport(viewport)
    {
        viewport->installEventFilter(this);
        qApp->installEventFilter(this);
    }

    ClickHandler onClick;
    bool backspaceHeld = false;
    bool deleteHeld = false;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::KeyPress:
        case QEvent::KeyRelease: {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->isAutoRepeat())
                break;
            const bool down = event->type() == QEvent::KeyPress;
            if (key->key() == Qt::Key_Backspace)
                backspaceHeld = down;
            else if (key->key() == Qt::Key_Delete)
                deleteHeld = down;
            break;
        }
        case QEvent::ApplicationDeactivate:
            backspaceHeld = deleteHeld = false;
            break;
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
            if (watched == viewport) {
                auto *mouse = static_cast<QMouseEvent *>(event);
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                const QPoint pos = mouse->pos();
                if (now - lastPress < QApplication::doubleClickInterval() &&
                    (pos - lastPos).manhattanLength() < QApplication::startDragDistance() * 2)
                    detail = qMin(detail + 1, 3);
                else
                    detail = 1;
                lastPress = now;
                lastPos = pos;
                if (auto *host = Qord::Plugins::PluginHost::instance(); host && host->hasHook(Qord::Plugins::Hook::MessageClicks))
                    host->dispatchChatMouseDown(now);
            }
            break;
        case QEvent::MouseButtonRelease:
            if (watched == viewport && onClick) {
                auto *mouse = static_cast<QMouseEvent *>(event);
                onClick(mouse->pos(), detail, mouse->button());
            }
            break;
        default:
            break;
        }
        return false;
    }

private:
    QWidget *viewport;
    qint64 lastPress = 0;
    QPoint lastPos;
    int detail = 0;
};
} // namespace

void MainWindow::addGoLiveItems(QMenu *menu, Core::ClientInstance *instance, Snowflake userId)
{
#ifdef QORD_HAVE_GO_LIVE
    if (goLiveUi)
        goLiveUi->addUserMenuItems(menu, instance, userId);
#else
    Q_UNUSED(menu);
    Q_UNUSED(instance);
    Q_UNUSED(userId);
#endif
}

bool MainWindow::isSystemDm(Core::ClientInstance *instance, Snowflake channelId) const
{
    const auto channel = instance ? instance->getChannel(channelId) : std::nullopt;
    if (!channel)
        return false;
    if (channel->recipients.hasValue())
        for (const auto &user : channel->recipients.get())
            if (user.isSystem())
                return true;
    if (channel->recipientIds.hasValue())
        for (const Snowflake id : channel->recipientIds.get())
            if (quint64(id) == Discord::User::SystemUserId)
                return true;
    return false;
}

bool MainWindow::canCreateThreadsIn(Core::ClientInstance *instance, Snowflake channelId) const
{
    if (!instance)
        return false;
    const auto channel = instance->getChannel(channelId);
    if (!channel || channel->isThread() || !channel->type.hasValue())
        return false;
    const auto type = channel->type.get();
    if (type != Discord::ChannelType::GUILD_TEXT && type != Discord::ChannelType::GUILD_NEWS)
        return false;
    return instance->permissions()->hasChannelPermission(instance->accountId(), channelId,
                                                         Discord::Permission::CREATE_PUBLIC_THREADS);
}

void MainWindow::createThreadFromMessage(Snowflake channelId, Snowflake messageId, const QString &suggestedName)
{
    if (!currentInstance)
        return;
    QInputDialog dialog(this);
    dialog.setWindowTitle(tr("Create Thread"));
    dialog.setLabelText(tr("Thread name"));
    dialog.setTextValue(suggestedName);
    if (auto *edit = dialog.findChild<QLineEdit *>()) {
        edit->setMaxLength(100); // Discord's limit
        edit->setMinimumWidth(400);
    }
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString name = dialog.textValue().trimmed();
    if (name.isEmpty())
        return;
    const QPointer<Core::ClientInstance> instance(currentInstance);
    const QPointer<MainWindow> self(this);
    currentInstance->discord()->createThreadFromMessage(
            channelId, messageId, name, [self, instance](const Core::Result<Discord::Channel> &result) {
                if (!self || !instance)
                    return;
                if (!result.success()) {
                    QMessageBox::warning(self, tr("Create Thread"), tr("Couldn't create the thread: %1").arg(result.error));
                    return;
                }
                const Snowflake threadId = result.value->id.get();
                // THREAD_CREATE brings the thread into the channel list; it may
                // come before or after this reply
                if (instance->getChannel(threadId)) {
                    self->navigateToChannel(threadId);
                    return;
                }
                auto connection = std::make_shared<QMetaObject::Connection>();
                *connection = connect(instance->discord(), &Discord::Client::threadCreated, self,
                                      [self, threadId, connection](const Discord::ChannelCreate &event) {
                                          if (event.channel->id.get() != threadId)
                                              return;
                                          disconnect(*connection);
                                          QTimer::singleShot(0, self, [self, threadId]() {
                                              if (self)
                                                  self->navigateToChannel(threadId);
                                          });
                                      });
                // Don't wait forever for an event that may never come
                QTimer::singleShot(10000, self, [connection]() { disconnect(*connection); });
            });
}

void MainWindow::setupQordFeatures()
{
    applyQordSurfaces();
#ifdef QORD_HAVE_GO_LIVE
    // Go Live: share your screen from the voice panel, LIVE badges, watching
    goLiveUi = new GoLiveUi(session, voiceStatusBar, this);
    ChannelDelegate::setLiveProvider([this](quint64 accountId, quint64 userId) {
        return goLiveUi && goLiveUi->isLive(accountId, userId);
    });
    // Cameras: the call's connection asks for video from now on
    Core::Audio::VoiceManager::setVideoSupported(true);
    cameraUi = new CameraUi(session, voiceStatusBar, this);
    ChannelDelegate::setCameraProvider([this](quint64 accountId, quint64 userId) {
        return cameraUi && cameraUi->hasCamera(accountId, userId);
    });
    connect(this, &QObject::destroyed, []() {
        ChannelDelegate::setLiveProvider(nullptr);
        ChannelDelegate::setCameraProvider(nullptr);
    });
#endif
#ifndef ACHERON_NO_VOICE
    // As in Discord: clicking a voice channel joins it, clicking someone who
    // is live opens their stream
    connect(channelTree, &QAbstractItemView::clicked, this, [this](const QModelIndex &proxyIndex) {
        if (QGuiApplication::keyboardModifiers() != Qt::NoModifier)
            return;
        auto *node = channelTreeModel->nodeFromIndex(channelFilterProxy->mapToSource(proxyIndex));
        if (!node)
            return;
        ChannelNode *accountNode = channelTreeModel->getAccountNodeFor(node);
        Core::ClientInstance *instance = accountNode ? session->client(accountNode->id) : nullptr;
        if (!instance)
            return;
        if (node->type == ChannelNode::Type::VoiceChannel) {
            if (instance->voiceChannelId() != node->id)
                emit channelTree->joinVoiceChannelRequested(proxyIndex);
            return;
        }
#ifdef QORD_HAVE_GO_LIVE
        if (node->type == ChannelNode::Type::VoiceParticipant && goLiveUi && node->id != instance->accountId() &&
            goLiveUi->isLive(quint64(instance->accountId()), quint64(node->id)))
            goLiveUi->watch(instance, node->id);
        else if (node->type == ChannelNode::Type::VoiceParticipant && cameraUi && instance->isInVoice() &&
                 cameraUi->hasCamera(quint64(instance->accountId()), quint64(node->id)))
            cameraUi->showVideo(instance);
#endif
    });
#endif
    // Threads from messages
    connect(chatView, &ChatView::createThreadRequested, this, &MainWindow::createThreadFromMessage);
    connect(chatView, &ChatView::openThreadRequested, this, &MainWindow::navigateToChannel);

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

    // MessageClickActions: clicks on messages, and the edit/reply it can start
    auto *clickWatcher = new ChatClickWatcher(chatView->viewport(), this);
    clickWatcher->onClick =          [this, clickWatcher](const QPoint &pos, int detail, Qt::MouseButton button) {
                                         auto *host = Qord::Plugins::PluginHost::instance();
                                         if (!host || !host->hasHook(Qord::Plugins::Hook::MessageClicks) || !currentInstance)
                                             return;
                                         const QModelIndex idx = chatView->indexAt(pos);
                                         const Discord::Message *msg = idx.isValid() ? chatModel->messageAt(idx.row()) : nullptr;
                                         if (!msg)
                                             return;
                                         Qord::Plugins::MessageClickContext click;
                                         const Snowflake channelId = chatModel->getActiveChannelId();
                                         const Snowflake guildId = chatModel->getActiveGuildId();
                                         click.message = Qord::Plugins::toMessageContext(
                                                 *msg, quint64(currentInstance->accountId()), quint64(guildId));
                                         if (const auto channel = currentInstance->getChannel(channelId); channel && channel->type.hasValue()) {
                                             click.message.isDm = channel->type.get() == Discord::ChannelType::DM;
                                             click.message.isGroupDm = channel->type.get() == Discord::ChannelType::GROUP_DM;
                                         }
                                         click.detail = detail;
                                         click.primaryButton = button == Qt::LeftButton;
                                         const auto mods = QGuiApplication::queryKeyboardModifiers();
                                         click.shift = mods & Qt::ShiftModifier;
                                         click.ctrl = mods & Qt::ControlModifier;
                                         click.alt = mods & Qt::AltModifier;
                                         click.backspace = clickWatcher->backspaceHeld;
                                         click.deleteKey = clickWatcher->deleteHeld;
                                         click.timeMs = QDateTime::currentMSecsSinceEpoch();
                                         const Snowflake me = currentInstance->accountId();
                                         auto *perms = currentInstance->permissions();
                                         if (guildId.isValid()) {
                                             click.canSend = perms->hasChannelPermission(me, channelId, Discord::Permission::SEND_MESSAGES);
                                             click.canManageMessages = perms->hasChannelPermission(me, channelId, Discord::Permission::MANAGE_MESSAGES);
                                             click.canReact = perms->hasChannelPermission(me, channelId, Discord::Permission::ADD_REACTIONS) &&
                                                              perms->hasChannelPermission(me, channelId, Discord::Permission::READ_MESSAGE_HISTORY);
                                         }
                                         click.pinned = msg->pinned;
                                         if (msg->reactions.hasValue())
                                             for (const auto &r : msg->reactions.get())
                                                 if (r.me.get())
                                                     click.myReactions.append(r.emoji->id.isNull() || !r.emoji->id.get().isValid()
                                                                                      ? r.emoji->name.get()
                                                                                      : r.emoji->name.get() + QLatin1Char(':') +
                                                                                                QString::number(quint64(r.emoji->id.get())));
                                         click.selectedText = chatView->selectedChatText();
                                         host->dispatchMessageClicked(click);
                                     };
    Qord::Plugins::QtUiService::editStarter() = [this](const Qord::Plugins::MessageContext &message) {
        chatView->startEditingMessage(Snowflake(message.messageId));
    };
    Qord::Plugins::QtUiService::replyStarter() = [this](const Qord::Plugins::MessageContext &message, bool,
                                                        std::optional<bool> mention) {
        if (!mention) {
            // Discord's own reply: shift and reply-mention plugins decide the ping
            emit chatView->replyToMessageRequested(Snowflake(message.channelId), Snowflake(message.messageId));
            return;
        }
        const int row = chatModel->rowForMessage(Snowflake(message.messageId));
        const QModelIndex idx = chatModel->index(row, 0);
        messageInput->setReplyTarget(Snowflake(message.messageId), idx.data(ChatModel::UsernameRole).toString(),
                                     idx.data(ChatModel::ContentRole).toString(), *mention);
    };

    // Slash commands
    slashPopup = new SlashCommandPopup(messageInput->editor(), messageInput, [this]() { return slashIndex(); });

    // GIF and sticker pickers
    expressionPicker = new ExpressionPicker(session->getImageManager(), this);
    const auto openExpressions = [this](ExpressionPicker::Tab tab, QWidget *anchor) {
        if (!currentInstance || !chatModel->getActiveChannelId().isValid())
            return;
        expressionPicker->open(currentInstance, chatModel->getActiveChannelId(), chatModel->getActiveGuildId(), tab,
                               anchor);
    };
    connect(messageInput, &MessageInput::gifButtonClicked, this,
            [openExpressions](QWidget *anchor) { openExpressions(ExpressionPicker::Tab::Gifs, anchor); });
    connect(messageInput, &MessageInput::stickerButtonClicked, this,
            [openExpressions](QWidget *anchor) { openExpressions(ExpressionPicker::Tab::Stickers, anchor); });
    connect(expressionPicker, &ExpressionPicker::emojiTabRequested, messageInput, &MessageInput::emojiButtonClicked);
    // A GIF is sent as its link, through the normal send path (plugins, slowmode, replies)
    connect(expressionPicker, &ExpressionPicker::gifPicked, this,
            [this](const QString &url) { emit messageInput->sendMessage(url, {}); });
    connect(expressionPicker, &ExpressionPicker::stickerPicked, this, [this](Snowflake stickerId) {
        if (!currentInstance || !chatModel->getActiveChannelId().isValid())
            return;
        const Snowflake channelId = chatModel->getActiveChannelId();
        const Snowflake replyTo = messageInput->replyTargetMessageId();
        // Plugins see the sticker like any send (FakeNitro may turn it into a link)
        QString content;
        auto *host = Qord::Plugins::PluginHost::instance();
        if (host && host->hasHook(Qord::Plugins::Hook::BeforeSend)) {
            Qord::Plugins::OutgoingMessage out;
            out.accountId = quint64(currentInstance->accountId());
            out.channelId = Qord::Plugins::pluginId(channelId);
            out.guildId = Qord::Plugins::pluginId(currentInstance->discord()->getGuildIdForChannel(channelId));
            out.replyToMessageId = quint64(replyTo);
            out.stickerIds = { quint64(stickerId) };
            host->dispatchBeforeSend(out);
            if (out.cancel)
                return;
            content = out.content;
            if (out.stickerIds.isEmpty()) {
                messageInput->clearReplyTarget();
                if (!content.trimmed().isEmpty())
                    currentInstance->messages()->sendMessage(channelId, content, replyTo, {},
                                                             messageInput->replyMentionEnabled(), false);
                return;
            }
            stickerId = Snowflake(out.stickerIds.first());
        }
        messageInput->clearReplyTarget();
        currentInstance->discord()->sendSticker(channelId, stickerId, replyTo, [](const Core::Result<void> &) {},
                                                content);
    });
    Qord::Plugins::QtUiService::fileAttacher() = [this](const QString &fileName, const QByteArray &data,
                                                        const QString &mimeType) {
        messageInput->attachData(fileName, data, mimeType);
    };

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
        Qord::Plugins::QtUiService::editStarter() = nullptr;
        Qord::Plugins::QtUiService::fileAttacher() = nullptr;
        Qord::Plugins::QtUiService::replyStarter() = nullptr;
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

    // Voice labels (CallTimer): participants in the channel list, and the
    // connection panel; ticking once a second while a plugin shows them
    ChannelDelegate::setVoiceLabelProvider([](quint64 accountId, quint64 guildId, quint64 channelId,
                                              quint64 userId) -> std::optional<ChannelDelegate::VoiceLabel> {
        auto *host = Qord::Plugins::PluginHost::instance();
        if (!host || !host->hasHook(Qord::Plugins::Hook::VoiceStates))
            return std::nullopt;
        Qord::Plugins::VoiceParticipantContext ctx;
        ctx.accountId = accountId;
        ctx.guildId = guildId;
        ctx.channelId = channelId;
        ctx.userId = userId;
        ctx.isSelf = userId == accountId;
        ctx.nowMs = QDateTime::currentMSecsSinceEpoch();
        const auto label = host->dispatchVoiceParticipantLabel(ctx);
        if (!label)
            return std::nullopt;
        return ChannelDelegate::VoiceLabel { label->text, label->inlineText };
    });
    connect(this, &QObject::destroyed, []() { ChannelDelegate::setVoiceLabelProvider(nullptr); });
    auto *voiceTick = new QTimer(this);
    voiceTick->setInterval(1000);
    connect(voiceTick, &QTimer::timeout, this, [this]() {
        auto *host = Qord::Plugins::PluginHost::instance();
        const bool active = host && host->hasHook(Qord::Plugins::Hook::VoiceStates);
#ifndef ACHERON_NO_VOICE
        QString text;
        if (active) {
            for (const auto &instance : session->getClients()) {
                if (!instance || !instance->isInVoice())
                    continue;
                Qord::Plugins::VoiceConnectionContext ctx;
                ctx.accountId = quint64(instance->accountId());
                ctx.userId = quint64(instance->accountId());
                ctx.guildId = Qord::Plugins::pluginId(instance->voiceGuildId());
                ctx.channelId = Qord::Plugins::pluginId(instance->voiceChannelId());
                ctx.nowMs = QDateTime::currentMSecsSinceEpoch();
                text = host->dispatchVoiceConnectionText(ctx).value_or(QString());
                break;
            }
        }
        if (voiceStatusBar)
            voiceStatusBar->setExtraText(text);
#endif
        if (active)
            channelTree->viewport()->update();
    });
    voiceTick->start();
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
    connect(messageInput, &MessageInput::sendMessage, this, [this]() {
        lastTypingSentMs = 0;
        chatModel->setUnreadMarker(Snowflake::Invalid); // Discord drops the NEW divider once you reply
    });
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

QString MainWindow::slashScopeKey() const
{
    if (!currentInstance || !chatModel->getActiveChannelId().isValid())
        return {};
    const Snowflake guildId = chatModel->getActiveGuildId();
    return guildId.isValid() ? QStringLiteral("%1:g%2").arg(quint64(currentInstance->accountId())).arg(quint64(guildId))
                             : QStringLiteral("%1:c%2")
                                       .arg(quint64(currentInstance->accountId()))
                                       .arg(quint64(chatModel->getActiveChannelId()));
}

const Qord::SlashCommands::Index *MainWindow::slashIndex()
{
    const QString key = slashScopeKey();
    if (key.isEmpty())
        return nullptr;
    auto it = slashIndexes.constFind(key);
    if (it != slashIndexes.constEnd())
        return &it.value();
    if (!slashLoading.contains(key)) {
        slashLoading.insert(key);
        const QPointer<MainWindow> guard(this);
        currentInstance->discord()->fetchCommandIndex(
                chatModel->getActiveGuildId(), chatModel->getActiveChannelId(),
                [guard, key](const Core::Result<QJsonDocument> &result) {
                    if (!guard)
                        return;
                    guard->slashLoading.remove(key);
                    guard->slashIndexes.insert(key, result.success() ? Qord::SlashCommands::parseIndex(result.value->object())
                                                                     : Qord::SlashCommands::Index());
                    if (guard->slashPopup && guard->slashScopeKey() == key)
                        guard->slashPopup->refresh();
                });
    }
    return nullptr;
}

bool MainWindow::trySendSlashCommand(const QString &text)
{
    if (!text.startsWith(QLatin1Char('/')) || !currentInstance)
        return false;
    const Qord::SlashCommands::Index *index = slashIndex();
    if (!index || !Qord::SlashCommands::find(*index, text))
        return false; // not a command here: send it as a message, like Discord
    const Qord::SlashCommands::Built built = Qord::SlashCommands::build(*index, text);
    if (!built.ok) {
        // Keep what was typed and say what is wrong
        messageInput->insertText(text);
        QToolTip::showText(messageInput->mapToGlobal(QPoint(12, -8)), built.error, messageInput);
        return true;
    }
    QJsonObject payload {
        { QStringLiteral("type"), 2 },
        { QStringLiteral("application_id"), built.command->applicationId },
        { QStringLiteral("channel_id"), QString::number(quint64(chatModel->getActiveChannelId())) },
        { QStringLiteral("session_id"), currentInstance->discord()->gatewaySessionId() },
        { QStringLiteral("data"), built.data },
        { QStringLiteral("nonce"), QString::number(quint64(Snowflake::fromUnixMs(QDateTime::currentMSecsSinceEpoch()))) },
        { QStringLiteral("analytics_location"), QStringLiteral("slash_ui") },
    };
    if (chatModel->getActiveGuildId().isValid())
        payload.insert(QStringLiteral("guild_id"), QString::number(quint64(chatModel->getActiveGuildId())));
    const QPointer<MainWindow> guard(this);
    currentInstance->discord()->sendInteraction(payload, [guard, text](const Core::Result<void> &result) {
        if (!guard || result.success())
            return;
        guard->messageInput->insertText(text);
        QToolTip::showText(guard->messageInput->mapToGlobal(QPoint(12, -8)),
                           tr("The command could not be sent"), guard->messageInput);
    });
    return true;
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
    connect(instance->voice(), &Core::Audio::VoiceManager::channelVoiceMemberChanged, this,
            [this, guard](Snowflake channelId, Snowflake userId, bool joined) {
                if (!guard)
                    return;
                if (guard == currentInstance)
                    refreshMemberCountBar();
                // CallTimer: joins, moves and leaves of other users
                auto *host = Qord::Plugins::PluginHost::instance();
                if (!host || !host->hasHook(Qord::Plugins::Hook::VoiceStates))
                    return;
                Qord::Plugins::VoiceStateChange change;
                change.accountId = quint64(guard->accountId());
                change.userId = quint64(userId);
                const auto channel = guard->getChannel(channelId);
                change.guildId = channel && channel->guildId.hasValue() ? quint64(channel->guildId.get()) : 0;
                change.channelId = joined ? quint64(channelId) : 0;
                change.atMs = QDateTime::currentMSecsSinceEpoch();
                host->dispatchVoiceStateChanged(change);
            });
#endif
    // Our own voice connection
    connect(instance, &Core::ClientInstance::voiceStateChanged, this,
            [guard](Snowflake channelId, Snowflake guildId) {
                auto *host = Qord::Plugins::PluginHost::instance();
                if (!guard || !host || !host->hasHook(Qord::Plugins::Hook::VoiceStates))
                    return;
                Qord::Plugins::VoiceStateChange change;
                change.accountId = quint64(guard->accountId());
                change.userId = quint64(guard->accountId());
                change.guildId = Qord::Plugins::pluginId(guildId);
                change.channelId = Qord::Plugins::pluginId(channelId);
                change.isSelf = true;
                change.atMs = QDateTime::currentMSecsSinceEpoch();
                host->dispatchVoiceStateChanged(change);
            });
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
