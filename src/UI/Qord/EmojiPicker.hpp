#pragma once

#include <QFrame>
#include <QHash>
#include <QPointer>

#include "Core/Emoji/EmojiMatch.hpp"
#include "Core/Snowflake.hpp"

class QLabel;
class QLineEdit;
class QListView;
class QStandardItem;
class QStandardItemModel;

namespace Acheron {
namespace Core {
class ClientInstance;
class ImageManager;
} // namespace Core

namespace UI {

// Discord's emoji picker: search, frequently used, each server's emojis and
// all Unicode emoji. Used by the message box and "Add Reaction".
class EmojiPicker : public QFrame
{
    Q_OBJECT
public:
    EmojiPicker(Core::ImageManager *images, QWidget *parent = nullptr);

    // Fills the picker for a channel; forReaction uses reaction frecency
    void open(Core::ClientInstance *instance, Core::Snowflake channelId, bool forReaction, QWidget *anchor);
    // Moves the open picker so its right edge sits at globalPos, kept on screen
    void moveNear(const QPoint &globalPos);

    // "name:id" for custom emoji, the surrogates for Unicode (the reaction API format)
    static QString reactionKey(const Core::EmojiMatch &match);

signals:
    void emojiPicked(const Core::EmojiMatch &match);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuild();
    void addSection(const QString &title, const QList<Core::EmojiMatch> &emojis);
    void pick(const QModelIndex &index);

    Core::ImageManager *images;
    QPointer<Core::ClientInstance> instance;
    Core::Snowflake channelId;
    bool forReaction = false;

    QLineEdit *search;
    QListView *view;
    QLabel *preview;
    QStandardItemModel *model;
    QHash<QUrl, QList<QStandardItem *>> pendingImages;
};

} // namespace UI
} // namespace Acheron
