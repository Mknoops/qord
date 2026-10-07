#pragma once

#include <QWidget>
#include <QTextEdit>
#include <QLabel>

#include "Core/PendingAttachment.hpp"
#include "Core/Snowflake.hpp"
#include "EmojiAutocompletePopup.hpp"

class QHBoxLayout;
class QToolButton;

namespace Acheron {
namespace UI {

class AttachmentPreviewPanel;

class ChatTextEdit : public QTextEdit
{
    Q_OBJECT
public:
    explicit ChatTextEdit(QWidget *parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;
signals:
    void returnPressed();
    void escapePressed();
    void editLastMessageRequested();
    void filesPasted(const QList<QUrl> &urls);
    void imagePasted(const QImage &image);
};

class MessageInput : public QWidget
{
    Q_OBJECT
public:
    explicit MessageInput(QWidget *parent = nullptr);
    void clear();
    void setPlaceholder(const QString &name);

    void setReplyTarget(Core::Snowflake messageId, const QString &authorName,
                        const QString &contentSnippet, bool mentionAuthor = true);
    void clearReplyTarget();
    [[nodiscard]] Core::Snowflake replyTargetMessageId() const { return replyMessageId; }
    // Whether the reply pings its author (the "@ ON/OFF" toggle)
    [[nodiscard]] bool replyMentionEnabled() const { return replyMention; }

    void setSendBlocked(bool blocked);
    [[nodiscard]] bool isSendBlocked() const { return sendBlocked; }

    void insertText(const QString &text);
    // Places a widget to the right of the text box (used for plugin buttons)
    void addTrailingWidget(QWidget *widget);

    void queueAttachments(const QList<QUrl> &urls);
    void setMaxUploadSize(qint64 bytes);

    [[nodiscard]] EmojiAutocomplete *emojiAutocomplete() const { return autocomplete; }

protected:
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

signals:
    void sendMessage(const QString &text, const QList<Core::PendingAttachment> &attachments);
    void editLastMessageRequested();

private:
    ChatTextEdit *textEdit;
    QHBoxLayout *inputLayout;
    QWidget *replyBar;
    QLabel *replyLabel;
    QToolButton *replyMentionButton;
    QToolButton *replyCancelButton;
    AttachmentPreviewPanel *attachmentPanel;
    EmojiAutocomplete *autocomplete;

    Core::Snowflake replyMessageId;
    bool replyMention = true;
    bool sendBlocked = false;

    void adjustHeight();
    void updateReplyMentionButton();
};

} // namespace UI
} // namespace Acheron
