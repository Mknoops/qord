#include "AttachmentPreviewPanel.hpp"

#include <QBuffer>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QTemporaryFile>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include <memory>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMimeDatabase>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "Core/Emoji/EmojiManager.hpp"
#ifdef QORD_HAVE_GO_LIVE
#include "Qord/Video/VideoShrink.hpp"
#endif
#include "Core/Theme/Icons.hpp"
#include "Core/Theme/Manager.hpp"
#include "UI/Chat/ChatLayout.hpp"
#include "UI/Dialogs/BasePopup.hpp"

namespace Acheron {
namespace UI {

namespace {

class AttachmentEditPopup : public BasePopup
{
public:
    AttachmentEditPopup(Core::PendingAttachment &attachment, QWidget *parent)
        : BasePopup(parent)
    {
        auto *layout = new QVBoxLayout(getContainer());
        layout->setSpacing(12);
        layout->setContentsMargins(24, 24, 24, 24);

        auto *titleLabel = new QLabel(tr("Edit Attachment"), getContainer());
        QFont titleFont = titleLabel->font();
        titleFont.setBold(true);
        titleFont.setPointSize(titleFont.pointSize() + 2);
        titleLabel->setFont(titleFont);
        layout->addWidget(titleLabel);

        layout->addWidget(new QLabel(tr("File Name"), getContainer()));
        auto *filenameEdit = new QLineEdit(attachment.filename, getContainer());
        filenameEdit->setMaxLength(1024);
        filenameEdit->setMinimumWidth(320);
        layout->addWidget(filenameEdit);

        layout->addWidget(new QLabel(tr("Description (Alt Text)"), getContainer()));
        auto *descriptionEdit = new QLineEdit(attachment.description, getContainer());
        descriptionEdit->setMaxLength(1024);
        descriptionEdit->setPlaceholderText(tr("Add a description"));
        layout->addWidget(descriptionEdit);

        auto *spoilerCheck = new QCheckBox(tr("Mark as spoiler"), getContainer());
        spoilerCheck->setChecked(attachment.isSpoiler);
        layout->addWidget(spoilerCheck);

        auto *btnBox = new QDialogButtonBox(getContainer());
        QPushButton *saveButton = btnBox->addButton(tr("Save"), QDialogButtonBox::AcceptRole);
        btnBox->addButton(QDialogButtonBox::Cancel);
        saveButton->setDefault(true);
        layout->addWidget(btnBox);

        connect(btnBox, &QDialogButtonBox::accepted, this,
                [this, &attachment, filenameEdit, descriptionEdit, spoilerCheck]() {
                    QString filename = filenameEdit->text().trimmed();
                    if (!filename.isEmpty())
                        attachment.filename = filename;
                    attachment.description = descriptionEdit->text();
                    attachment.isSpoiler = spoilerCheck->isChecked();
                    accept();
                });
        connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }
};

} // namespace

AttachmentPreviewPanel::AttachmentPreviewPanel(QWidget *parent) : QWidget(parent)
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 4, 0, 2);
    outerLayout->setSpacing(2);

    errorLabel = new QLabel(this);
    errorLabel->setVisible(false);
    QColor errorColor = Core::Theme::Manager::instance().color(Core::Theme::Token::ChatError);
    errorLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(errorColor.name()));
    outerLayout->addWidget(errorLabel);

    progressRow = new QWidget(this);
    progressRow->setVisible(false);
    auto *progressLayout = new QHBoxLayout(progressRow);
    progressLayout->setContentsMargins(0, 0, 0, 0);
    progressLayout->setSpacing(8);
    progressBar = new QProgressBar(progressRow);
    progressBar->setRange(0, 1000);
    progressBar->setFormat(QStringLiteral("%p%"));
    progressBar->setFixedHeight(14);
    progressLayout->addWidget(progressBar, 1);
    auto *stopButton = new QToolButton(progressRow);
    stopButton->setText(tr("Cancel"));
    stopButton->setCursor(Qt::PointingHandCursor);
    connect(stopButton, &QToolButton::clicked, this, [this]() {
        if (stopShrinking)
            *stopShrinking = true;
    });
    progressLayout->addWidget(stopButton);
    outerLayout->addWidget(progressRow);

    scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setFixedHeight(132);

    cardsContainer = new QWidget(scrollArea);
    cardsLayout = new QHBoxLayout(cardsContainer);
    cardsLayout->setContentsMargins(4, 4, 4, 4);
    cardsLayout->setSpacing(8);
    cardsLayout->addStretch();
    scrollArea->setWidget(cardsContainer);
    outerLayout->addWidget(scrollArea);

    errorTimer = new QTimer(this);
    errorTimer->setSingleShot(true);
    errorTimer->setInterval(4000);
    connect(errorTimer, &QTimer::timeout, this, [this]() {
        errorLabel->setVisible(false);
        updateVisibility();
        emit attachmentsChanged();
    });

    setVisible(false);
}

void AttachmentPreviewPanel::setMaxFileSize(qint64 bytes)
{
    maxFileSize = bytes;
}

void AttachmentPreviewPanel::addFiles(const QList<QUrl> &urls)
{
    for (const QUrl &url : urls) {
        if (!url.isLocalFile())
            continue;

        QFileInfo info(url.toLocalFile());
        if (!info.isFile() || !info.isReadable()) {
            showError(tr("Can't read %1").arg(info.fileName()));
            continue;
        }

        Core::PendingAttachment attachment;
        attachment.filePath = info.absoluteFilePath();
        attachment.filename = info.fileName();
        attachment.size = info.size();
        attachment.mimeType = QMimeDatabase().mimeTypeForFile(info.absoluteFilePath()).name();
        if (!appendAttachment(attachment))
            break; // no more
    }
}

void AttachmentPreviewPanel::addImage(const QImage &image)
{
    if (image.isNull())
        return;

    Core::PendingAttachment attachment;
    QBuffer buffer(&attachment.data);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG")) {
        showError(tr("Couldn't process the pasted image"));
        return;
    }
    attachment.image = image;
    attachment.filename = QStringLiteral("image.png");
    attachment.mimeType = QStringLiteral("image/png");
    attachment.size = attachment.data.size();
    appendAttachment(attachment);
}

void AttachmentPreviewPanel::addData(const QString &fileName, const QByteArray &data, const QString &mimeType)
{
    if (data.isEmpty())
        return;
    Core::PendingAttachment attachment;
    attachment.data = data;
    if (mimeType.startsWith(QLatin1String("image/")))
        attachment.image = QImage::fromData(data);
    attachment.filename = fileName;
    attachment.mimeType = mimeType;
    attachment.size = data.size();
    appendAttachment(attachment);
}

bool AttachmentPreviewPanel::appendAttachment(Core::PendingAttachment attachment)
{
    if (items.size() >= MaxFiles) {
        showError(tr("You can attach up to %1 files per message").arg(MaxFiles));
        return false;
    }
    if (attachment.size > maxFileSize) {
        if (startShrinking(attachment))
            return true; // added once it fits
        showError(tr("%1 exceeds the %2 upload limit")
                          .arg(attachment.filename, ChatLayout::formatFileSize(maxFileSize)));
        return false;
    }

    items.append(std::move(attachment));
    rebuildCards();
    emit attachmentsChanged();
    return true;
}

// Qord: FakeNitro's handler makes an image too large to upload fit. It
// runs on a worker thread (an animated GIF can take a while); the image is
// added when it's done, or refused as before if it can't be made to fit.
bool AttachmentPreviewPanel::startShrinking(const Core::PendingAttachment &attachment)
{
#ifdef QORD_HAVE_GO_LIVE
    if (attachment.mimeType.startsWith(QLatin1String("video/")) && Core::expressionBypass().shrinkVideos)
        return startShrinkingVideo(attachment);
#endif
    const auto shrink = Core::expressionBypass().shrinkImage;
    // Larger files are not read into memory
    constexpr qint64 MaxInput = 200 * 1024 * 1024;
    if (!shrink || !attachment.mimeType.startsWith(QLatin1String("image/")) || attachment.size > MaxInput)
        return false;
    QByteArray data = attachment.data;
    if (data.isEmpty() && !attachment.filePath.isEmpty()) {
        QFile file(attachment.filePath);
        if (!file.open(QIODevice::ReadOnly))
            return false;
        data = file.readAll();
    }
    const QString fileName = attachment.filename;
    const QString mimeType = attachment.mimeType;
    const qint64 limit = maxFileSize;
    const bool spoiler = attachment.isSpoiler;
    showNotice(tr("Making %1 smaller to fit the %2 upload limit...")
                       .arg(fileName, ChatLayout::formatFileSize(limit)));
    auto *watcher = new QFutureWatcher<std::optional<Core::ShrunkImage>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, fileName, limit, spoiler]() {
        watcher->deleteLater();
        const std::optional<Core::ShrunkImage> out = watcher->result();
        if (!out || out->data.isEmpty() || out->data.size() > limit) {
            showError(tr("%1 exceeds the %2 upload limit").arg(fileName, ChatLayout::formatFileSize(limit)));
            return;
        }
        errorLabel->setVisible(false);
        Core::PendingAttachment shrunk;
        shrunk.data = out->data;
        shrunk.image = QImage::fromData(out->data);
        shrunk.filename = out->fileName;
        shrunk.mimeType = out->mimeType;
        shrunk.size = out->data.size();
        shrunk.isSpoiler = spoiler;
        appendAttachment(shrunk);
    });
    watcher->setFuture(QtConcurrent::run([shrink, fileName, mimeType, data, limit]() {
        return shrink(fileName, mimeType, data, limit);
    }));
    return true;
}

#ifdef QORD_HAVE_GO_LIVE
// Qord: FakeNitro's "Shrink large videos": re-encoded on a worker thread
// (it takes about as long as the video plays, often less)
bool AttachmentPreviewPanel::startShrinkingVideo(const Core::PendingAttachment &attachment)
{
    QString path = attachment.filePath;
    std::shared_ptr<QTemporaryFile> pasted;
    if (path.isEmpty()) {
        // Pasted or plugin-added data goes through a file, as ffmpeg reads files
        pasted = std::make_shared<QTemporaryFile>(QDir::tempPath() + QStringLiteral("/qord-video-XXXXXX"));
        if (attachment.data.isEmpty() || !pasted->open() || pasted->write(attachment.data) != attachment.data.size())
            return false;
        pasted->close();
        path = pasted->fileName();
    }
    const QString fileName = attachment.filename;
    const qint64 limit = maxFileSize;
    const bool spoiler = attachment.isSpoiler;
    showNotice(tr("Making %1 smaller to fit the %2 upload limit...").arg(fileName, ChatLayout::formatFileSize(limit)));

    const Core::ExpressionBypass &bypass = Core::expressionBypass();
    Qord::Video::ShrinkOptions options;
    options.priority = Qord::Video::Priority(qBound(0, bypass.videoPriority, 2));
    options.maxSeconds = bypass.maxVideoSeconds;
    options.trimToFit = bypass.trimVideosToFit;
    // The worker writes how far along it is; the bar reads it here
    auto stop = std::make_shared<std::atomic<bool>>(false);
    auto done = std::make_shared<std::atomic<int>>(0);
    options.progress = [stop, done](double fraction) {
        *done = int(fraction * 1000);
        return !*stop;
    };
    if (stopShrinking)
        *stopShrinking = true; // one at a time
    stopShrinking = stop;
    progressBar->setValue(0);
    progressRow->setVisible(true);
    updateVisibility();
    emit attachmentsChanged();

    struct Result
    {
        std::optional<Qord::Video::ShrinkResult> result;
        QString error;
    };
    auto *watcher = new QFutureWatcher<Result>(this);
    auto *poll = new QTimer(watcher);
    poll->setInterval(200);
    connect(poll, &QTimer::timeout, this, [this, done]() { progressBar->setValue(*done); });
    poll->start();
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, fileName, limit, spoiler, stop]() {
        watcher->deleteLater();
        if (stopShrinking == stop) {
            stopShrinking.reset();
            progressRow->setVisible(false);
        }
        const Result outcome = watcher->result();
        if (*stop) {
            errorLabel->setVisible(false);
            updateVisibility();
            emit attachmentsChanged();
            return;
        }
        if (!outcome.result) {
            showError(tr("%1 exceeds the %2 upload limit (%3)")
                              .arg(fileName, ChatLayout::formatFileSize(limit), outcome.error));
            return;
        }
        errorLabel->setVisible(false);
        Core::PendingAttachment shrunk;
        shrunk.data = outcome.result->data;
        const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));
        shrunk.filename = (dot > 0 ? fileName.left(dot) : fileName) + QStringLiteral(".mp4");
        shrunk.mimeType = QStringLiteral("video/mp4");
        shrunk.size = shrunk.data.size();
        shrunk.isSpoiler = spoiler;
        appendAttachment(shrunk);
        if (outcome.result->trimmed) {
            const int total = int(outcome.result->seconds);
            showNotice(tr("%1 was cut to its first %2:%3 to fit")
                               .arg(fileName)
                               .arg(total / 60)
                               .arg(total % 60, 2, 10, QLatin1Char('0'))); // stays while it's attached
        }
    });
    watcher->setFuture(QtConcurrent::run([path, limit, pasted, options]() {
        Result outcome;
        outcome.result = Qord::Video::shrinkVideo(path, limit, options, &outcome.error);
        return outcome;
    }));
    return true;
}
#endif

void AttachmentPreviewPanel::clearAttachments()
{
    if (items.isEmpty() && errorLabel->isHidden())
        return;

    items.clear();
    errorLabel->setVisible(false);
    rebuildCards();
    emit attachmentsChanged();
}

void AttachmentPreviewPanel::rebuildCards()
{
    while (cardsLayout->count() > 1) { // keep the stretch at the end
        QLayoutItem *item = cardsLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    for (int i = 0; i < items.size(); i++)
        cardsLayout->insertWidget(i, createCard(i));

    updateVisibility();
}

QWidget *AttachmentPreviewPanel::createCard(int index)
{
    const auto &item = items[index];
    auto &theme = Core::Theme::Manager::instance();

    auto *card = new QFrame(cardsContainer);
    card->setFixedSize(124, 120);
    card->setStyleSheet(QStringLiteral("QFrame { background-color: %1; border-radius: 4px; }")
                                .arg(theme.color(Core::Theme::Token::AlternateBaseBg).name()));

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(6, 2, 6, 4);
    layout->setSpacing(2);

    auto *thumbLabel = new QLabel(card);
    thumbLabel->setFixedHeight(64);
    thumbLabel->setAlignment(Qt::AlignCenter);
    thumbLabel->setPixmap(thumbnailFor(item));

    auto makeButton = [card](const QString &iconName, const QString &tooltip) {
        auto *button = new QToolButton(card);
        button->setIcon(Core::Theme::Icons::icon(iconName, Core::Theme::Token::PlaceholderText));
        button->setIconSize(QSize(15, 15));
        button->setToolTip(tooltip);
        button->setFixedSize(18, 18);
        button->setAutoRaise(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet("QToolButton { border: none; }");
        return button;
    };

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(2);
    buttonRow->addStretch();

    auto *spoilerButton = makeButton(Core::Theme::Icons::Name::Eye, tr("Spoiler"));
    spoilerButton->setCheckable(true);
    spoilerButton->setChecked(item.isSpoiler);
    connect(spoilerButton, &QToolButton::toggled, this, [this, index, thumbLabel](bool checked) {
        if (index >= items.size())
            return;
        items[index].isSpoiler = checked;
        thumbLabel->setPixmap(thumbnailFor(items[index]));
    });
    buttonRow->addWidget(spoilerButton);

    auto *editButton = makeButton(Core::Theme::Icons::Name::Pencil, tr("Edit"));
    connect(editButton, &QToolButton::clicked, this, [this, index]() {
        editAttachment(index);
    });
    buttonRow->addWidget(editButton);

    auto *removeButton = makeButton(Core::Theme::Icons::Name::X, tr("Remove"));
    connect(removeButton, &QToolButton::clicked, this, [this, index]() {
        if (index >= items.size())
            return;
        items.removeAt(index);
        rebuildCards();
        emit attachmentsChanged();
    });
    buttonRow->addWidget(removeButton);

    layout->addLayout(buttonRow);
    layout->addWidget(thumbLabel);

    QFont nameFont = font();
    nameFont.setPointSize(nameFont.pointSize() - 1);
    QFontMetrics nameFm(nameFont);

    auto *nameLabel = new QLabel(nameFm.elidedText(item.filename, Qt::ElideMiddle, 108), card);
    nameLabel->setFont(nameFont);
    nameLabel->setToolTip(item.filename);
    layout->addWidget(nameLabel);

    auto *sizeLabel = new QLabel(item.size > 0 ? ChatLayout::formatFileSize(item.size)
                                               : tr("Clipboard image"),
                                 card);
    sizeLabel->setFont(nameFont);
    sizeLabel->setStyleSheet(
            QStringLiteral("color: %1;")
                    .arg(theme.color(Core::Theme::Token::PlaceholderText).name()));
    layout->addWidget(sizeLabel);

    return card;
}

QPixmap AttachmentPreviewPanel::thumbnailFor(const Core::PendingAttachment &item) const
{
    constexpr QSize thumbSize(108, 64);

    QPixmap thumb;
    bool isImageThumb = false;
    if (!item.image.isNull()) {
        thumb = QPixmap::fromImage(
                item.image.scaled(thumbSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        isImageThumb = !thumb.isNull();
    } else if (item.mimeType.startsWith("image/")) {
        QImageReader reader(item.filePath);
        reader.setAutoTransform(true);
        QSize original = reader.size();
        if (original.isValid())
            reader.setScaledSize(original.scaled(thumbSize, Qt::KeepAspectRatio));
        thumb = QPixmap::fromImage(reader.read());
        isImageThumb = !thumb.isNull();
    }

    if (thumb.isNull()) {
        QFileIconProvider iconProvider;
        thumb = iconProvider.icon(QFileInfo(item.filePath)).pixmap(48, 48);
    }

    if (item.isSpoiler && isImageThumb)
        thumb = ChatLayout::createBlurredPixmap(thumb, 60);

    qreal dpr = devicePixelRatioF();
    QPixmap canvas(thumbSize * dpr);
    canvas.setDevicePixelRatio(dpr);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    QSizeF logicalSize = QSizeF(thumb.size()) / thumb.devicePixelRatio();
    painter.drawPixmap(QPointF((thumbSize.width() - logicalSize.width()) / 2.0,
                               (thumbSize.height() - logicalSize.height()) / 2.0),
                       thumb);

    if (item.isSpoiler) {
        painter.fillRect(QRect(QPoint(0, 0), thumbSize), QColor(0, 0, 0, 100));

        QFont spoilerFont = font();
        spoilerFont.setBold(true);
        painter.setFont(spoilerFont);
        painter.setPen(Qt::white);
        painter.drawText(QRect(QPoint(0, 0), thumbSize), Qt::AlignCenter, tr("SPOILER"));
    }

    return canvas;
}

void AttachmentPreviewPanel::editAttachment(int index)
{
    if (index < 0 || index >= items.size())
        return;

    AttachmentEditPopup popup(items[index], this);
    if (popup.exec() == QDialog::Accepted) {
        rebuildCards();
        emit attachmentsChanged();
    }
}

void AttachmentPreviewPanel::showNotice(const QString &message)
{
    errorLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;")
                                      .arg(Core::Theme::Manager::instance().color(Core::Theme::Token::MutedText).name()));
    errorLabel->setText(message);
    errorLabel->setVisible(true);
    errorTimer->stop();
    updateVisibility();
    emit attachmentsChanged();
}

void AttachmentPreviewPanel::showError(const QString &message)
{
    errorLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;")
                                      .arg(Core::Theme::Manager::instance().color(Core::Theme::Token::ChatError).name()));
    errorLabel->setText(message);
    errorLabel->setVisible(true);
    errorTimer->start();
    updateVisibility();
    emit attachmentsChanged();
}

void AttachmentPreviewPanel::updateVisibility()
{
    // isHidden: isVisible is false while the panel itself is hidden
    setVisible(!items.isEmpty() || !errorLabel->isHidden() || !progressRow->isHidden());
    // Only a notice or progress: no empty row of cards under it
    scrollArea->setVisible(!items.isEmpty());
}

} // namespace UI
} // namespace Acheron
