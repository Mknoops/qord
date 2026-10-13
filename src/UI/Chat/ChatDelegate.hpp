#pragma once

#include <QHash>
#include <QStyledItemDelegate>

namespace Acheron {
struct AttachmentData;
namespace Core {
class ImageManager;
} // namespace Core
namespace UI {
class ChatModel;
class ChatView;
class EmojiTextObject;

class ChatDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit ChatDelegate(Core::ImageManager *imageManager, ChatView *view);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    // Qord: tooltips in a message header (name decorations, the timestamp),
    // where they were last painted
    struct HeaderTip
    {
        QRect rect;
        QString text;
        // A clickable name decoration (UserVoiceShow)
        QString pluginId;
        QString clickId;
        quint64 userId = 0;
    };
    [[nodiscard]] QList<HeaderTip> headerTips(quint64 messageId) const { return lastHeaderTips.value(messageId); }

    // Qord: a text file's preview (ChatLayout::textPreviewButtons)
    void paintTextPreview(QPainter *painter, const QStyleOptionViewItem &option, const QRect &box,
                          const Acheron::AttachmentData &att) const;
    // Qord: Discord's header timestamp ("Today at 3:45 PM")
    static QString headerTimestamp(const QDateTime &timestamp);

private:
    mutable QHash<quint64, QList<HeaderTip>> lastHeaderTips;
    bool paintBodyTextOnly(QPainter *painter, const QStyleOptionViewItem &option,
                           const QModelIndex &index, const ChatModel *chatModel, const ChatView *chatView,
                           const QRect &textRect, const QRect &damage) const;

    EmojiTextObject *emojiHandler;
};
} // namespace UI
} // namespace Acheron
