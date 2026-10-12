#pragma once

// Go Live in the window: the "Share Your Screen" button in the voice panel,
// Discord's screen share picker (applications or screens, resolution and
// frame rate within what the account may use), LIVE badges with "Watch
// Stream", and a viewer window per watched stream.

#include <QDialog>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QWidget>

#include "Core/Snowflake.hpp"
#include "Qord/Video/GoLive.hpp"

class QComboBox;
class QLabel;
class QListWidget;
class QMenu;
class QPushButton;
class QTabBar;

namespace Acheron {
namespace Core {
class ClientInstance;
class Session;
} // namespace Core
namespace UI {

class VoiceStatusBar;

class ScreenSharePicker : public QDialog
{
    Q_OBJECT
public:
    ScreenSharePicker(const QList<Qord::Video::GoLive::Quality> &allowed, QWidget *parent = nullptr);

    [[nodiscard]] Qord::Video::CaptureSource source() const;
    [[nodiscard]] Qord::Video::GoLive::Quality quality() const;

private:
    void showSources();
    void sourcesLoaded(const QList<Qord::Video::CaptureSource> &list);
    void updateQualityChoices();

    QList<Qord::Video::GoLive::Quality> allowed;
    QList<Qord::Video::CaptureSource> sources;
    QTabBar *tabs;
    QListWidget *list;
    QLabel *status;
    QComboBox *resolution;
    QComboBox *frameRate;
    QPushButton *goLive;
};

class StreamViewer : public QWidget
{
    Q_OBJECT
public:
    explicit StreamViewer(const QString &title, QWidget *parent = nullptr);
    void showFrame(const QImage &frame);
    void showEnded(const QString &reason);

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    QImage frame;
    QString message;
};

class GoLiveUi : public QObject
{
    Q_OBJECT
public:
    GoLiveUi(Core::Session *session, VoiceStatusBar *bar, QWidget *window);
    ~GoLiveUi() override;

    void addUserMenuItems(QMenu *menu, Core::ClientInstance *instance, Core::Snowflake userId);
    [[nodiscard]] bool isLive(quint64 accountId, quint64 userId) const;
    void watch(Core::ClientInstance *instance, Core::Snowflake userId);

private:
    Qord::Video::GoLive *goLive(Core::ClientInstance *instance);
    Core::ClientInstance *voiceInstance() const;
    void toggleSharing();
    void updateButton();

    Core::Session *session;
    QWidget *window;
    QPushButton *shareButton = nullptr;
    QHash<quint64, Qord::Video::GoLive *> lives;
    QHash<QString, QPointer<StreamViewer>> viewers;
};

} // namespace UI
} // namespace Acheron
