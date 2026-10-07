#pragma once

#include <QDialog>
#include <QPointer>

class QLabel;
class QLineEdit;
class QStackedWidget;

namespace Acheron {
namespace Core {
class ClientInstance;
}

namespace UI {

// Discord's "Add a Server": create your own server or join one with an
// invite link. The new server arrives through the gateway like on Discord.
class AddServerDialog : public QDialog
{
    Q_OBJECT
public:
    AddServerDialog(Core::ClientInstance *instance, QWidget *parent = nullptr);

    // Accepts https://discord.gg/x, discord.com/invite/x, x
    static QString inviteCode(const QString &input);

private:
    void createServer();
    void joinServer();

    QPointer<Core::ClientInstance> instance;
    QStackedWidget *pages;
    QLineEdit *nameField;
    QLineEdit *inviteField;
    QLabel *createStatus;
    QLabel *joinStatus;
};

} // namespace UI
} // namespace Acheron
