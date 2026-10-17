#include "MessageInput.hpp"
#include "AttachmentPreviewPanel.hpp"
#include "TextEdgeNavigation.hpp"

#include "Core/Emoji/UnicodeEmojiIndex.hpp"
#include "Core/Emoji/EmojiManager.hpp"
#include "Core/Theme/Icons.hpp"
#include "Core/Theme/Manager.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QAbstractTextDocumentLayout>
#include <QDragEnterEvent>
#include <QImage>
#include <QMimeData>
#include <QToolButton>
#include <QFileDialog>

#include <algorithm>

namespace Acheron {
namespace UI {

ChatTextEdit::ChatTextEdit(QWidget *parent) : QTextEdit(parent)
{
    setObjectName("MessageInput");
    document()->setDocumentMargin(0);
    setAcceptRichText(false);

    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    setPlaceholderText("Message...");
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void ChatTextEdit::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        if (!(e->modifiers() & Qt::ShiftModifier)) {
            emit returnPressed();
            return;
        }
    }
    if (e->key() == Qt::Key_Escape) {
        emit escapePressed();
        return;
    }
    if (e->key() == Qt::Key_Up && e->modifiers() == Qt::NoModifier && document()->isEmpty()) {
        emit editLastMessageRequested();
        return;
    }
    if (moveCursorToTextEdgeFromOuterLine(this, e))
        return;
    QTextEdit::keyPressEvent(e);
}

static bool mimeHasLocalFiles(const QMimeData *source)
{
    if (!source->hasUrls())
        return false;
    const auto urls = source->urls();
    return std::any_of(urls.begin(), urls.end(),
                       [](const QUrl &url) { return url.isLocalFile(); });
}

bool ChatTextEdit::canInsertFromMimeData(const QMimeData *source) const
{
    return mimeHasLocalFiles(source) || source->hasImage() || QTextEdit::canInsertFromMimeData(source);
}

void ChatTextEdit::insertFromMimeData(const QMimeData *source)
{
    if (mimeHasLocalFiles(source)) {
        emit filesPasted(source->urls());
        return;
    }
    if (source->hasImage()) {
        QImage image = qvariant_cast<QImage>(source->imageData());
        if (!image.isNull()) {
            emit imagePasted(image);
            return;
        }
    }
    QTextEdit::insertFromMimeData(source);
}

MessageInput::MessageInput(QWidget *parent) : QWidget(parent)
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(16, 0, 16, 0);
    outerLayout->setSpacing(0);

    // Reply bar
    replyBar = new QWidget(this);
    replyBar->setVisible(false);
    auto *replyLayout = new QHBoxLayout(replyBar);
    replyLayout->setContentsMargins(8, 4, 4, 2);
    replyLayout->setSpacing(4);

    replyLabel = new QLabel(replyBar);
    replyLabel->setStyleSheet("color: #b5bac1; font-size: 12px;");
    replyLayout->addWidget(replyLabel, 1);

    replyMentionButton = new QToolButton(replyBar);
    replyMentionButton->setAutoRaise(true);
    replyMentionButton->setCursor(Qt::PointingHandCursor);
    replyMentionButton->setToolTip(tr("Click to toggle whether this reply pings the author"));
    replyLayout->addWidget(replyMentionButton);
    connect(replyMentionButton, &QToolButton::clicked, this, [this]() {
        replyMention = !replyMention;
        updateReplyMentionButton();
    });

    replyCancelButton = new QToolButton(replyBar);
    replyCancelButton->setIcon(Core::Theme::Icons::icon(Core::Theme::Icons::Name::X, Core::Theme::Token::PlaceholderText));
    replyCancelButton->setIconSize(QSize(14, 14));
    replyCancelButton->setFixedSize(16, 16);
    replyCancelButton->setAutoRaise(true);
    replyCancelButton->setCursor(Qt::PointingHandCursor);
    replyCancelButton->setStyleSheet("QToolButton { border: none; }");
    replyLayout->addWidget(replyCancelButton);

    connect(replyCancelButton, &QToolButton::clicked, this, &MessageInput::clearReplyTarget);

    outerLayout->addWidget(replyBar);

    // Qord: stickers going with the message, each removable
    stickerBar = new QWidget(this);
    stickerBar->setVisible(false);
    stickerLayout = new QHBoxLayout(stickerBar);
    stickerLayout->setContentsMargins(8, 4, 4, 2);
    stickerLayout->setSpacing(6);
    outerLayout->addWidget(stickerBar);

    attachmentPanel = new AttachmentPreviewPanel(this);
    outerLayout->addWidget(attachmentPanel);

    // Text edit
    auto *inputContainer = new QWidget(this);
    // Qord: Discord's rounded message box with an attach button on the left
    inputContainer->setObjectName("MessageInputBox");
    inputContainer->setAttribute(Qt::WA_StyledBackground, true);
    inputLayout = new QHBoxLayout(inputContainer);
    inputLayout->setContentsMargins(10, 0, 8, 0);
    inputLayout->setSpacing(4);

    auto *attachButton = new QToolButton(inputContainer);
    attachButton->setIcon(Core::Theme::Icons::icon(Core::Theme::Icons::Name::CirclePlus, Core::Theme::Token::MutedText));
    attachButton->setIconSize(QSize(22, 22));
    attachButton->setFixedSize(28, 44); // centred on a single-line box, stays at the top as it grows
    attachButton->setAutoRaise(true);
    attachButton->setCursor(Qt::PointingHandCursor);
    attachButton->setToolTip(tr("Upload a File"));
    attachButton->setStyleSheet("QToolButton { border: none; background: transparent; }");
    connect(attachButton, &QToolButton::clicked, this, [this]() {
        const QList<QUrl> files = QFileDialog::getOpenFileUrls(this, tr("Upload a File"));
        if (!files.isEmpty())
            queueAttachments(files);
    });
    inputLayout->addWidget(attachButton, 0, Qt::AlignTop);

    textEdit = new ChatTextEdit(inputContainer);
    setFocusProxy(textEdit);

    connect(textEdit, &ChatTextEdit::returnPressed, this, &MessageInput::submit);

    connect(textEdit, &ChatTextEdit::editLastMessageRequested, this, &MessageInput::editLastMessageRequested);

    connect(textEdit, &ChatTextEdit::escapePressed, this, [this]() {
        clearReplyTarget();
        attachmentPanel->clearAttachments();
        clearStickers();
    });

    connect(textEdit->document(), &QTextDocument::contentsChanged, this,
            &MessageInput::adjustHeight);
    connect(textEdit, &QTextEdit::textChanged, this, [this]() {
        if (!textEdit->document()->isEmpty())
            emit userTyping();
        checkDraftEmpty();
        updateLengthCounter();
    });

    autocomplete = new EmojiAutocomplete(textEdit, this);

    connect(textEdit, &ChatTextEdit::filesPasted, this, &MessageInput::queueAttachments);
    connect(textEdit, &ChatTextEdit::imagePasted, attachmentPanel, &AttachmentPreviewPanel::addImage);
    connect(attachmentPanel, &AttachmentPreviewPanel::attachmentsChanged, this, &MessageInput::adjustHeight);
    connect(attachmentPanel, &AttachmentPreviewPanel::attachmentsChanged, this, &MessageInput::checkDraftEmpty);

    inputLayout->addWidget(textEdit);

    // Qord: GIF and sticker buttons, like Discord's message box
    const auto addTextButton = [&](const QString &label, const QString &tooltip, void (MessageInput::*signal)(QWidget *)) {
        auto *button = new QToolButton(inputContainer);
        button->setText(label);
        button->setObjectName(QStringLiteral("MessageBoxTextButton"));
        button->setFixedSize(label.size() > 1 ? 34 : 30, 44);
        button->setAutoRaise(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(tooltip);
        connect(button, &QToolButton::clicked, this, [this, button, signal]() { emit (this->*signal)(button); });
        inputLayout->addWidget(button, 0, Qt::AlignTop);
    };
    addTextButton(QStringLiteral("GIF"), tr("Open GIF picker"), &MessageInput::gifButtonClicked);
    addTextButton(QStringLiteral("\u25A3"), tr("Open sticker picker"), &MessageInput::stickerButtonClicked);

    auto *emojiButton = new QToolButton(inputContainer);
    emojiButton->setIcon(Core::Theme::Icons::icon(Core::Theme::Icons::Name::Smile, Core::Theme::Token::MutedText));
    emojiButton->setIconSize(QSize(22, 22));
    emojiButton->setFixedSize(30, 44);
    emojiButton->setAutoRaise(true);
    emojiButton->setCursor(Qt::PointingHandCursor);
    emojiButton->setToolTip(tr("Select emoji"));
    emojiButton->setStyleSheet("QToolButton { border: none; background: transparent; }");
    connect(emojiButton, &QToolButton::clicked, this, [this, emojiButton]() { emit emojiButtonClicked(emojiButton); });
    inputLayout->addWidget(emojiButton, 0, Qt::AlignTop);
    outerLayout->addWidget(inputContainer);

    setAcceptDrops(true);

    // Qord: Discord's character counter, bottom right of the box
    lengthCounter = new QLabel(this);
    lengthCounter->setObjectName(QStringLiteral("MessageLengthCounter"));
    lengthCounter->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    lengthCounter->hide();

    adjustHeight();
}

void MessageInput::setMessageLengthLimit(int limit)
{
    lengthLimit = limit > 0 ? limit : 2000;
    updateLengthCounter();
}

// Shown from 200 characters before the limit: what's left, or how far over
// it is in red; when FakeNitro splits long messages, how many it will send
void MessageInput::updateLengthCounter()
{
    if (!lengthCounter)
        return;
    QString text = textEdit->toPlainText();
    static const QString SilentPrefix = QStringLiteral("@silent ");
    if (text.startsWith(SilentPrefix))
        text = text.mid(SilentPrefix.size());
    const int length = int(text.size());
    if (length < lengthLimit - 200) {
        lengthCounter->hide();
        return;
    }
    const auto &theme = Core::Theme::Manager::instance();
    const auto &split = Core::expressionBypass().splitCount;
    QColor color = theme.color(Core::Theme::Token::MutedText);
    if (length <= lengthLimit) {
        lengthCounter->setText(QString::number(lengthLimit - length));
        lengthCounter->setToolTip(tr("%1 of %2 characters").arg(length).arg(lengthLimit));
    } else if (split) {
        const int parts = split(text, lengthLimit);
        lengthCounter->setText(tr("%1 messages").arg(parts));
        lengthCounter->setToolTip(tr("FakeNitro will send this as %1 messages of up to %2 characters")
                                          .arg(parts)
                                          .arg(lengthLimit));
    } else {
        color = theme.color(Core::Theme::Token::ChatError);
        lengthCounter->setText(QString::number(lengthLimit - length));
        lengthCounter->setToolTip(tr("Your message is too long (%1 of %2 characters)").arg(length).arg(lengthLimit));
    }
    lengthCounter->setStyleSheet(QStringLiteral("color: %1; font-size: 11px; background: transparent;").arg(color.name()));
    lengthCounter->adjustSize();
    lengthCounter->move(width() - lengthCounter->width() - 12, height() - lengthCounter->height() - 2);
    lengthCounter->show();
    lengthCounter->raise();
}

void MessageInput::setPlaceholder(const QString &name)
{
    textEdit->setPlaceholderText(name);
}

void MessageInput::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateLengthCounter();
}

void MessageInput::setSending(bool value)
{
    sending = value;
    textEdit->setReadOnly(value);
}

void MessageInput::clear()
{
    textEdit->clear();
    clearReplyTarget();
    attachmentPanel->clearAttachments();
    clearStickers();
    adjustHeight();
}

void MessageInput::submit()
{
    if (sendBlocked || sending)
        return;
    const QString txt = draftText();
    if (txt.isEmpty() && !attachmentPanel->hasAttachments() && stagedStickers.isEmpty())
        return;
    emit sendMessage(txt, attachmentPanel->attachments());
    if (keepAfterSend)
        keepAfterSend = false;
    else
        clear();
}

bool MessageInput::addSticker(Core::Snowflake id, const QString &name)
{
    if (stagedStickers.size() >= 3)
        return false;
    for (const StagedSticker &staged : std::as_const(stagedStickers))
        if (staged.id == id)
            return true;
    stagedStickers.append({ id, name });
    rebuildStickerBar();
    return true;
}

QList<Core::Snowflake> MessageInput::stickerIds() const
{
    QList<Core::Snowflake> ids;
    for (const StagedSticker &staged : stagedStickers)
        ids.append(staged.id);
    return ids;
}

QList<Core::PendingAttachment> MessageInput::pendingAttachments() const
{
    return attachmentPanel->attachments();
}

void MessageInput::clearStickers()
{
    if (stagedStickers.isEmpty())
        return;
    stagedStickers.clear();
    rebuildStickerBar();
}

void MessageInput::rebuildStickerBar()
{
    // Hidden while rebuilt, so showing it again shows the new chips too
    stickerBar->hide();
    while (QLayoutItem *item = stickerLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    if (!stagedStickers.isEmpty()) {
        auto *label = new QLabel(tr("Stickers:"), stickerBar);
        label->setStyleSheet("color: #b5bac1; font-size: 12px;");
        stickerLayout->addWidget(label);
        for (const StagedSticker &staged : std::as_const(stagedStickers)) {
            auto *chip = new QToolButton(stickerBar);
            chip->setText(staged.name.isEmpty() ? tr("Sticker") : staged.name);
            chip->setIcon(Core::Theme::Icons::icon(Core::Theme::Icons::Name::X, Core::Theme::Token::PlaceholderText));
            chip->setIconSize(QSize(12, 12));
            chip->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            chip->setLayoutDirection(Qt::RightToLeft); // the x after the name
            chip->setAutoRaise(true);
            chip->setCursor(Qt::PointingHandCursor);
            chip->setToolTip(tr("Remove"));
            const Core::Snowflake id = staged.id;
            connect(chip, &QToolButton::clicked, this, [this, id]() {
                for (qsizetype i = 0; i < stagedStickers.size(); ++i)
                    if (stagedStickers[i].id == id)
                        stagedStickers.removeAt(i--);
                rebuildStickerBar();
            });
            stickerLayout->addWidget(chip);
        }
        stickerLayout->addStretch(1);
    }
    stickerBar->setVisible(!stagedStickers.isEmpty());
    adjustHeight();
    checkDraftEmpty();
}

void MessageInput::attachData(const QString &fileName, const QByteArray &data, const QString &mimeType)
{
    attachmentPanel->addData(fileName, data, mimeType);
}

void MessageInput::queueAttachments(const QList<QUrl> &urls)
{
    attachmentPanel->addFiles(urls);
    textEdit->setFocus();
}

void MessageInput::setMaxUploadSize(qint64 bytes)
{
    attachmentPanel->setMaxFileSize(bytes);
}

void MessageInput::dragEnterEvent(QDragEnterEvent *event)
{
    const auto urls = event->mimeData()->urls();
    if (event->mimeData()->hasUrls() &&
        std::any_of(urls.begin(), urls.end(),
                    [](const QUrl &url) { return url.isLocalFile(); }))
        event->acceptProposedAction();
}

void MessageInput::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasUrls())
        return;
    event->acceptProposedAction();
    queueAttachments(event->mimeData()->urls());
}

void MessageInput::setReplyTarget(Core::Snowflake messageId, const QString &authorName,
                                  const QString &contentSnippet, bool mentionAuthor)
{
    replyMessageId = messageId;
    replyMention = mentionAuthor;
    updateReplyMentionButton();
    QString snippet = contentSnippet;
    snippet.replace('\n', ' ');
    if (snippet.length() > 100)
        snippet = snippet.left(100) + "...";

    replyLabel->setText(tr("Replying to <b>%1</b> %2").arg(authorName, snippet));
    replyBar->setVisible(true);
    adjustHeight();
    textEdit->setFocus();
}

void MessageInput::clearReplyTarget()
{
    if (!replyMessageId.isValid())
        return;

    replyMessageId = Core::Snowflake::Invalid;
    replyBar->setVisible(false);
    adjustHeight();
}

void MessageInput::updateReplyMentionButton()
{
    replyMentionButton->setText(replyMention ? tr("@ ON") : tr("@ OFF"));
    replyMentionButton->setStyleSheet(
            replyMention ? QStringLiteral("QToolButton { border: none; color: #5865f2; font-weight: bold; font-size: 12px; }")
                         : QStringLiteral("QToolButton { border: none; color: #b5bac1; font-weight: bold; font-size: 12px; }"));
}

void MessageInput::setSendBlocked(bool blocked)
{
    sendBlocked = blocked;
}

void MessageInput::addTrailingWidget(QWidget *widget)
{
    // Same row as the GIF and emoji buttons: centred on a single-line box,
    // staying at the top as the box grows
    widget->setFixedHeight(44);
    inputLayout->addWidget(widget, 0, Qt::AlignTop);
}

QTextEdit *MessageInput::editor() const
{
    return textEdit;
}

void MessageInput::insertText(const QString &text)
{
    textEdit->insertPlainText(text);
    textEdit->setFocus();
}

void MessageInput::adjustHeight()
{
    int contentHeight = textEdit->document()->size().height();

    int newHeight = contentHeight + contentHeight;

    if (newHeight < 44)
        newHeight = 44;
    if (newHeight > 200)
        newHeight = 200;

    if (newHeight >= 200)
        textEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    else
        textEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    textEdit->setFixedHeight(newHeight);

    int totalHeight = newHeight + 12;
    if (replyBar->isVisible())
        totalHeight += replyBar->sizeHint().height();
    if (stickerBar && !stickerBar->isHidden())
        totalHeight += stickerBar->sizeHint().height();
    if (attachmentPanel->isVisible())
        totalHeight += attachmentPanel->sizeHint().height();

    setFixedHeight(totalHeight);
}

QString MessageInput::draftText() const
{
    return Core::UnicodeEmojiIndex::instance().translateNamesToSurrogates(textEdit->toPlainText().trimmed());
}

bool MessageInput::hasPendingAttachments() const
{
    return attachmentPanel->hasAttachments();
}

void MessageInput::checkDraftEmpty()
{
    const bool empty = textEdit->document()->isEmpty() && !attachmentPanel->hasAttachments() && stagedStickers.isEmpty();
    if (empty == draftEmpty)
        return;
    draftEmpty = empty;
    emit draftEmptyChanged(empty);
}

} // namespace UI
} // namespace Acheron
