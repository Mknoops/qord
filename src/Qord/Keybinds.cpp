#include "Keybinds.hpp"

#include <QCoreApplication>
#include <QSettings>

namespace Qord {

namespace {
QString settingsKey(const QString &id)
{
    return QStringLiteral("keybinds/") + id;
}

QString tr(const char *text)
{
    return QCoreApplication::translate("Qord::Keybinds", text);
}
} // namespace

Keybinds &Keybinds::instance()
{
    static Keybinds keybinds;
    return keybinds;
}

const QList<Keybinds::Action> &Keybinds::actions()
{
    // Discord's defaults where it has the action
    static const QList<Action> list = {
        { QStringLiteral("previousChannel"), tr("Previous channel"), tr("Open the channel above this one."),
          QKeySequence(Qt::ALT | Qt::Key_Up) },
        { QStringLiteral("nextChannel"), tr("Next channel"), tr("Open the channel below this one."),
          QKeySequence(Qt::ALT | Qt::Key_Down) },
        { QStringLiteral("previousUnread"), tr("Previous unread channel"),
          tr("Open the nearest unread channel above this one."), QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_Up) },
        { QStringLiteral("nextUnread"), tr("Next unread channel"), tr("Open the nearest unread channel below this one."),
          QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_Down) },
        { QStringLiteral("markChannelRead"), tr("Mark channel as read"), tr("Mark the open channel as read."),
          QKeySequence(Qt::SHIFT | Qt::Key_Escape) },
        { QStringLiteral("focusMessageBox"), tr("Focus the message box"), tr("Put the cursor in the message box."),
          QKeySequence(Qt::CTRL | Qt::Key_L) },
        { QStringLiteral("search"), tr("Search"), tr("Search the messages of this server or conversation."),
          QKeySequence(Qt::CTRL | Qt::Key_F) },
        { QStringLiteral("toggleMute"), tr("Toggle mute"), tr("Mute or unmute your microphone."),
          QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M) },
        { QStringLiteral("toggleDeafen"), tr("Toggle deafen"), tr("Deafen or undeafen yourself."),
          QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D) },
        { QStringLiteral("toggleStreamerMode"), tr("Toggle streamer mode"),
          tr("Hide personal information, invite links and notifications."), QKeySequence() },
        { QStringLiteral("toggleChannelList"), tr("Toggle channel list"), tr("Show or hide the channel list."),
          QKeySequence(Qt::CTRL | Qt::Key_B) },
        { QStringLiteral("toggleMemberList"), tr("Toggle member list"), tr("Show or hide the member list."),
          QKeySequence(Qt::CTRL | Qt::Key_U) },
        { QStringLiteral("openSettings"), tr("Open settings"), tr("Open this window."),
          QKeySequence(Qt::CTRL | Qt::Key_Comma) },
        { QStringLiteral("reconnect"), tr("Reconnect"), tr("Reconnect the account in view to Discord."),
          QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R) },
    };
    return list;
}

QKeySequence Keybinds::key(const QString &id)
{
    QSettings settings;
    if (settings.contains(settingsKey(id)))
        return QKeySequence(settings.value(settingsKey(id)).toString(), QKeySequence::PortableText);
    return defaultKey(id);
}

QKeySequence Keybinds::defaultKey(const QString &id)
{
    for (const Action &action : actions())
        if (action.id == id)
            return action.defaultKey;
    return {};
}

void Keybinds::setKey(const QString &id, const QKeySequence &key)
{
    if (Keybinds::key(id) == key)
        return;
    QSettings().setValue(settingsKey(id), key.toString(QKeySequence::PortableText));
    emit instance().changed(id);
}

void Keybinds::reset(const QString &id)
{
    const QKeySequence before = key(id);
    QSettings().remove(settingsKey(id));
    if (key(id) != before)
        emit instance().changed(id);
}

QString Keybinds::conflict(const QKeySequence &key, const QString &except)
{
    if (key.isEmpty())
        return {};
    for (const Action &action : actions())
        if (action.id != except && Keybinds::key(action.id) == key)
            return action.id;
    return {};
}

} // namespace Qord
