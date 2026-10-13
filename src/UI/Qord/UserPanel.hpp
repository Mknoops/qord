#pragma once

#include <QPointer>
#include <QWidget>

#include "Core/Snowflake.hpp"

class QHBoxLayout;
class QLabel;
class QToolButton;

namespace Acheron {
namespace Core {
class ClientInstance;
class ImageManager;
} // namespace Core

namespace UI {

class StatusDot;

// Discord's bottom-left panel: your avatar with status, name and status
// line, plus mute, deafen and settings buttons. Clicking the name opens the
// status menu (online/idle/dnd/invisible, custom status) and account actions.
class UserPanel : public QWidget
{
    Q_OBJECT
public:
    explicit UserPanel(Core::ImageManager *images, QWidget *parent = nullptr);

    void setInstance(Core::ClientInstance *instance);

    [[nodiscard]] bool selfMute() const { return muted || deafened; }
    [[nodiscard]] bool selfDeaf() const { return deafened; }
    // Re-applies mute/deafen to the active voice connection (after joining)
    void applyVoiceFlags();
    void applyLocalVoiceFlags();

signals:
    void settingsRequested();
    void accountsRequested();
    void menuBarToggleRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    void refresh();
    void refreshAvatar();
    void refreshButtons();
    // Plugin buttons before mute (Hook::UserPanelButtons, GameActivityToggle)
    void rebuildPluginButtons();
    void showStatusMenu();
    void setStatus(const QString &status);
    void editCustomStatus();
    QString currentStatusText() const;

    Core::ImageManager *images;
    QPointer<Core::ClientInstance> instance;
    QList<QMetaObject::Connection> instanceConnections;

    QWidget *identity;
    QLabel *avatarLabel;
    StatusDot *statusDot;
    QLabel *nameLabel;
    QLabel *subtitleLabel;
    QToolButton *muteButton;
    QToolButton *deafenButton;
    QToolButton *settingsButton;
    QHBoxLayout *panelLayout;
    QList<QToolButton *> pluginButtons;

    bool muted = false;
    bool deafened = false;
    QString customText;
    QString customEmoji;
    QString chosenStatus; // empty until the user picks one this session
    QUrl avatarUrl;
};

} // namespace UI
} // namespace Acheron
