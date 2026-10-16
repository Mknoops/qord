#pragma once

#include <QWidget>

#include <atomic>
#include <memory>

#include "Core/PendingAttachment.hpp"

class QHBoxLayout;
class QLabel;
class QProgressBar;
class QScrollArea;
class QTimer;

namespace Acheron {
namespace UI {

class AttachmentPreviewPanel : public QWidget
{
    Q_OBJECT
public:
    explicit AttachmentPreviewPanel(QWidget *parent = nullptr);

    void setMaxFileSize(qint64 bytes);
    void addFiles(const QList<QUrl> &urls);
    void addImage(const QImage &image);
    // Qord: a file made in memory (FakeNitro's animated stickers)
    void addData(const QString &fileName, const QByteArray &data, const QString &mimeType);
    [[nodiscard]] bool hasAttachments() const { return !items.isEmpty(); }
    [[nodiscard]] QList<Core::PendingAttachment> attachments() const { return items; }
    void clearAttachments();

signals:
    void attachmentsChanged();

private:
    static constexpr int MaxFiles = 10;
    bool appendAttachment(Core::PendingAttachment attachment);
    bool startShrinking(const Core::PendingAttachment &attachment);
    bool startShrinkingVideo(const Core::PendingAttachment &attachment);
    void showNotice(const QString &message);
    void rebuildCards();
    QWidget *createCard(int index);
    QPixmap thumbnailFor(const Core::PendingAttachment &item) const;
    void editAttachment(int index);
    void showError(const QString &message);
    void updateVisibility();

    QList<Core::PendingAttachment> items;
    QScrollArea *scrollArea;
    QWidget *cardsContainer;
    QHBoxLayout *cardsLayout;
    QLabel *errorLabel;
    QTimer *errorTimer;
    // Qord: a video being shrunk, with a way to stop it
    QWidget *progressRow = nullptr;
    QProgressBar *progressBar = nullptr;
    std::shared_ptr<std::atomic<bool>> stopShrinking;
    qint64 maxFileSize = 10ll * 1024 * 1024;
};

} // namespace UI
} // namespace Acheron
