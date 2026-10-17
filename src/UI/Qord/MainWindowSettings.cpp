// Qord: the main window's side of Settings: preferences that apply at once,
// the keybinds, and keeping Qord in the tray.

#include "UI/MainWindow.hpp"

#include "Core/ClientInstance.hpp"
#include "Core/Session.hpp"
#include "Discord/Client.hpp"
#include "Qord/Keybinds.hpp"
#include "Qord/Preferences.hpp"
#include "UI/ChannelList/ChannelFilterProxyModel.hpp"
#include "UI/ChannelList/ChannelNode.hpp"
#include "UI/ChannelList/ChannelTreeModel.hpp"
#include "UI/ChannelList/ChannelTreeView.hpp"
#include "UI/Chat/ChatLayout.hpp"
#include "UI/Chat/ChatModel.hpp"
#include "UI/Chat/ChatView.hpp"
#include "UI/Chat/FrameAnimator.hpp"
#include "UI/Input/MessageInput.hpp"
#include "UI/Qord/DesktopNotifier.hpp"
#include "UI/Qord/UserPanel.hpp"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QLineEdit>
#include <QMenuBar>
#include <QTimer>

namespace Acheron {
namespace UI {

using Qord::Keybinds;
namespace Pref = Qord::Pref;
using Qord::Preferences;

void MainWindow::setupPreferences()
{
    ChatLayout::groupSpacing() = Preferences::number(Pref::GroupSpacing);
    chatView->frameAnimator()->setEmojiEnabled(Preferences::animateEmoji());
    chatView->frameAnimator()->setStickersEnabled(Preferences::animateStickers());

    connect(&Preferences::instance(), &Preferences::changed, this, [this](const QString &key) {
        if (key == QLatin1String(Pref::AnimateEmoji) || key == QLatin1String(Pref::AnimateStickers)
            || key == QLatin1String(Pref::ReduceMotion)) {
            chatView->frameAnimator()->setEmojiEnabled(Preferences::animateEmoji());
            chatView->frameAnimator()->setStickersEnabled(Preferences::animateStickers());
            chatView->viewport()->update();
            return;
        }
        if (key == QLatin1String(Pref::ShowMenuBar)) {
            menuBar()->setVisible(Preferences::flag(Pref::ShowMenuBar));
            return;
        }
        // What messages look like: lay them out again
        if (key.startsWith(QLatin1String("chat/")) || key == QLatin1String(Pref::UnderlineLinks)
            || key.startsWith(QLatin1String("streamer/"))) {
            ChatLayout::groupSpacing() = Preferences::number(Pref::GroupSpacing);
            chatModel->invalidateDocCache();
            chatModel->invalidateLayout();
            chatView->doItemsLayout();
            chatView->viewport()->update();
        }
    });

    setupKeybinds();
}

void MainWindow::setupKeybinds()
{
    const auto run = [this](const QString &id) {
        if (id == QLatin1String("previousChannel"))
            stepChannel(-1, false);
        else if (id == QLatin1String("nextChannel"))
            stepChannel(1, false);
        else if (id == QLatin1String("previousUnread"))
            stepChannel(-1, true);
        else if (id == QLatin1String("nextUnread"))
            stepChannel(1, true);
        else if (id == QLatin1String("markChannelRead")) {
            if (currentInstance && channelTree->currentIndex().isValid())
                markIndexAsRead(currentInstance->accountId(),
                                channelFilterProxy->mapToSource(channelTree->currentIndex()));
        } else if (id == QLatin1String("focusMessageBox")) {
            if (messageInput->isVisible())
                messageInput->chatEditor()->setFocus(Qt::ShortcutFocusReason);
        } else if (id == QLatin1String("search")) {
            if (searchField->isVisible()) {
                searchField->setFocus(Qt::ShortcutFocusReason);
                searchField->selectAll();
            }
        } else if (id == QLatin1String("toggleMute"))
            userPanel->toggleMute();
        else if (id == QLatin1String("toggleDeafen"))
            userPanel->toggleDeafen();
        else if (id == QLatin1String("toggleStreamerMode"))
            Preferences::set(Pref::Streamer, !Preferences::flag(Pref::Streamer));
        else if (id == QLatin1String("toggleChannelList"))
            channelListToggle->toggle();
        else if (id == QLatin1String("toggleMemberList")) {
            if (memberListToggle->isEnabled())
                memberListToggle->toggle();
        } else if (id == QLatin1String("openSettings"))
            openSettingsWindow();
        else if (id == QLatin1String("reconnect")) {
            if (currentInstance)
                currentInstance->discord()->debugForceReconnect();
        }
    };

    for (const Keybinds::Action &binding : Keybinds::actions()) {
        auto *action = new QAction(binding.name, this);
        action->setShortcut(Keybinds::key(binding.id));
        action->setShortcutContext(Qt::WindowShortcut);
        connect(action, &QAction::triggered, this, [run, id = binding.id]() { run(id); });
        addAction(action);
        keybindActions.insert(binding.id, action);
    }
    // The keybinds own these keys now; two actions on one key would both stop working
    channelListToggle->setShortcut(QKeySequence());
    memberListToggle->setShortcut(QKeySequence());
    connect(&Keybinds::instance(), &Keybinds::changed, this, [this](const QString &id) {
        if (QAction *action = keybindActions.value(id))
            action->setShortcut(Keybinds::key(id));
    });
}

void MainWindow::stepChannel(int step, bool unreadOnly)
{
    QModelIndex index = channelTree->currentIndex();
    if (!index.isValid())
        index = channelFilterProxy->index(0, 0, channelTree->rootIndex());
    for (int guard = 0; index.isValid() && guard < 5000; ++guard) {
        index = step > 0 ? channelTree->indexBelow(index) : channelTree->indexAbove(index);
        if (!index.isValid())
            return;
        const auto type = ChannelNode::Type(index.data(ChannelTreeModel::TypeRole).toInt());
        const bool openable = type == ChannelNode::Type::Channel || type == ChannelNode::Type::Forum
                              || type == ChannelNode::Type::Thread || type == ChannelNode::Type::DMChannel;
        if (!openable)
            continue;
        if (unreadOnly && !index.data(ChannelTreeModel::IsUnreadRole).toBool())
            continue;
        channelTree->setCurrentIndex(index);
        channelTree->scrollTo(index);
        return;
    }
}

void MainWindow::showAtStart()
{
    if (!Preferences::flag(Pref::StartMinimized)) {
        show();
        return;
    }
    if (!hideToTray())
        showMinimized();
}

bool MainWindow::hideToTray()
{
    if (!notifier || !notifier->hasTray())
        return false;
    // Hidden in the tray is still running; closing another window must not quit
    qApp->setQuitOnLastWindowClosed(false);
    saveWindowState();
    hide();
    return true;
}

void MainWindow::showEvent(QShowEvent *event)
{
    qApp->setQuitOnLastWindowClosed(true);
    QMainWindow::showEvent(event);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && isMinimized()
        && Preferences::flag(Pref::MinimizeToTray)) {
        QTimer::singleShot(0, this, [this]() {
            if (isMinimized() && hideToTray())
                setWindowState(windowState() & ~Qt::WindowMinimized);
        });
    }
}

} // namespace UI
} // namespace Acheron
