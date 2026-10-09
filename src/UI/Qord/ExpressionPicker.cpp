#include "ExpressionPicker.hpp"

#include "Core/ClientInstance.hpp"
#include "Core/ImageManager.hpp"
#include "Core/Theme/Manager.hpp"
#include "Discord/CdnUrls.hpp"
#include "Discord/Client.hpp"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

namespace {
enum Roles {
    PayloadRole = Qt::UserRole + 1,
    CellRole,
};

const QSize GifCell(196, 128);
const QSize StickerCell(96, 96);

class ImageDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &index) const override
    {
        return index.data(CellRole).toSize();
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const auto &theme = Core::Theme::Manager::instance();
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF cell = QRectF(option.rect).adjusted(3, 3, -3, -3);
        QPainterPath clip;
        clip.addRoundedRect(cell, 6, 6);
        p->setClipPath(clip);
        p->fillRect(cell, theme.color(Core::Theme::Token::InputBg));
        const QPixmap pm = index.data(Qt::DecorationRole).value<QPixmap>();
        if (!pm.isNull()) {
            QSizeF size = QSizeF(pm.size()) / pm.devicePixelRatio();
            size.scale(cell.size(), Qt::KeepAspectRatio);
            p->drawPixmap(QRectF(cell.center() - QPointF(size.width() / 2, size.height() / 2), size), pm,
                          QRectF(QPointF(0, 0), QSizeF(pm.size())));
        } else {
            p->setPen(theme.color(Core::Theme::Token::MutedText));
            p->drawText(cell.adjusted(4, 4, -4, -4), Qt::AlignCenter | Qt::TextWordWrap,
                        index.data(Qt::DisplayRole).toString());
        }
        if (option.state & QStyle::State_MouseOver) {
            p->setPen(QPen(theme.color(Core::Theme::Token::Highlight), 2));
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(cell.adjusted(1, 1, -1, -1), 6, 6);
        }
        p->restore();
    }
};
} // namespace

ExpressionPicker::ExpressionPicker(Core::ImageManager *images, QWidget *parent)
    : QFrame(parent, Qt::Popup), images(images)
{
    setObjectName(QStringLiteral("EmojiPicker")); // same frame style as the emoji picker
    resize(436, 460);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto *tabs = new QHBoxLayout;
    tabs->setSpacing(4);
    const auto makeTab = [this, tabs](const QString &label) {
        auto *b = new QPushButton(label, this);
        b->setObjectName(QStringLiteral("PickerTab"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        tabs->addWidget(b);
        return b;
    };
    gifTab = makeTab(tr("GIFs"));
    stickerTab = makeTab(tr("Stickers"));
    emojiTab = makeTab(tr("Emoji"));
    tabs->addStretch();
    layout->addLayout(tabs);
    connect(gifTab, &QPushButton::clicked, this, [this]() { showTab(Tab::Gifs); });
    connect(stickerTab, &QPushButton::clicked, this, [this]() { showTab(Tab::Stickers); });
    connect(emojiTab, &QPushButton::clicked, this, [this]() {
        hide();
        emit emojiTabRequested(anchor);
    });

    search = new QLineEdit(this);
    search->setObjectName(QStringLiteral("EmojiPickerSearch"));
    search->setClearButtonEnabled(true);
    layout->addWidget(search);

    model = new QStandardItemModel(this);
    view = new QListView(this);
    view->setObjectName(QStringLiteral("EmojiPickerGrid"));
    view->setModel(model);
    view->setItemDelegate(new ImageDelegate(view));
    view->setViewMode(QListView::IconMode);
    view->setFlow(QListView::LeftToRight);
    view->setWrapping(true);
    view->setResizeMode(QListView::Adjust);
    view->setMovement(QListView::Static);
    view->setUniformItemSizes(true);
    view->setMouseTracking(true);
    view->viewport()->setAttribute(Qt::WA_Hover);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setFrameShape(QFrame::NoFrame);
    view->setCursor(Qt::PointingHandCursor);
    layout->addWidget(view, 1);

    status = new QLabel(this);
    status->setObjectName(QStringLiteral("EmojiPickerPreview"));
    layout->addWidget(status);

    searchDelay.setSingleShot(true);
    searchDelay.setInterval(350);
    connect(&searchDelay, &QTimer::timeout, this, [this]() {
        if (tab == Tab::Gifs)
            loadGifs();
        else
            loadStickers();
    });
    connect(search, &QLineEdit::textChanged, this, [this]() { searchDelay.start(); });
    connect(view, &QListView::clicked, this, &ExpressionPicker::pick);
    connect(view, &QListView::activated, this, &ExpressionPicker::pick);
    connect(images, &Core::ImageManager::imageFetched, this,
            [this](const QUrl &url, const QSize &, const QPixmap &pixmap) {
                for (QStandardItem *item : pendingImages.take(url))
                    item->setData(pixmap, Qt::DecorationRole);
            });
}

void ExpressionPicker::open(Core::ClientInstance *newInstance, Core::Snowflake newChannelId,
                            Core::Snowflake newGuildId, Tab newTab, QWidget *newAnchor)
{
    instance = newInstance;
    channelId = newChannelId;
    guildId = newGuildId;
    anchor = newAnchor;
    QPoint pos = newAnchor->mapToGlobal(QPoint(newAnchor->width() - width(), -height() - 4));
    if (QScreen *screen = newAnchor->screen()) {
        const QRect avail = screen->availableGeometry();
        pos.setX(qBound(avail.left(), pos.x(), avail.right() - width()));
        pos.setY(qBound(avail.top(), pos.y(), avail.bottom() - height()));
    }
    move(pos);
    show();
    raise();
    showTab(newTab);
    search->setFocus();
}

void ExpressionPicker::showTab(Tab newTab)
{
    tab = newTab;
    gifTab->setChecked(tab == Tab::Gifs);
    stickerTab->setChecked(tab == Tab::Stickers);
    emojiTab->setChecked(false);
    search->blockSignals(true);
    search->clear();
    search->blockSignals(false);
    search->setPlaceholderText(tab == Tab::Gifs ? tr("Search Tenor") : tr("Find the perfect sticker"));
    view->setGridSize(tab == Tab::Gifs ? GifCell : StickerCell);
    if (tab == Tab::Gifs)
        loadGifs();
    else
        loadStickers();
}

void ExpressionPicker::addImageItem(const QString &label, const QUrl &image, const QVariant &payload,
                                    const QSize &cell)
{
    auto *item = new QStandardItem(label);
    item->setData(payload, PayloadRole);
    item->setData(cell, CellRole);
    item->setToolTip(label);
    if (image.isValid() && instance) {
        const QPixmap pm = images->get(image, cell, instance->accountId());
        if (!pm.isNull())
            item->setData(pm, Qt::DecorationRole);
        else
            pendingImages[image].append(item);
    }
    model->appendRow(item);
}

void ExpressionPicker::loadGifs()
{
    model->clear();
    pendingImages.clear();
    if (!instance)
        return;
    const int request = ++generation;
    const QString query = search->text().trimmed();
    status->setText(tr("Loading…"));
    const QPointer<ExpressionPicker> guard(this);
    instance->discord()->fetchGifs(query, [guard, request](const Core::Result<QJsonDocument> &result) {
        if (!guard || request != guard->generation || guard->tab != Tab::Gifs)
            return;
        if (!result.success()) {
            guard->status->setText(tr("Couldn't load GIFs"));
            return;
        }
        // trending-gifs and search both return a list of {url, src, gif_src, preview, width, height}
        const QJsonArray gifs = result.value->isArray() ? result.value->array()
                                                        : result.value->object().value(QStringLiteral("gifs")).toArray();
        for (const auto &v : gifs) {
            const QJsonObject gif = v.toObject();
            const QString url = gif.value(QStringLiteral("url")).toString();
            if (url.isEmpty())
                continue;
            QString preview = gif.value(QStringLiteral("preview")).toString();
            if (preview.isEmpty())
                preview = gif.value(QStringLiteral("gif_src")).toString();
            guard->addImageItem(gif.value(QStringLiteral("title")).toString(), QUrl(preview), url, GifCell);
        }
        guard->status->setText(gifs.isEmpty() ? tr("No GIFs found") : tr("Powered by Tenor"));
    });
}

void ExpressionPicker::loadStickers()
{
    model->clear();
    pendingImages.clear();
    if (!instance)
        return;
    if (!guildId.isValid()) {
        status->setText(tr("Server stickers can be sent in servers"));
        return;
    }
    const int request = ++generation;
    const QString query = search->text().trimmed();
    status->setText(tr("Loading…"));
    const QPointer<ExpressionPicker> guard(this);
    instance->discord()->fetchGuildStickers(
            guildId, [guard, request, query](const Core::Result<QList<Discord::Sticker>> &result) {
                if (!guard || request != guard->generation || guard->tab != Tab::Stickers)
                    return;
                if (!result.success()) {
                    guard->status->setText(tr("Couldn't load stickers"));
                    return;
                }
                int shown = 0;
                const qreal dpr = guard->devicePixelRatioF();
                for (const auto &sticker : *result.value) {
                    const QString name = sticker.name.get();
                    const QString tags = sticker.tags.hasValue() ? sticker.tags.get() : QString();
                    if (!query.isEmpty() && !name.contains(query, Qt::CaseInsensitive) &&
                        !tags.contains(query, Qt::CaseInsensitive))
                        continue;
                    if (sticker.available.hasValue() && !sticker.available.get())
                        continue;
                    const QUrl still = Discord::Cdn::stickerStill(
                            sticker.id.get(), sticker.formatType.get(),
                            Discord::Cdn::stickerAssetPx(StickerCell.width(), dpr));
                    guard->addImageItem(name, still, quint64(sticker.id.get()), StickerCell);
                    ++shown;
                }
                guard->status->setText(shown ? QString() : tr("This server has no stickers"));
            });
}

void ExpressionPicker::pick(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    const QVariant payload = index.data(PayloadRole);
    hide();
    if (tab == Tab::Gifs)
        emit gifPicked(payload.toString());
    else
        emit stickerPicked(Core::Snowflake(payload.toULongLong()));
}

} // namespace UI
} // namespace Acheron
