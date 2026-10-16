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
    // Qord: the text box (slash command menu)
    [[nodiscard]] QTextEdit *editor() const;
    // Places a widget to the right of the text box (used for plugin buttons)
    void addTrailingWidget(QWidget *widget);
    // Qord: the text as it would be sent, and whether files are attached
    // (PreviewMessage)
    [[nodiscard]] QString draftText() const;
    [[nodiscard]] bool hasPendingAttachments() const;

    void queueAttachments(const QList<QUrl> &urls);
    void setMaxUploadSize(qint64 bytes);
    // Qord: a file made in memory, and keeping the text and files when a
    // send is cancelled (called from the sendMessage handler)
    void attachData(const QString &fileName, const QByteArray &data, const QString &mimeType);
    void keepContentsAfterSend() { keepAfterSend = true; }
    // Qord: stickers to send with the message (Discord takes three). False
    // when there are three already.
    bool addSticker(Core::Snowflake id, const QString &name);
    [[nodiscard]] QList<Core::Snowflake> stickerIds() const;
    void clearStickers();
    // Sends what's in the box, as Enter does
    void submit();
    // Qord: a plugin is finishing the send (auto-translate); the text can't
    // be edited or sent again meanwhile
    void setSending(bool sending);
    [[nodiscard]] bool isSending() const { return sending; }

    [[nodiscard]] EmojiAutocomplete *emojiAutocomplete() const { return autocomplete; }

protected:
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

signals:
    void sendMessage(const QString &text, const QList<Core::PendingAttachment> &attachments);
    void editLastMessageRequested();
    // Qord: the user typed into the box (MainWindow throttles the typing indicator)
    void userTyping();
    // Qord: the emoji button in the message box was clicked
    void emojiButtonClicked(QWidget *anchor);
    // Qord: Discord's GIF and sticker buttons
    void gifButtonClicked(QWidget *anchor);
    void stickerButtonClicked(QWidget *anchor);
    // Qord: the box went from empty to holding text or files, or back
    void draftEmptyChanged(bool empty);

public:
    // Qord: Discord's length limit for the account; the box counts down near it
    void setMessageLengthLimit(int limit);

private:
    QLabel *lengthCounter = nullptr;
    int lengthLimit = 2000;
    void updateLengthCounter();
    ChatTextEdit *textEdit;
    bool sending = false;
    QHBoxLayout *inputLayout;
    QWidget *replyBar;
    QLabel *replyLabel;
    QToolButton *replyMentionButton;
    QToolButton *replyCancelButton;
    AttachmentPreviewPanel *attachmentPanel;
    EmojiAutocomplete *autocomplete;
    QWidget *stickerBar = nullptr;
    QHBoxLayout *stickerLayout = nullptr;
    struct StagedSticker
    {
        Core::Snowflake id;
        QString name;
    };
    QList<StagedSticker> stagedStickers;
    void rebuildStickerBar();

    Core::Snowflake replyMessageId;
    bool replyMention = true;
    bool sendBlocked = false;
    bool keepAfterSend = false;
    bool draftEmpty = true;
    void checkDraftEmpty();

    void adjustHeight();
    void updateReplyMentionButton();
};

} // namespace UI
} // namespace Acheron
