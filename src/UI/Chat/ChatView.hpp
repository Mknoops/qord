#pragma once

#include "Plugins/Types.hpp"

#include <optional>
#include <QtWidgets>
#include <QClipboard>
#include <QGuiApplication>

#include <optional>

#include "ChatLayout.hpp"
#include "ChatModel.hpp"
#include "MessageActionBar.hpp"
#include "Core/Snowflake.hpp"

namespace Acheron {
namespace UI {

class FrameAnimator;
class ImageViewer;
class InlineVideoController;
struct ChatCursor
{
    int row = -1;
    int index = -1;

    bool isValid() const { return row >= 0 && index >= 0; }

    bool operator==(const ChatCursor &other) const
    {
        return row == other.row && index == other.index;
    }
    bool operator!=(const ChatCursor &other) const { return !(*this == other); }
    bool operator<(const ChatCursor &other) const
    {
        if (row != other.row)
            return row < other.row;
        return index < other.index;
    }
};

class JumpToPresentBar : public QWidget
{
    Q_OBJECT
public:
    explicit JumpToPresentBar(QWidget *parent = nullptr);

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    void applyTheme();

    QLabel *label = nullptr;
    QPushButton *button = nullptr;
};

class ChatView : public QListView
{
    Q_OBJECT
public:
    ChatView(QWidget *parent = nullptr);

    void setImageManager(Core::ImageManager *manager) { imageManager = manager; }

    int hoveredRowAtPaint() const { return hoveredRow; }
    int hoveredCharIndexAtPaint() const { return hoveredChar; }
    bool replyBarHoveredAtPaint() const { return hoveredReplyBar; }
    int hoveredMessageRowAtPaint() const { return hoveredMessage.isValid() ? hoveredMessage.row() : -1; }
    int editingRow() const { return currentEditingIndex.isValid() ? currentEditingIndex.row() : -1; }

    // jump flash
    int highlightedRow() const;
    qreal highlightOpacity() const { return highlightAlpha; }

    [[nodiscard]] InlineVideoController *videoController() const { return video; }
    [[nodiscard]] FrameAnimator *frameAnimator() const { return animator; }

    static constexpr int InlineEditMinHeight = 60;

    bool hasTextSelection() const;
    // Qord: the selected text (MessageClickActions' quotes), and editing a message by id
    QString selectedChatText() const;
    bool startEditingMessage(Core::Snowflake messageId);
    [[nodiscard]] Core::Snowflake editingMessageId() const { return currentEditingMessageId; }
    void stopEditingMessage() { cancelInlineEdit(); }

    ChatCursor selectionStart() const;
    ChatCursor selectionEnd() const;

    void setModel(QAbstractItemModel *model) override;

    void setCurrentUserId(Core::Snowflake userId);
    void setCanPinMessages(bool canPin);
    // Qord: Create Thread on messages (a text channel, with permission)
    void setCanCreateThreads(bool canCreate) { canCreateThreads = canCreate; }
    void setCanManageMessages(bool canManage);
    // Qord: tells the plugin a name decoration at `pos` was clicked
    bool clickHeaderDecoration(const QPoint &pos, bool doubleClick, bool dryRun = false);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    // Qord: clickable name decorations (UserVoiceShow)
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool viewportEvent(QEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void clearSelection();
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void updateGeometries() override;

signals:
    void historyRequested();
    void futureRequested();
    void jumpRequested(Core::Snowflake messageId);
    void presentRequested();
    void atBottomChanged(bool atBottom);
    void editMessageRequested(Core::Snowflake channelId, Core::Snowflake messageId, const QString &currentContent);
    void deleteMessageRequested(Core::Snowflake channelId, Core::Snowflake messageId);
    void pinMessageRequested(Core::Snowflake channelId, Core::Snowflake messageId);
    // Qord: suggestedName is the start of the message, as Discord prefills it
    void createThreadRequested(Core::Snowflake channelId, Core::Snowflake messageId, const QString &suggestedName);
    void openThreadRequested(Core::Snowflake threadId);
    // Qord: an avatar on a reaction (WhoReacted)
    void reactionUserClicked(Core::Snowflake userId);
    // Qord: a user mention was clicked or the mouse entered one
    void userMentionClicked(Core::Snowflake userId);
    void userMentionHovered(Core::Snowflake userId, Core::Snowflake messageId);
    void replyToMessageRequested(Core::Snowflake channelId, Core::Snowflake messageId);
    void addReactionRequested(Core::Snowflake channelId, Core::Snowflake messageId);
    void cancelUploadRequested(Core::Snowflake channelId, Core::Snowflake messageId);
    // Qord: "Dismiss message" under a message only you can see
    void dismissMessageRequested(Core::Snowflake channelId, Core::Snowflake messageId);
    void filesDropped(const QList<QUrl> &urls);
    void toggleReactionClicked(Core::Snowflake channelId, Core::Snowflake messageId,
                               const QString &emoji, bool currentlyReacted, bool isBurst);
    void channelMentionClicked(Core::Snowflake channelId);
    void messageLinkClicked(Core::Snowflake channelId, Core::Snowflake messageId);
    void userContextMenuRequested(Core::Snowflake userId, QPoint globalPos);
    void inlineEditFinished();

public slots:
    void onHistoryRequestFinished();
    void onFutureRequestFinished(bool loadedMore);
    void editLastOwnMessage();
    void jumpToMessage(Core::Snowflake messageId);
    void openLink(const QString &url);
    void jumpToPresent();

private slots:
    void onScrollBarValueChanged(int value);
    void onModelReset();
    void onAtLatestChanged(bool atLatest);
    void onRowsAboutToBeInserted(const QModelIndex &parent, int start, int end);
    void onRowsInserted(const QModelIndex &parent, int start, int end);
    void onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight);
    void onActionBarTriggered(MessageActionBar::Action action);
    void onActionBarMoreRequested(const QPoint &buttonTopLeft);
    void onActionBarPluginButton(const QString &pluginId, const QString &buttonId, bool rightClick);
    void updatePluginPopoverButtons(const QModelIndex &index);

private:
    // Qord: plugin message filters (hidden rows) and context
    void applyMessageFilters(int first = 0, int last = -1);
    [[nodiscard]] std::optional<Qord::Plugins::MessageContext> pluginContext(int row, bool withJson = false) const;

    void copySelectedText();
    void copyMessageContent(const QModelIndex &index);
    static void copyMessageId(Core::Snowflake messageId);
    void copyMessageLink(Core::Snowflake messageId);
    void copyImage(const QUrl &proxyUrl, const QPixmap &preview);
    void saveMedia(const QUrl &url, const QString &filename);
    void startInlineEdit(const QModelIndex &index);
    void commitInlineEdit();
    void cancelInlineEdit();
    bool scrollToMessage(Core::Snowflake messageId);
    void flashMessage(Core::Snowflake messageId);
    bool modelAtLatest() const;
    void updateScrollState();
    void maybeRequestFuture();
    void positionJumpToPresentBar();
    void updateJumpToPresentBar();
    bool canShowActionBar(const QModelIndex &index) const;
    bool canDeleteMessage(const QModelIndex &index) const;
    QModelIndex messageUnderCursor(const QPoint &globalPos) const;
    QPoint actionBarPosition(const QModelIndex &index) const;
    void updateHoveredMessage();
    void addMessageAction(QMenu &menu, MessageActionBar::Action action, Core::Snowflake messageId);
    void triggerMessageAction(MessageActionBar::Action action, Core::Snowflake messageId);
    void execMessageMenu(QMenu &menu, const QPoint &globalPos);
    void releaseHoverHold(const QPoint &globalPos);

    InlineVideoController *video = nullptr;
    FrameAnimator *animator = nullptr;
    Core::ImageManager *imageManager = nullptr;

    QTextEdit *inlineEditWidget = nullptr;
    Core::Snowflake currentEditingMessageId = Core::Snowflake::Invalid;
    QModelIndex currentEditingIndex;

    JumpToPresentBar *jumpToPresentBar = nullptr;
    MessageActionBar *actionBar = nullptr;
    QPersistentModelIndex hoveredMessage;
    std::optional<QPoint> hoverHeldAt;
    Core::Snowflake actionBarMessageId = Core::Snowflake::Invalid;
    Core::Snowflake pendingJumpMessageId = Core::Snowflake::Invalid;
    Core::Snowflake highlightedMessageId = Core::Snowflake::Invalid;
    qreal highlightAlpha = 0.0;
    QSequentialAnimationGroup *highlightAnimation = nullptr;

    int hoveredRow;
    int hoveredChar;
    bool hoveredReplyBar = false;
    QPair<Core::Snowflake, Core::Snowflake> hoveredMention; // Qord: message, user

    ChatCursor selectionAnchor;
    ChatCursor selectionHead;

    bool isFetchingTop = false;
    bool isFetchingBottom = false;

    QPersistentModelIndex anchorIndex;
    int anchorDistanceFromBottom = 0;

    void setAtBottom(bool value);
    bool atBottom = true;

    Core::Snowflake currentUserId = Core::Snowflake::Invalid;
    bool canPinMessages = false;
    bool canCreateThreads = false;
    bool canManageMessages = false;
};
} // namespace UI
} // namespace Acheron
