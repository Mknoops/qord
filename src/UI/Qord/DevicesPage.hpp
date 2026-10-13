#pragma once

#include <QPointer>
#include <QWidget>

#include "Plugins/Types.hpp"

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace Acheron {
namespace Core {
class ClientInstance;
} // namespace Core

namespace UI {

// Discord's Settings → Devices: the devices logged in to your account (GET
// /auth/sessions), this one first, with log out for each and for all others.
// Plugins can restyle a device and add buttons (Hook::DeviceSessions).
class DevicesPage : public QWidget
{
    Q_OBJECT
public:
    explicit DevicesPage(QWidget *parent = nullptr);

    void setInstance(Core::ClientInstance *instance);
    // Fetches the devices again
    void reload();
    // Shows the fetched devices again (a plugin's view of them changed)
    void rerender();

    // The page shown, if any (for plugins' refreshDevicesPage)
    static DevicesPage *visiblePage();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QWidget *row(const Qord::Plugins::DeviceSession &session);
    void logOut(const QStringList &idHashes);

    QPointer<Core::ClientInstance> instance;
    QList<Qord::Plugins::DeviceSession> sessions;
    bool loaded = false;
    QVBoxLayout *list;
    QLabel *status;
    QPushButton *logOutAll;
};

} // namespace UI
} // namespace Acheron
