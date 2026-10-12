#pragma once

// Camera video in the window: the camera button in the voice panel (with a
// device menu when there is more than one camera), the camera icon next to
// participants who have theirs on, and the call's video window, a grid of
// everyone's camera plus your own self view.

#include <QHash>
#include <QImage>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QWidget>

#include "Core/Snowflake.hpp"
#include "Qord/Video/Camera.hpp"

class QGridLayout;
class QPushButton;

namespace Acheron {
namespace Core {
class ClientInstance;
class Session;
} // namespace Core
namespace UI {

class VoiceStatusBar;

class VideoTile : public QWidget
{
    Q_OBJECT
public:
    VideoTile(const QString &name, bool mirrored, QWidget *parent = nullptr);
    void showFrame(const QImage &frame);
    void setName(const QString &name);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage frame;
    QString name;
    bool mirrored;
};

class VideoCallWindow : public QWidget
{
    Q_OBJECT
public:
    explicit VideoCallWindow(QWidget *parent = nullptr);
    // The tile for `key` (made on first use); "self" is your own camera
    VideoTile *tile(const QString &key, const QString &name, bool mirrored = false);
    void removeTile(const QString &key);
    [[nodiscard]] bool isEmpty() const { return tiles.isEmpty(); }
    [[nodiscard]] QStringList keys() const { return tiles.keys(); }

signals:
    void closing(const QStringList &keys);

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void relayout();

    QGridLayout *grid;
    QMap<QString, VideoTile *> tiles;
};

class CameraUi : public QObject
{
    Q_OBJECT
public:
    CameraUi(Core::Session *session, VoiceStatusBar *bar, QWidget *window);
    ~CameraUi() override;

    [[nodiscard]] bool hasCamera(quint64 accountId, quint64 userId) const;
    void showVideo(Core::ClientInstance *instance);

private:
    Qord::Video::Camera *camera(Core::ClientInstance *instance);
    Core::ClientInstance *voiceInstance() const;
    void track(Core::ClientInstance *instance);
    void toggleCamera();
    void startWith(Core::ClientInstance *instance, const Qord::Video::CameraDevice &device);
    void updateButton();
    VideoCallWindow *videoWindow(bool create);
    void closeVideoWindowIfEmpty();

    Core::Session *session;
    QWidget *window;
    QPushButton *cameraButton = nullptr;
    QHash<quint64, QPointer<Qord::Video::Camera>> cameras;
    QPointer<VideoCallWindow> videos;
    // Videos whose window you closed: they don't reopen it
    QSet<QString> dismissed;
    bool listing = false;
};

} // namespace UI
} // namespace Acheron
