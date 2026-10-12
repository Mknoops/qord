#include "CameraUi.hpp"

#include "Core/Audio/VoiceManager.hpp"
#include "Core/ClientInstance.hpp"
#include "Core/Session.hpp"
#include "Core/UserManager.hpp"
#include "UI/VoiceStatusBar.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QGridLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>

#include <cmath>
#include <thread>

namespace Acheron {
namespace UI {

using Qord::Video::Camera;
using Qord::Video::CameraDevice;

namespace {

const QString SelfKey = QStringLiteral("self");
const QString DeviceSetting = QStringLiteral("qord/camera/device");

QIcon cameraIcon(const QColor &color, bool off)
{
    QPixmap px(20, 20);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(2.5, 5.5, 10.5, 9), 2, 2);
    QPolygonF lens;
    lens << QPointF(13, 9) << QPointF(17.5, 6.5) << QPointF(17.5, 13.5) << QPointF(13, 11);
    p.drawPolygon(lens);
    if (off)
        p.drawLine(QPointF(3, 17), QPointF(17, 3));
    return QIcon(px);
}

QString userKey(Core::Snowflake userId)
{
    return QString::number(quint64(userId));
}

} // namespace

VideoTile::VideoTile(const QString &name, bool mirrored, QWidget *parent)
    : QWidget(parent), name(name), mirrored(mirrored)
{
    setMinimumSize(240, 135);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void VideoTile::showFrame(const QImage &newFrame)
{
    // Your own camera is shown mirrored, as Discord does; others see it as is
    frame = mirrored ? newFrame.flipped(Qt::Horizontal) : newFrame;
    update();
}

void VideoTile::setName(const QString &newName)
{
    name = newName;
    update();
}

void VideoTile::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF r = QRectF(rect()).adjusted(4, 4, -4, -4);
    QPainterPath clip;
    clip.addRoundedRect(r, 8, 8);
    p.setClipPath(clip);
    p.fillRect(r, QColor(0x1e, 0x1f, 0x22));
    if (!frame.isNull()) {
        const QSize size = frame.size().scaled(r.size().toSize(), Qt::KeepAspectRatio);
        const QRectF target(r.center().x() - size.width() / 2.0, r.center().y() - size.height() / 2.0,
                            size.width(), size.height());
        p.drawImage(target, frame);
    } else {
        p.setPen(QColor(0x94, 0x9b, 0xa4));
        p.drawText(r, Qt::AlignCenter, tr("Waiting for video…"));
    }
    // The name, bottom left, on a dark pill
    QFont font = p.font();
    font.setPixelSize(13);
    font.setBold(true);
    p.setFont(font);
    const QFontMetrics fm(font);
    const QString shown = fm.elidedText(name, Qt::ElideRight, int(r.width()) - 32);
    const QRectF pill(r.left() + 8, r.bottom() - 8 - (fm.height() + 6), fm.horizontalAdvance(shown) + 16,
                      fm.height() + 6);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 150));
    p.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
    p.setPen(Qt::white);
    p.drawText(pill, Qt::AlignCenter, shown);
}

VideoCallWindow::VideoCallWindow(QWidget *parent) : QWidget(parent, Qt::Window)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Video"));
    resize(960, 560);
    setMinimumSize(320, 200);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
    setAutoFillBackground(true);
    grid = new QGridLayout(this);
    grid->setContentsMargins(8, 8, 8, 8);
    grid->setSpacing(0);
}

VideoTile *VideoCallWindow::tile(const QString &key, const QString &name, bool mirrored)
{
    if (VideoTile *existing = tiles.value(key))
        return existing;
    auto *t = new VideoTile(name, mirrored, this);
    tiles.insert(key, t);
    relayout();
    return t;
}

void VideoCallWindow::removeTile(const QString &key)
{
    if (VideoTile *t = tiles.take(key)) {
        grid->removeWidget(t);
        t->deleteLater();
        relayout();
    }
}

void VideoCallWindow::relayout()
{
    // As square a grid as the tiles allow, your own camera last
    QList<VideoTile *> order;
    for (auto it = tiles.cbegin(); it != tiles.cend(); ++it)
        if (it.key() != SelfKey)
            order.append(it.value());
    if (VideoTile *self = tiles.value(SelfKey))
        order.append(self);
    for (VideoTile *t : std::as_const(order))
        grid->removeWidget(t);
    const int columns = qMax(1, int(std::ceil(std::sqrt(double(order.size())))));
    for (int i = 0; i < order.size(); ++i)
        grid->addWidget(order[i], i / columns, i % columns);
}

void VideoCallWindow::closeEvent(QCloseEvent *event)
{
    emit closing(tiles.keys());
    QWidget::closeEvent(event);
}

void VideoCallWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && isFullScreen())
        showNormal();
    else if (event->key() == Qt::Key_F11)
        mouseDoubleClickEvent(nullptr);
    else
        QWidget::keyPressEvent(event);
}

void VideoCallWindow::mouseDoubleClickEvent(QMouseEvent *)
{
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

CameraUi::CameraUi(Core::Session *session, VoiceStatusBar *bar, QWidget *window)
    : QObject(window), session(session), window(window)
{
    cameraButton = new QPushButton(bar);
    cameraButton->setFixedSize(24, 24);
    cameraButton->setIconSize(QSize(20, 20));
    cameraButton->setCursor(Qt::PointingHandCursor);
    cameraButton->setStyleSheet(QStringLiteral("QPushButton { background: transparent; border: none; }"
                                               "QPushButton:hover { background: rgba(255, 255, 255, 20); border-radius: 4px; }"));
    connect(cameraButton, &QPushButton::clicked, this, &CameraUi::toggleCamera);
    bar->insertActionButton(cameraButton);
    updateButton();

    // A camera service per account, before any call, so other people's
    // cameras show as soon as you join
    for (Core::ClientInstance *instance : session->getClients())
        track(instance);
    connect(session, &Core::Session::connectionStateChanged, this, [this](Core::Snowflake accountId) {
        track(this->session->client(accountId));
    });
}

CameraUi::~CameraUi()
{
    if (videos)
        videos->close();
}

void CameraUi::track(Core::ClientInstance *instance)
{
    if (instance)
        camera(instance);
}

Core::ClientInstance *CameraUi::voiceInstance() const
{
    for (Core::ClientInstance *instance : session->getClients())
        if (instance && instance->isInVoice())
            return instance;
    return nullptr;
}

Camera *CameraUi::camera(Core::ClientInstance *instance)
{
    if (!instance || !instance->voice())
        return nullptr;
    const quint64 id = quint64(instance->accountId());
    if (Camera *existing = cameras.value(id))
        return existing;
    // Owned by the voice manager: it goes when the account does
    auto *cam = new Camera(instance->discord(), instance->voice(), instance->accountId(), instance->voice());
    cameras.insert(id, cam);
    const QPointer<Core::ClientInstance> guard(instance);
    connect(cam, &Camera::cameraChanged, this, [this, guard](bool on) {
        updateButton();
        if (on && guard) {
            showVideo(guard);
        } else if (videos) {
            videos->removeTile(SelfKey);
            closeVideoWindowIfEmpty();
        }
    });
    connect(cam, &Camera::error, this, [this](const QString &message) {
        QMessageBox::warning(window, tr("Camera"), message);
        updateButton();
    });
    connect(cam, &Camera::previewFrame, this, [this](const QImage &frame) {
        if (videos)
            videos->tile(SelfKey, tr("You"), true)->showFrame(frame);
    });
    connect(cam, &Camera::remoteFrame, this, [this, guard](Core::Snowflake userId, const QImage &frame) {
        if (!guard || (!videos && dismissed.contains(userKey(userId))))
            return;
        const bool opened = !videos;
        VideoCallWindow *w = videoWindow(true);
        const QString name = guard->users()->getDisplayName(userId, guard->voice()->currentGuildId());
        w->tile(userKey(userId), name)->showFrame(frame);
        if (opened)
            w->show();
    });
    connect(cam, &Camera::remoteVideoEnded, this, [this](Core::Snowflake userId) {
        dismissed.remove(userKey(userId));
        if (videos) {
            videos->removeTile(userKey(userId));
            closeVideoWindowIfEmpty();
        }
    });
    return cam;
}

VideoCallWindow *CameraUi::videoWindow(bool create)
{
    if (!videos && create) {
        videos = new VideoCallWindow;
        connect(videos, &VideoCallWindow::closing, this, [this](const QStringList &keys) {
            for (const QString &key : keys)
                dismissed.insert(key);
        });
    }
    return videos;
}

void CameraUi::closeVideoWindowIfEmpty()
{
    if (videos && videos->isEmpty())
        videos->close();
}

void CameraUi::showVideo(Core::ClientInstance *instance)
{
    Camera *cam = camera(instance);
    if (!cam)
        return;
    dismissed.clear();
    VideoCallWindow *w = videoWindow(true);
    if (cam->isOn())
        w->tile(SelfKey, tr("You"), true);
    w->show();
    w->raise();
    w->activateWindow();
}

void CameraUi::updateButton()
{
    Core::ClientInstance *instance = voiceInstance();
    Camera *cam = instance ? cameras.value(quint64(instance->accountId())) : nullptr;
    const bool on = cam && cam->isOn();
    cameraButton->setIcon(cameraIcon(on ? QColor(0x23, 0xa5, 0x5a) : QColor(0xb5, 0xba, 0xc1), !on));
    cameraButton->setToolTip(on ? tr("Turn Off Camera") : tr("Turn On Camera"));
}

void CameraUi::toggleCamera()
{
    Core::ClientInstance *instance = voiceInstance();
    Camera *cam = camera(instance);
    if (!cam) {
        QMessageBox::information(window, tr("Camera"), tr("Join a voice channel to turn on your camera."));
        return;
    }
    if (cam->isOn()) {
        cam->stop();
        return;
    }
    if (listing)
        return;
    // Drivers can take a moment to answer: list the cameras off the UI thread
    listing = true;
    cameraButton->setEnabled(false);
    const QPointer<CameraUi> self(this);
    const QPointer<Core::ClientInstance> guard(instance);
    std::thread([self, guard]() {
        const QList<CameraDevice> found = Qord::Video::listCameras();
        QMetaObject::invokeMethod(qApp, [self, guard, found]() {
            if (!self)
                return;
            self->listing = false;
            self->cameraButton->setEnabled(true);
            if (!guard)
                return;
            if (found.isEmpty()) {
                QMessageBox::information(self->window, tr("Camera"), tr("No camera was found."));
                return;
            }
            const QString last = QSettings().value(DeviceSetting).toString();
            if (found.size() == 1) {
                self->startWith(guard, found.first());
                return;
            }
            QMenu menu;
            for (const CameraDevice &device : found) {
                QAction *action = menu.addAction(device.name);
                action->setCheckable(true);
                action->setChecked(device.id == last);
                action->setData(device.id);
            }
            QAction *chosen = menu.exec(self->cameraButton->mapToGlobal(QPoint(0, self->cameraButton->height())));
            if (!chosen || !self || !guard)
                return;
            for (const CameraDevice &device : found)
                if (device.id == chosen->data().toString())
                    self->startWith(guard, device);
        });
    }).detach();
}

void CameraUi::startWith(Core::ClientInstance *instance, const CameraDevice &device)
{
    Camera *cam = camera(instance);
    if (!cam)
        return;
    QSettings().setValue(DeviceSetting, device.id);
    cam->start(device);
}

bool CameraUi::hasCamera(quint64 accountId, quint64 userId) const
{
    Core::ClientInstance *instance = session->client(Core::Snowflake(accountId));
    if (!instance || !instance->voice())
        return false;
    if (Core::Snowflake(userId) == instance->accountId()) {
        const Camera *cam = cameras.value(accountId);
        return cam && cam->isOn();
    }
    const auto state = instance->voice()->voiceStateForUser(Core::Snowflake(userId));
    return state && state->selfVideo.hasValue() && state->selfVideo.get();
}

} // namespace UI
} // namespace Acheron
