#pragma once

#include <QFrame>
#include <QHash>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include "Core/Snowflake.hpp"

class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QStandardItem;
class QStandardItemModel;

namespace Acheron {
namespace Core {
class ClientInstance;
class ImageManager;
} // namespace Core

namespace UI {

// Discord's GIF and sticker pickers (the GIFs / Stickers tabs next to Emoji).
// GIFs come from Discord's Tenor search (/gifs/trending-gifs, /gifs/search)
// and are sent as their link; stickers are the current server's stickers.
class ExpressionPicker : public QFrame
{
    Q_OBJECT
public:
    enum class Tab { Gifs, Stickers };

    ExpressionPicker(Core::ImageManager *images, QWidget *parent = nullptr);

    void open(Core::ClientInstance *instance, Core::Snowflake channelId, Core::Snowflake guildId, Tab tab,
              QWidget *anchor);

signals:
    void gifPicked(const QString &url);
    // `more`: shift was held, so the picker stays open to add more stickers
    void stickerPicked(Core::Snowflake stickerId, const QString &name, bool more);
    void emojiTabRequested(QWidget *anchor);

private:
    void showTab(Tab tab);
    void loadGifs();
    void loadStickers();
    void addImageItem(const QString &label, const QUrl &image, const QVariant &payload, const QSize &cell);
    void pick(const QModelIndex &index);

    Core::ImageManager *images;
    QPointer<Core::ClientInstance> instance;
    Core::Snowflake channelId;
    Core::Snowflake guildId;
    QPointer<QWidget> anchor;
    Tab tab = Tab::Gifs;
    int generation = 0; // drops replies for an older search

    QPushButton *gifTab;
    QPushButton *stickerTab;
    QPushButton *emojiTab;
    QLineEdit *search;
    QListView *view;
    QLabel *status;
    QStandardItemModel *model;
    QTimer searchDelay;
    QHash<QUrl, QList<QStandardItem *>> pendingImages;
};

} // namespace UI
} // namespace Acheron
