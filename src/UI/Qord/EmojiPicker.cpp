#include "EmojiPicker.hpp"

#include "Core/ClientInstance.hpp"
#include "Core/Emoji/EmojiManager.hpp"
#include "Core/ImageManager.hpp"
#include "Core/Theme/Manager.hpp"

#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QGuiApplication>
#include <QScreen>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

namespace {

constexpr int CellSize = 44;
constexpr int GlyphPx = 30;
constexpr int HeaderHeight = 28;

enum Roles {
    IsHeaderRole = Qt::UserRole + 1,
    InsertTextRole,
    SurrogatesRole,
    CustomIdRole,
    ImageUrlRole,
    LabelRole,
};

class EmojiDelegate : public QStyledItemDelegate
{
public:
    explicit EmojiDelegate(QListView *view) : QStyledItemDelegate(view), view(view) {}

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &index) const override
    {
        if (index.data(IsHeaderRole).toBool())
            return QSize(view->viewport()->width() - 2, HeaderHeight);
        return QSize(CellSize, CellSize);
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const auto &theme = Core::Theme::Manager::instance();
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setRenderHint(QPainter::SmoothPixmapTransform);
        if (index.data(IsHeaderRole).toBool()) {
            QFont f = option.font;
            f.setPixelSize(12);
            f.setWeight(QFont::DemiBold);
            f.setCapitalization(QFont::AllUppercase);
            p->setFont(f);
            p->setPen(theme.color(Core::Theme::Token::MutedText));
            p->drawText(option.rect.adjusted(6, 0, 0, 0), Qt::AlignLeft | Qt::AlignBottom,
                        index.data(Qt::DisplayRole).toString());
            p->restore();
            return;
        }
        if (option.state & (QStyle::State_MouseOver | QStyle::State_Selected)) {
            p->setPen(Qt::NoPen);
            p->setBrush(theme.color(Core::Theme::Token::SelectedBg));
            p->drawRoundedRect(QRectF(option.rect).adjusted(2, 2, -2, -2), 6, 6);
        }
        const QRect glyph(option.rect.center() - QPoint(GlyphPx / 2, GlyphPx / 2), QSize(GlyphPx, GlyphPx));
        const QPixmap pm = index.data(Qt::DecorationRole).value<QPixmap>();
        if (!pm.isNull()) {
            p->drawPixmap(glyph, pm);
        } else {
            QFont f = option.font;
            f.setPixelSize(GlyphPx - 4);
            p->setFont(f);
            p->setPen(theme.color(Core::Theme::Token::PrimaryText));
            p->drawText(option.rect, Qt::AlignCenter, index.data(SurrogatesRole).toString());
        }
        p->restore();
    }

private:
    QListView *view;
};

Core::EmojiMatch matchFromIndex(const QModelIndex &index)
{
    Core::EmojiMatch m;
    m.insertText = index.data(InsertTextRole).toString();
    m.surrogates = index.data(SurrogatesRole).toString();
    m.customId = Core::Snowflake(index.data(CustomIdRole).toULongLong());
    m.imageUrl = index.data(ImageUrlRole).toUrl();
    m.displayLabel = index.data(LabelRole).toString();
    return m;
}

} // namespace

EmojiPicker::EmojiPicker(Core::ImageManager *images, QWidget *parent)
    : QFrame(parent, Qt::Popup), images(images)
{
    setObjectName(QStringLiteral("EmojiPicker"));
    resize(436, 460);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    search = new QLineEdit(this);
    search->setObjectName(QStringLiteral("EmojiPickerSearch"));
    search->setPlaceholderText(tr("Find the perfect emoji"));
    search->setClearButtonEnabled(true);
    search->installEventFilter(this);
    layout->addWidget(search);

    model = new QStandardItemModel(this);
    view = new QListView(this);
    view->setObjectName(QStringLiteral("EmojiPickerGrid"));
    view->setModel(model);
    view->setItemDelegate(new EmojiDelegate(view));
    view->setFlow(QListView::LeftToRight);
    view->setWrapping(true);
    view->setResizeMode(QListView::Adjust);
    view->setUniformItemSizes(false);
    view->setMouseTracking(true);
    view->viewport()->setAttribute(Qt::WA_Hover);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setFrameShape(QFrame::NoFrame);
    view->setCursor(Qt::PointingHandCursor);
    layout->addWidget(view, 1);

    preview = new QLabel(this);
    preview->setObjectName(QStringLiteral("EmojiPickerPreview"));
    preview->setMinimumHeight(24);
    layout->addWidget(preview);

    connect(search, &QLineEdit::textChanged, this, &EmojiPicker::rebuild);
    connect(view, &QListView::clicked, this, &EmojiPicker::pick);
    connect(view, &QListView::activated, this, &EmojiPicker::pick);
    connect(view, &QListView::entered, this, [this](const QModelIndex &index) {
        if (!index.data(IsHeaderRole).toBool())
            preview->setText(index.data(LabelRole).toString());
    });
    connect(images, &Core::ImageManager::imageFetched, this,
            [this](const QUrl &url, const QSize &, const QPixmap &pixmap) {
                const auto items = pendingImages.take(url);
                for (QStandardItem *item : items)
                    item->setData(pixmap, Qt::DecorationRole);
            });
}

QString EmojiPicker::reactionKey(const Core::EmojiMatch &match)
{
    if (!match.isCustom())
        return match.surrogates;
    // insertText is <:name:id> or <a:name:id>
    QString name = match.insertText;
    name.remove(0, name.startsWith(QLatin1String("<a:")) ? 3 : 2);
    name = name.section(QLatin1Char(':'), 0, 0);
    return name + QLatin1Char(':') + match.customId.toString();
}

void EmojiPicker::open(Core::ClientInstance *newInstance, Core::Snowflake newChannelId, bool reaction, QWidget *anchor)
{
    instance = newInstance;
    channelId = newChannelId;
    forReaction = reaction;
    search->clear();
    rebuild();

    QPoint pos = anchor->mapToGlobal(QPoint(anchor->width() - width(), -height() - 4));
    if (QScreen *screen = anchor->screen()) {
        const QRect avail = screen->availableGeometry();
        pos.setX(qBound(avail.left(), pos.x(), avail.right() - width()));
        if (pos.y() < avail.top())
            pos.setY(anchor->mapToGlobal(QPoint(0, anchor->height() + 4)).y());
        pos.setY(qBound(avail.top(), pos.y(), avail.bottom() - height()));
    }
    move(pos);
    show();
    raise();
    search->setFocus();
}

void EmojiPicker::moveNear(const QPoint &globalPos)
{
    QPoint pos = globalPos - QPoint(width() + 8, height() / 2);
    if (QScreen *screen = QGuiApplication::screenAt(globalPos)) {
        const QRect avail = screen->availableGeometry();
        pos.setX(qBound(avail.left(), pos.x(), avail.right() - width()));
        pos.setY(qBound(avail.top(), pos.y(), avail.bottom() - height()));
    }
    move(pos);
}

void EmojiPicker::rebuild()
{
    model->clear();
    pendingImages.clear();
    preview->clear();
    if (!instance)
        return;
    const QString query = search->text().trimmed();
    if (!query.isEmpty()) {
        const auto results = instance->emojis()->search(query, channelId, 120);
        addSection(results.isEmpty() ? tr("No emoji match") : tr("Search results"), results);
        if (!results.isEmpty())
            view->setCurrentIndex(model->index(1, 0));
        return;
    }
    for (const auto &section : instance->emojis()->pickerSections(channelId, forReaction))
        addSection(section.title, section.emojis);
    view->scrollToTop();
}

void EmojiPicker::addSection(const QString &title, const QList<Core::EmojiMatch> &emojis)
{
    auto *header = new QStandardItem(title);
    header->setData(true, IsHeaderRole);
    header->setFlags(Qt::ItemIsEnabled);
    model->appendRow(header);
    for (const auto &m : emojis) {
        auto *item = new QStandardItem;
        item->setData(m.insertText, InsertTextRole);
        item->setData(m.surrogates, SurrogatesRole);
        item->setData(quint64(m.customId), CustomIdRole);
        item->setData(m.imageUrl, ImageUrlRole);
        item->setData(m.displayLabel, LabelRole);
        item->setToolTip(m.displayLabel);
        if (m.isCustom() && instance) {
            const QPixmap pm = images->get(m.imageUrl, QSize(GlyphPx, GlyphPx), instance->accountId());
            if (!pm.isNull())
                item->setData(pm, Qt::DecorationRole);
            pendingImages[m.imageUrl].append(item);
        }
        model->appendRow(item);
    }
}

void EmojiPicker::pick(const QModelIndex &index)
{
    if (!index.isValid() || index.data(IsHeaderRole).toBool())
        return;
    const Core::EmojiMatch match = matchFromIndex(index);
    hide();
    emit emojiPicked(match);
}

bool EmojiPicker::eventFilter(QObject *watched, QEvent *event)
{
    // Enter in the search box picks the highlighted result, arrows move through the grid
    if (watched == search && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            QModelIndex current = view->currentIndex();
            if (!current.isValid() || current.data(IsHeaderRole).toBool())
                current = model->index(1, 0);
            pick(current);
            return true;
        }
        if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up || key->key() == Qt::Key_Left
            || key->key() == Qt::Key_Right) {
            QCoreApplication::sendEvent(view, event);
            return true;
        }
    }
    return QFrame::eventFilter(watched, event);
}

} // namespace UI
} // namespace Acheron
