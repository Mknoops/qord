#include "HiddenChannelScreen.hpp"

#include "Core/Theme/Icons.hpp"
#include "Core/Theme/Manager.hpp"
#include "UI/Chat/ChatDelegate.hpp"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QToolButton>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

using Qord::Plugins::LockScreen;
using Qord::Plugins::PermissionsEntry;

namespace {
constexpr int ContentWidth = 640;
const QColor DefaultRoleColor(0xb5, 0xba, 0xc1);

QLabel *text(const QString &value, int pixelSize, bool bold, QWidget *parent)
{
    auto *label = new QLabel(value, parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont font = label->font();
    font.setPixelSize(pixelSize);
    font.setBold(bold);
    label->setFont(font);
    return label;
}

QPixmap chipIcon(const PermissionsEntry &entry, const QPixmap &avatar)
{
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pixmap(QSize(20, 20) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (entry.kind == PermissionsEntry::Kind::Member) {
        QPainterPath clip;
        clip.addEllipse(QRectF(0, 0, 20, 20));
        p.setClipPath(clip);
        if (!avatar.isNull())
            p.drawPixmap(QRectF(0, 0, 20, 20), avatar, avatar.rect());
        else
            p.fillPath(clip, Core::Theme::Manager::instance().color(Core::Theme::Token::Highlight));
    } else {
        p.setPen(Qt::NoPen);
        p.setBrush(entry.color ? QColor::fromRgb(entry.color) : DefaultRoleColor);
        p.drawEllipse(QPointF(10, 10), 5, 5);
    }
    return pixmap;
}
} // namespace

HiddenChannelScreen::HiddenChannelScreen(QWidget *parent) : QScrollArea(parent)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
}

QWidget *HiddenChannelScreen::buildAllowed(const LockScreen &screen, QWidget *parent)
{
    auto *box = new QWidget(parent);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 8, 0, 0);
    auto *header = new QHBoxLayout;
    const QColor color = palette().color(QPalette::Text);
    const qreal dpr = devicePixelRatioF();
    if (screen.showPermissionDetails) {
        auto *details = new QToolButton(box);
        details->setAutoRaise(true);
        details->setIcon(QIcon(Core::Theme::Icons::pixmap(Core::Theme::Icons::Name::Ellipsis, 20, color, dpr)));
        details->setToolTip(tr("Permission Details"));
        connect(details, &QToolButton::clicked, this, [f = screen.showPermissionDetails]() { f(); });
        header->addWidget(details);
    }
    auto *title = text(tr("Allowed users and roles:"), 16, true, box);
    title->setWordWrap(false);
    header->addWidget(title);
    auto *toggle = new QToolButton(box);
    toggle->setAutoRaise(true);
    QPixmap chevron = Core::Theme::Icons::pixmap(Core::Theme::Icons::Name::ChevronDown, 20, color, dpr);
    if (screen.allowedExpanded)
        chevron = chevron.transformed(QTransform().scale(1, -1));
    toggle->setIcon(QIcon(chevron));
    toggle->setToolTip(screen.allowedExpanded ? tr("Hide Allowed Users and Roles") : tr("View Allowed Users and Roles"));
    if (screen.toggleAllowed)
        connect(toggle, &QToolButton::clicked, this, [f = screen.toggleAllowed]() { f(); });
    header->addWidget(toggle);
    header->addStretch(1);
    layout->addLayout(header);

    if (!screen.allowedExpanded)
        return box;
    auto *chips = new QListWidget(box);
    chips->setFlow(QListView::LeftToRight);
    chips->setWrapping(true);
    chips->setResizeMode(QListView::Adjust);
    chips->setSpacing(3);
    chips->setFrameShape(QFrame::NoFrame);
    chips->setSelectionMode(QAbstractItemView::NoSelection);
    chips->setIconSize(QSize(20, 20));
    chips->setStyleSheet(QStringLiteral("QListWidget { background: transparent; }"
                                        "QListWidget::item { background: rgba(128, 128, 128, 0.16); border-radius: 10px;"
                                        " padding: 2px 8px 2px 2px; }"));
    for (const PermissionsEntry &entry : screen.allowed) {
        const bool role = entry.kind != PermissionsEntry::Kind::Member;
        auto *item = new QListWidgetItem(QIcon(chipIcon(entry, {})), role && !entry.name.startsWith(QLatin1Char('@')) ? QLatin1Char('@') + entry.name : entry.name, chips);
        item->setFlags(Qt::ItemIsEnabled);
        if (role && entry.color)
            item->setForeground(QColor::fromRgb(entry.color));
        const QUrl url(entry.imageUrl);
        if (!role && loadImage && url.isValid() && !url.isEmpty()) {
            QPointer<QListWidget> list = chips;
            const int row = chips->count() - 1;
            loadImage(url, chips, [list, row, entry](const QPixmap &avatar) {
                if (list && list->item(row))
                    list->item(row)->setIcon(QIcon(chipIcon(entry, avatar)));
            });
        }
    }
    chips->setFixedWidth(ContentWidth);
    chips->ensurePolished();
    chips->resize(ContentWidth, 1000);
    chips->doItemsLayout();
    int bottom = 0;
    for (int i = 0; i < chips->count(); ++i)
        bottom = qMax(bottom, chips->visualItemRect(chips->item(i)).bottom());
    chips->setFixedHeight(bottom + 8);
    layout->addWidget(chips);
    return box;
}

void HiddenChannelScreen::showScreen(const LockScreen &screen)
{
    auto *page = new QWidget(this);
    auto *outer = new QHBoxLayout(page);
    outer->addStretch(1);
    auto *column = new QWidget(page);
    column->setFixedWidth(ContentWidth);
    outer->addWidget(column);
    outer->addStretch(1);
    auto *layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 48, 0, 48);
    layout->setSpacing(8);

    const QColor muted = palette().color(QPalette::PlaceholderText);
    auto *logo = new QLabel(column);
    logo->setPixmap(Core::Theme::Icons::pixmap(Core::Theme::Icons::Name::Lock, 96, muted, devicePixelRatioF()));
    logo->setAlignment(Qt::AlignCenter);
    layout->addWidget(logo);

    auto *heading = new QHBoxLayout;
    heading->addStretch(1);
    auto *title = text(screen.heading, 24, true, column);
    title->setWordWrap(false);
    heading->addWidget(title);
    if (screen.nsfw) {
        auto *nsfw = new QLabel(column);
        nsfw->setPixmap(Core::Theme::Icons::pixmap(Core::Theme::Icons::Name::TriangleAlert, 28, QColor(0xf2, 0x3f, 0x43),
                                                   devicePixelRatioF()));
        nsfw->setToolTip(tr("NSFW"));
        heading->addWidget(nsfw);
    }
    heading->addStretch(1);
    layout->addLayout(heading);

    if (!screen.subheading.isEmpty()) {
        auto *sub = text(screen.subheading, 16, false, column);
        sub->setAlignment(Qt::AlignCenter);
        layout->addWidget(sub);
    }
    if (!screen.topic.isEmpty()) {
        auto *topic = text(screen.topic, 14, false, column);
        topic->setTextFormat(Qt::PlainText);
        topic->setStyleSheet(QStringLiteral("QLabel { background: rgba(128, 128, 128, 0.12); border-radius: 6px; padding: 10px; }"));
        layout->addWidget(topic);
    }
    for (const auto &detail : screen.details) {
        QString line = detail.text;
        if (detail.time.isValid())
            line += QLatin1Char(' ') + ChatDelegate::headerTimestamp(detail.time.toLocalTime());
        auto *label = text(line, 14, false, column);
        label->setAlignment(Qt::AlignCenter);
        layout->addWidget(label);
    }
    if (!screen.tags.isEmpty()) {
        layout->addWidget(text(tr("Available tags:"), 16, true, column));
        QStringList chips;
        for (const QString &tag : screen.tags)
            chips.append(QStringLiteral("<span style=\"background: rgba(128,128,128,0.2);\">&nbsp;%1&nbsp;</span>")
                                 .arg(tag.toHtmlEscaped()));
        auto *tags = new QLabel(chips.join(QStringLiteral("&nbsp; ")), column);
        tags->setTextFormat(Qt::RichText);
        tags->setWordWrap(true);
        layout->addWidget(tags);
    }
    layout->addWidget(buildAllowed(screen, column));
    layout->addStretch(1);

    setWidget(page); // deletes the previous page
}

} // namespace UI
} // namespace Acheron
