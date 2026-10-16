#include "GoLiveUi.hpp"

#include "Core/Audio/VoiceManager.hpp"
#include "Core/ClientInstance.hpp"
#include "Core/Emoji/EmojiManager.hpp"
#include "Core/Session.hpp"
#include "Core/UserManager.hpp"
#include "Discord/Client.hpp"
#include "UI/VoiceStatusBar.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QSettings>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTabBar>
#include <QVBoxLayout>

#include <thread>

namespace Acheron {
namespace UI {

using Qord::Video::CaptureSource;
using Qord::Video::GoLive;

namespace {

constexpr QSize Thumbnail(192, 108);

QIcon screenIcon(const QColor &color)
{
    QPixmap px(20, 20);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 1.6));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(2.5, 3.5, 15, 10), 1.5, 1.5);
    p.drawLine(QPointF(7, 16.5), QPointF(13, 16.5));
    p.drawLine(QPointF(10, 13.5), QPointF(10, 16.5));
    // the arrow Discord draws inside
    p.drawLine(QPointF(10, 11), QPointF(10, 6.5));
    p.drawLine(QPointF(8, 8.5), QPointF(10, 6.5));
    p.drawLine(QPointF(12, 8.5), QPointF(10, 6.5));
    return QIcon(px);
}

QString resolutionLabel(int height)
{
    return height >= 2160 ? QStringLiteral("Source") : QStringLiteral("%1p").arg(height);
}

} // namespace

ScreenSharePicker::ScreenSharePicker(const QList<GoLive::Quality> &allowed, QWidget *parent)
    : QDialog(parent), allowed(allowed)
{
    setWindowTitle(tr("Screen Share"));
    setMinimumSize(680, 520);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *title = new QLabel(tr("Screen Share"), this);
    QFont titleFont = title->font();
    titleFont.setPixelSize(20);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    tabs = new QTabBar(this);
    tabs->addTab(tr("Applications"));
    tabs->addTab(tr("Screens"));
    tabs->setDrawBase(false);
    layout->addWidget(tabs);

    list = new QListWidget(this);
    list->setViewMode(QListView::IconMode);
    list->setIconSize(Thumbnail);
    list->setGridSize(Thumbnail + QSize(24, 44));
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setWordWrap(true);
    list->setUniformItemSizes(true);
    layout->addWidget(list, 1);

    status = new QLabel(tr("Looking for screens and windows…"), this);
    status->setAlignment(Qt::AlignCenter);
    layout->addWidget(status);

    auto *quality = new QHBoxLayout;
    quality->addWidget(new QLabel(tr("Resolution"), this));
    resolution = new QComboBox(this);
    quality->addWidget(resolution);
    quality->addSpacing(16);
    quality->addWidget(new QLabel(tr("Frame Rate"), this));
    frameRate = new QComboBox(this);
    quality->addWidget(frameRate);
    quality->addStretch();
    // Sound: on Windows only the shared program (or everything but Qord);
    // elsewhere everything the computer plays, this call included, so off
    // unless you turned it on before
    sound = new QCheckBox(tr("Share sound"), this);
    sound->setVisible(Qord::Audio::StreamAudio::isSupported());
    sound->setChecked(QSettings().value(QStringLiteral("qord/goLive/sound"),
                                        Qord::Audio::StreamAudio::excludesOwnAudio()).toBool());
    if (!Qord::Audio::StreamAudio::excludesOwnAudio())
        sound->setToolTip(tr("Shares everything this computer plays, including this call"));
    connect(sound, &QCheckBox::toggled, this,
            [](bool on) { QSettings().setValue(QStringLiteral("qord/goLive/sound"), on); });
    quality->addWidget(sound);
    layout->addLayout(quality);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    auto *cancel = new QPushButton(tr("Cancel"), this);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancel);
    goLive = new QPushButton(tr("Go Live"), this);
    goLive->setDefault(true);
    goLive->setEnabled(false);
    goLive->setStyleSheet(QStringLiteral("QPushButton { background: #5865F2; color: white; border: none; border-radius: 3px;"
                                         " padding: 6px 18px; font-weight: bold; }"
                                         "QPushButton:disabled { background: #4752c4; color: #b9bbbe; }"));
    connect(goLive, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(goLive);
    layout->addLayout(buttons);

    connect(tabs, &QTabBar::currentChanged, this, &ScreenSharePicker::showSources);
    connect(list, &QListWidget::currentRowChanged, this, [this](int row) { goLive->setEnabled(row >= 0); });
    connect(list, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
    connect(resolution, qOverload<int>(&QComboBox::currentIndexChanged), this, &ScreenSharePicker::updateQualityChoices);

    // Every resolution and frame rate Discord offers; the ones this account
    // can't use are shown but disabled, as Discord does
    for (int height : { 720, 1080, 1440, 2160 })
        resolution->addItem(resolutionLabel(height), height);
    for (int fps : { 15, 30, 60 })
        frameRate->addItem(QStringLiteral("%1 FPS").arg(fps), fps);
    resolution->setCurrentIndex(0);
    frameRate->setCurrentIndex(1);
    updateQualityChoices();

    // Window thumbnails ask each program to draw itself; a hung one must not
    // freeze the dialog
    const QPointer<ScreenSharePicker> guard(this);
    std::thread([guard]() {
        const QList<CaptureSource> found = Qord::Video::listCaptureSources(Thumbnail);
        QMetaObject::invokeMethod(qApp, [guard, found]() {
            if (guard)
                guard->sourcesLoaded(found);
        });
    }).detach();
}

void ScreenSharePicker::sourcesLoaded(const QList<CaptureSource> &list)
{
    sources = list;
    showSources();
}

void ScreenSharePicker::showSources()
{
    list->clear();
    const auto kind = tabs->currentIndex() == 0 ? CaptureSource::Kind::Window : CaptureSource::Kind::Screen;
    int shown = 0;
    for (int i = 0; i < sources.size(); ++i) {
        const CaptureSource &s = sources[i];
        if (s.kind != kind)
            continue;
        QPixmap thumb(Thumbnail);
        thumb.fill(QColor(0x20, 0x22, 0x25));
        if (!s.thumbnail.isNull()) {
            QPainter p(&thumb);
            const QImage scaled = s.thumbnail.scaled(Thumbnail, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            p.drawImage((Thumbnail.width() - scaled.width()) / 2, (Thumbnail.height() - scaled.height()) / 2, scaled);
        }
        auto *item = new QListWidgetItem(QIcon(thumb), s.name);
        item->setData(Qt::UserRole, i);
        item->setToolTip(s.processName.isEmpty() ? s.name : s.name + QStringLiteral("\n") + s.processName);
        list->addItem(item);
        ++shown;
    }
    status->setText(shown ? QString() : sources.isEmpty() ? tr("Looking for screens and windows…") : tr("Nothing to share here"));
    status->setVisible(!status->text().isEmpty());
    if (shown)
        list->setCurrentRow(0);
}

void ScreenSharePicker::updateQualityChoices()
{
    const int height = resolution->currentData().toInt();
    const auto allows = [this](int h, int fps) {
        return std::any_of(allowed.begin(), allowed.end(),
                           [&](const GoLive::Quality &q) { return q.height == h && (fps == 0 || q.fps == fps); });
    };
    const auto setEnabled = [](QComboBox *box, int index, bool enabled, const QString &label) {
        auto *model = qobject_cast<QStandardItemModel *>(box->model());
        if (auto *item = model ? model->item(index) : nullptr) {
            item->setEnabled(enabled);
            item->setText(enabled ? label : label + QStringLiteral(" (Nitro)"));
        }
    };
    for (int i = 0; i < resolution->count(); ++i) {
        const int h = resolution->itemData(i).toInt();
        setEnabled(resolution, i, allows(h, 0), resolutionLabel(h));
    }
    for (int i = 0; i < frameRate->count(); ++i) {
        const int fps = frameRate->itemData(i).toInt();
        setEnabled(frameRate, i, allows(height, fps), QStringLiteral("%1 FPS").arg(fps));
    }
    if (!allows(height, frameRate->currentData().toInt()))
        for (int i = frameRate->count() - 1; i >= 0; --i)
            if (allows(height, frameRate->itemData(i).toInt())) {
                frameRate->setCurrentIndex(i);
                break;
            }
}

CaptureSource ScreenSharePicker::source() const
{
    const QListWidgetItem *item = list->currentItem();
    return item ? sources.value(item->data(Qt::UserRole).toInt()) : CaptureSource();
}

bool ScreenSharePicker::shareSound() const
{
    return !sound->isHidden() && sound->isChecked();
}

GoLive::Quality ScreenSharePicker::quality() const
{
    GoLive::Quality q;
    q.height = resolution->currentData().toInt();
    q.width = q.height * 16 / 9;
    q.fps = frameRate->currentData().toInt();
    return q;
}

StreamViewer::StreamViewer(const QString &title, QWidget *parent) : QWidget(parent, Qt::Window)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(title);
    resize(960, 560);
    setMinimumSize(320, 180);
    message = tr("Waiting for the stream…");
}

void StreamViewer::showFrame(const QImage &newFrame)
{
    frame = newFrame;
    message.clear();
    update();
}

void StreamViewer::showEnded(const QString &reason)
{
    message = reason.isEmpty() ? tr("The stream has ended") : tr("The stream has ended (%1)").arg(reason);
    update();
}

void StreamViewer::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (!frame.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QSize size = frame.size().scaled(this->size(), Qt::KeepAspectRatio);
        p.drawImage(QRect(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size), frame);
    }
    if (!message.isEmpty()) {
        p.setPen(QColor(0xdb, 0xde, 0xe1));
        p.drawText(rect(), Qt::AlignCenter, message);
    }
}

void StreamViewer::mouseDoubleClickEvent(QMouseEvent *)
{
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

void StreamViewer::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && isFullScreen())
        showNormal();
    else if (event->key() == Qt::Key_F11)
        mouseDoubleClickEvent(nullptr);
    else
        QWidget::keyPressEvent(event);
}

void StreamViewer::closeEvent(QCloseEvent *event)
{
    emit closed();
    QWidget::closeEvent(event);
}

GoLiveUi::GoLiveUi(Core::Session *session, VoiceStatusBar *bar, QWidget *window)
    : QObject(window), session(session), window(window)
{
    shareButton = new QPushButton(bar);
    shareButton->setFixedSize(24, 24);
    shareButton->setIconSize(QSize(20, 20));
    shareButton->setCursor(Qt::PointingHandCursor);
    shareButton->setStyleSheet(QStringLiteral("QPushButton { background: transparent; border: none; }"
                                              "QPushButton:hover { background: rgba(255, 255, 255, 20); border-radius: 4px; }"));
    connect(shareButton, &QPushButton::clicked, this, &GoLiveUi::toggleSharing);
    bar->insertActionButton(shareButton);
    updateButton();
}

GoLiveUi::~GoLiveUi()
{
    for (const auto &viewer : std::as_const(viewers))
        if (viewer)
            viewer->disconnect(this);
    qDeleteAll(lives);
}

Core::ClientInstance *GoLiveUi::voiceInstance() const
{
    for (Core::ClientInstance *instance : session->getClients())
        if (instance && instance->isInVoice())
            return instance;
    return nullptr;
}

GoLive *GoLiveUi::goLive(Core::ClientInstance *instance)
{
    if (!instance || !instance->voice())
        return nullptr;
    const quint64 id = quint64(instance->accountId());
    if (GoLive *existing = lives.value(id))
        return existing;
    auto *live = new GoLive(instance->discord(), instance->voice(), instance->accountId());
    lives.insert(id, live);
    connect(live, &GoLive::streamingChanged, this, &GoLiveUi::updateButton);
    connect(live, &GoLive::streamError, this, [this](const QString &message) {
        QMessageBox::warning(window, tr("Go Live"), message);
        updateButton();
    });
    connect(live, &GoLive::watchFrame, this, [this](const QString &key, const QImage &frame) {
        if (auto viewer = viewers.value(key))
            viewer->showFrame(frame);
    });
    connect(live, &GoLive::watchEnded, this, [this](const QString &key, const QString &reason) {
        if (auto viewer = viewers.take(key)) {
            viewer->disconnect(this);
            viewer->showEnded(reason);
        }
    });
    return live;
}

void GoLiveUi::updateButton()
{
    Core::ClientInstance *instance = voiceInstance();
    GoLive *live = instance ? lives.value(quint64(instance->accountId())) : nullptr;
    const bool streaming = live && live->isStreaming();
    shareButton->setIcon(screenIcon(streaming ? QColor(0xED, 0x42, 0x45) : QColor(0xb5, 0xba, 0xc1)));
    shareButton->setToolTip(streaming ? tr("Stop Streaming") : tr("Share Your Screen"));
}

void GoLiveUi::toggleSharing()
{
    Core::ClientInstance *instance = voiceInstance();
    GoLive *live = goLive(instance);
    if (!live) {
        QMessageBox::information(window, tr("Go Live"), tr("Join a voice channel to share your screen."));
        return;
    }
    if (live->isStreaming()) {
        live->stopStreaming();
        updateButton();
        return;
    }
    const auto me = instance->discord()->getMe();
    // FakeNitro's stream quality bypass offers what Nitro does
    const bool nitro = (me.premiumType.hasValue() && me.premiumType.get() == Discord::PremiumType::TIER_2) ||
                       Core::expressionBypass().streamQuality;
    int boostTier = 0;
    if (instance->voice()->currentGuildId().isValid())
        boostTier = int(instance->discord()->getGuildPremiumTier(instance->voice()->currentGuildId()));
    ScreenSharePicker picker(GoLive::allowedQualities(nitro, boostTier), window);
    if (picker.exec() != QDialog::Accepted)
        return;
    const CaptureSource source = picker.source();
    if (source.name.isEmpty())
        return;
    live->startStreaming(source, picker.quality(), picker.shareSound());
    updateButton();
}

bool GoLiveUi::isLive(quint64 accountId, quint64 userId) const
{
    Core::ClientInstance *instance = session->client(Core::Snowflake(accountId));
    if (!instance || !instance->voice())
        return false;
    // Our own voice state is kept locally, without the stream flag
    if (Core::Snowflake(userId) == instance->accountId()) {
        const GoLive *live = lives.value(accountId);
        return live && live->isStreaming();
    }
    const auto state = instance->voice()->voiceStateForUser(Core::Snowflake(userId));
    return state && state->selfStream.hasValue() && state->selfStream.get();
}

void GoLiveUi::watch(Core::ClientInstance *instance, Core::Snowflake userId)
{
    GoLive *live = goLive(instance);
    if (!live || !instance->voice())
        return;
    const QString key = GoLive::streamKey(instance->voice()->currentGuildId(), instance->voice()->currentChannelId(),
                                          userId);
    if (auto existing = viewers.value(key)) {
        existing->raise();
        existing->activateWindow();
        return;
    }
    const QString name = instance->users()->getDisplayName(userId, instance->voice()->currentGuildId());
    auto *viewer = new StreamViewer(tr("%1's screen").arg(name));
    viewers.insert(key, viewer);
    connect(viewer, &StreamViewer::closed, this, [this, live, key]() {
        viewers.remove(key);
        live->stopWatching(key);
    });
    viewer->show();
    live->watch(userId);
}

void GoLiveUi::addUserMenuItems(QMenu *menu, Core::ClientInstance *instance, Core::Snowflake userId)
{
    if (!instance || !isLive(quint64(instance->accountId()), quint64(userId)))
        return;
    const QPointer<Core::ClientInstance> guard(instance);
    if (userId == instance->accountId()) {
        GoLive *live = lives.value(quint64(instance->accountId()));
        if (live && live->isStreaming())
            connect(menu->addAction(tr("Stop Streaming")), &QAction::triggered, this, [this, live]() {
                live->stopStreaming();
                updateButton();
            });
        return;
    }
    // Watching needs you in the same call
    if (!instance->voice() || !instance->voice()->isConnected())
        return;
    connect(menu->addAction(tr("Watch Stream")), &QAction::triggered, this, [this, guard, userId]() {
        if (guard)
            watch(guard, userId);
    });
    menu->addSeparator();
}

} // namespace UI
} // namespace Acheron
