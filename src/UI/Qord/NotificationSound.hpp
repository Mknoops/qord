#pragma once

#include <QObject>

namespace Acheron {
namespace UI {

// The sound for a new DM or mention, like Discord's message sound. The chime
// is synthesised (two short notes); Discord's own sound files are not used.
// Plays through miniaudio's default output device when the client has it,
// otherwise the system beep.
class NotificationSound : public QObject
{
    Q_OBJECT
public:
    explicit NotificationSound(QObject *parent = nullptr);
    ~NotificationSound() override;

    static bool enabled(); // setting notifications/sound, on by default
    static void setEnabled(bool enabled);

    void play();

private:
    struct Device;
    Device *device = nullptr;
};

} // namespace UI
} // namespace Acheron
