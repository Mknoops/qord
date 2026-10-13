#include "PermissionsView.hpp"

#include "UI/GuildSettings/GuildRolesPage.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {
namespace PermissionsView {

using Qord::Plugins::GrantedPermission;
using Qord::Plugins::MemberPermissionsView;
using Qord::Plugins::PermissionsEntry;

namespace {
constexpr QSize EntryIconSize(40, 24);
const QColor DefaultRoleColor(0xb5, 0xba, 0xc1); // Discord's --primary-300
const QColor Allowed(0x23, 0xa5, 0x5a);
const QColor Denied(0xf2, 0x3f, 0x43);

QColor roleColor(quint32 color)
{
    return color ? QColor::fromRgb(color) : DefaultRoleColor;
}

QPixmap blank(const QSize &size)
{
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pixmap(size * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    return pixmap;
}

void drawDot(QPainter &p, const QPointF &center, qreal radius, const QColor &color)
{
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(center, radius, radius);
}

// A role's colour dot followed by its icon; a member's round avatar
QPixmap entryIcon(const PermissionsEntry &entry, const QPixmap &image)
{
    QPixmap pixmap = blank(EntryIconSize);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (entry.kind == PermissionsEntry::Kind::Member) {
        const QRectF avatar(0, 0, 24, 24);
        if (!image.isNull()) {
            QPainterPath clip;
            clip.addEllipse(avatar);
            p.setClipPath(clip);
            p.drawPixmap(avatar, image, image.rect());
        } else {
            drawDot(p, avatar.center(), 12, QColor(0x5865f2));
        }
        return pixmap;
    }
    drawDot(p, QPointF(8, 12), 6, roleColor(entry.color));
    const QRectF icon(18, 2, 20, 20);
    if (!image.isNull()) {
        p.drawPixmap(icon, image, image.rect());
    } else if (!entry.emoji.isEmpty()) {
        QFont font = p.font();
        font.setPixelSize(15);
        p.setFont(font);
        p.drawText(icon, Qt::AlignCenter, entry.emoji);
    }
    return pixmap;
}

QPixmap stateIcon(bool allowed)
{
    QPixmap pixmap = blank(QSize(20, 20));
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    drawDot(p, QPointF(10, 10), 9, allowed ? Allowed : Denied);
    p.setPen(QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (allowed) {
        p.drawLine(QPointF(6, 10), QPointF(9, 13));
        p.drawLine(QPointF(9, 13), QPointF(14, 7));
    } else {
        p.drawLine(QPointF(7, 7), QPointF(13, 13));
        p.drawLine(QPointF(13, 7), QPointF(7, 13));
    }
    return pixmap;
}

QString entryName(const PermissionsEntry &entry)
{
    if (entry.kind == PermissionsEntry::Kind::Owner)
        return QStringLiteral("@owner 👑");
    return entry.name;
}

void fillPermissions(QListWidget *list, const PermissionsEntry &entry)
{
    list->clear();
    for (const PermissionText &text : permissionTexts()) {
        bool allowed;
        if (entry.permissions)
            allowed = (*entry.permissions & text.bit) == text.bit;
        else if (entry.allow & text.bit)
            allowed = true;
        else if (entry.deny & text.bit)
            allowed = false;
        else
            continue; // not set by this overwrite
        auto *item = new QListWidgetItem(QIcon(stateIcon(allowed)), text.title, list);
        item->setToolTip(text.description);
        item->setFlags(Qt::ItemIsEnabled);
    }
}

QPixmap arrowIcon(bool down, const QColor &color)
{
    QPixmap pixmap = blank(QSize(18, 18));
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    if (down) {
        p.translate(0, 18);
        p.scale(1, -1);
    }
    QPainterPath arrow; // Material's arrow_upward, as upstream
    arrow.moveTo(8.25, 15.75);
    arrow.lineTo(8.25, 6.6);
    arrow.lineTo(4.05, 10.8);
    arrow.lineTo(3, 9.75);
    arrow.lineTo(9, 3.75);
    arrow.lineTo(15, 9.75);
    arrow.lineTo(13.95, 10.8);
    arrow.lineTo(9.75, 6.6);
    arrow.lineTo(9.75, 15.75);
    arrow.closeSubpath();
    p.fillPath(arrow, color);
    return pixmap;
}

QString permissionTitle(quint64 bit)
{
    if (bit == 0)
        return QCoreApplication::translate("PermissionsViewer", "Server Owner");
    for (const PermissionText &text : permissionTexts())
        if (text.bit == bit)
            return text.title;
    return {};
}
} // namespace

void showDialog(const Qord::Plugins::PermissionsView &view, const ImageLoader &loadImage)
{
    auto *dialog = new QDialog(QApplication::activeWindow());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(view.title);
    dialog->resize(820, 560);
    auto *layout = new QVBoxLayout(dialog);

    if (view.entries.isEmpty()) {
        auto *empty = new QLabel(QCoreApplication::translate("PermissionsViewer", "No permissions to display!"), dialog);
        empty->setAlignment(Qt::AlignCenter);
        QFont font = empty->font();
        font.setPixelSize(20);
        empty->setFont(font);
        layout->addWidget(empty, 1);
    } else {
        auto *body = new QHBoxLayout;
        auto *entries = new QListWidget(dialog);
        entries->setIconSize(EntryIconSize);
        entries->setFixedWidth(280);
        entries->setContextMenuPolicy(Qt::CustomContextMenu);
        auto *divider = new QFrame(dialog);
        divider->setFrameShape(QFrame::VLine);
        auto *permissions = new QListWidget(dialog);
        permissions->setIconSize(QSize(20, 20));
        permissions->setSelectionMode(QAbstractItemView::NoSelection);
        body->addWidget(entries);
        body->addWidget(divider);
        body->addWidget(permissions, 1);
        layout->addLayout(body, 1);

        for (const PermissionsEntry &entry : view.entries) {
            auto *item = new QListWidgetItem(QIcon(entryIcon(entry, {})), entryName(entry), entries);
            const QUrl url(entry.imageUrl);
            if (url.isValid() && !url.isEmpty() && loadImage) {
                QPointer<QListWidget> list = entries;
                const int row = entries->count() - 1;
                loadImage(url, entries, [list, row, entry](const QPixmap &image) {
                    if (list && list->item(row))
                        list->item(row)->setIcon(QIcon(entryIcon(entry, image)));
                });
            }
            Q_UNUSED(item);
        }
        const QList<PermissionsEntry> all = view.entries;
        QObject::connect(entries, &QListWidget::currentRowChanged, permissions, [permissions, all](int row) {
            if (row >= 0 && row < all.size())
                fillPermissions(permissions, all[row]);
        });
        QObject::connect(entries, &QListWidget::customContextMenuRequested, entries, [entries, all](const QPoint &pos) {
            const int row = entries->indexAt(pos).row();
            if (row < 0 || row >= all.size() || all[row].kind == PermissionsEntry::Kind::Owner)
                return;
            const PermissionsEntry &entry = all[row];
            QMenu menu(entries);
            const QString label = entry.kind == PermissionsEntry::Kind::Role
                                          ? QCoreApplication::translate("PermissionsViewer", "Copy Role ID")
                                          : QCoreApplication::translate("PermissionsViewer", "Copy User ID");
            const QString id = QString::number(entry.id);
            QObject::connect(menu.addAction(label), &QAction::triggered, entries,
                             [id]() { QGuiApplication::clipboard()->setText(id); });
            menu.exec(entries->viewport()->mapToGlobal(pos));
        });
        entries->setCurrentRow(0);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);
    dialog->show();
}

void showMemberPopout(const MemberPermissionsView &view, const QPoint &globalPos)
{
    // The sort toggle shows the popout again; keep it where it was
    static QPointer<QFrame> current;
    QPoint pos = globalPos;
    if (current) {
        pos = current->pos();
        current->close();
    }

    auto *popup = new QFrame(QApplication::activeWindow(), Qt::Popup);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setFrameShape(QFrame::StyledPanel);
    popup->setFixedWidth(500);
    auto *layout = new QVBoxLayout(popup);
    layout->setContentsMargins(12, 10, 12, 12);

    auto *header = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("<b>%1</b>").arg(QCoreApplication::translate("PermissionsViewer", "Permissions")),
                             popup);
    header->addWidget(title, 1);
    const QColor text = popup->palette().color(QPalette::Text);
    auto *sort = new QToolButton(popup);
    sort->setAutoRaise(true);
    sort->setIcon(QIcon(arrowIcon(view.lowestFirst, text)));
    sort->setToolTip(view.lowestFirst ? QCoreApplication::translate("PermissionsViewer", "Sorting by Lowest Role")
                                      : QCoreApplication::translate("PermissionsViewer", "Sorting by Highest Role"));
    auto *details = new QToolButton(popup);
    details->setAutoRaise(true);
    details->setText(QStringLiteral("⋯"));
    details->setToolTip(QCoreApplication::translate("PermissionsViewer", "Role Details"));
    header->addWidget(sort);
    header->addWidget(details);
    layout->addLayout(header);

    auto *chips = new QListWidget(popup);
    chips->setFlow(QListView::LeftToRight);
    chips->setWrapping(true);
    chips->setResizeMode(QListView::Adjust);
    chips->setSpacing(3);
    chips->setFrameShape(QFrame::NoFrame);
    chips->setSelectionMode(QAbstractItemView::NoSelection);
    chips->setIconSize(QSize(12, 12));
    // Pills, like the roles in a profile
    chips->setStyleSheet(QStringLiteral("QListWidget { background: transparent; }"
                                        "QListWidget::item { background: rgba(128, 128, 128, 0.16); border-radius: 4px;"
                                        " padding: 2px 6px 2px 4px; }"));
    for (const GrantedPermission &permission : view.permissions) {
        const QString name = permissionTitle(permission.bit);
        if (name.isEmpty())
            continue;
        QPixmap dot = blank(QSize(12, 12));
        {
            QPainter p(&dot);
            p.setRenderHint(QPainter::Antialiasing);
            drawDot(p, QPointF(6, 6), 5, roleColor(permission.roleColor));
        }
        auto *item = new QListWidgetItem(QIcon(dot), name, chips);
        item->setFlags(Qt::ItemIsEnabled);
        item->setToolTip(QStringLiteral("%1<br><span style=\"color:%2;\">●</span> %3")
                                 .arg(QCoreApplication::translate("PermissionsViewer", "Granted By"),
                                      roleColor(permission.roleColor).name(), permission.roleName.toHtmlEscaped()));
    }
    layout->addWidget(chips);
    // As tall as the chips need, scrolling past 300 px
    popup->ensurePolished();
    chips->ensurePolished();
    chips->resize(popup->width() - layout->contentsMargins().left() - layout->contentsMargins().right(), 1000);
    chips->doItemsLayout();
    int bottom = 0;
    for (int i = 0; i < chips->count(); ++i)
        bottom = qMax(bottom, chips->visualItemRect(chips->item(i)).bottom());
    chips->setFixedHeight(qMin(bottom + 8, 300));

    const auto toggle = view.toggleSort;
    QObject::connect(sort, &QToolButton::clicked, popup, [toggle]() {
        if (toggle)
            QTimer::singleShot(0, toggle);
    });
    const auto showDetails = view.showRoleDetails;
    QObject::connect(details, &QToolButton::clicked, popup, [popup, showDetails]() {
        popup->close();
        if (showDetails)
            QTimer::singleShot(0, showDetails);
    });

    popup->adjustSize();
    if (QScreen *screen = QGuiApplication::screenAt(pos)) {
        const QRect area = screen->availableGeometry();
        pos.setX(qBound(area.left(), pos.x(), area.right() - popup->width()));
        pos.setY(qBound(area.top(), pos.y(), area.bottom() - popup->height()));
    }
    popup->move(pos);
    current = popup;
    popup->show();
}

} // namespace PermissionsView
} // namespace UI
} // namespace Acheron
