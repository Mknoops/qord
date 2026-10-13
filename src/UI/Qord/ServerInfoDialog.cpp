#include "ServerInfoDialog.hpp"

#include "UI/Chat/ChatDelegate.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QTabWidget>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {
namespace ServerInfoDialog {

using Qord::Plugins::ServerInfoUser;
using Qord::Plugins::ServerInfoView;

namespace {
QPixmap round(const QPixmap &source, int size)
{
    QPixmap out(size, size);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addEllipse(QRectF(0, 0, size, size));
    p.setClipPath(clip);
    p.drawPixmap(QRect(0, 0, size, size), source);
    return out;
}

QPixmap acronymPixmap(const QString &text, int size, const QFont &base)
{
    QPixmap out(size, size);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x31, 0x33, 0x38));
    p.drawEllipse(QRectF(0, 0, size, size));
    QFont font = base;
    font.setPixelSize(qMax(8, size / 3));
    font.setBold(true);
    p.setFont(font);
    p.setPen(Qt::white);
    p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, text);
    return out;
}

// A label that loads its picture and can be clicked
QLabel *picture(QWidget *parent, const QString &url, int size, const QString &fallback, const Actions &actions,
                bool clickable)
{
    auto *label = new QLabel(parent);
    label->setFixedSize(size, size);
    label->setPixmap(acronymPixmap(fallback, size, parent->font()));
    if (!url.isEmpty() && actions.loadImage) {
        QPointer<QLabel> target = label;
        actions.loadImage(QUrl(url), label, [target, size](const QPixmap &pixmap) {
            if (target)
                target->setPixmap(round(pixmap, size));
        });
    }
    if (clickable && !url.isEmpty() && actions.openImage) {
        label->setCursor(Qt::PointingHandCursor);
        label->setProperty("qordImageUrl", url);
    }
    return label;
}

class ClickFilter : public QObject
{
public:
    ClickFilter(QObject *parent, std::function<void(QObject *)> onClick) : QObject(parent), onClick(std::move(onClick))
    {
    }
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            onClick(watched);
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void(QObject *)> onClick;
};

QWidget *userRow(QWidget *parent, const ServerInfoUser &user, const Actions &actions)
{
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->addWidget(picture(row, user.avatarUrl, 32, user.name.left(1), actions, false));
    auto *names = new QLabel(QStringLiteral("<b>%1</b><br><span style=\"color: gray;\">%2</span>")
                                     .arg(user.name.toHtmlEscaped(), user.username.toHtmlEscaped()),
                             row);
    layout->addWidget(names, 1);
    for (const auto &server : user.mutualServerIcons) {
        auto *icon = picture(row, server.iconUrl, 20, server.acronym, actions, false);
        icon->setToolTip(server.name);
        layout->addWidget(icon);
    }
    if (user.mutualServers > user.mutualServerIcons.size())
        layout->addWidget(new QLabel(QStringLiteral("+%1").arg(user.mutualServers - user.mutualServerIcons.size()), row));
    return row;
}
} // namespace

void show(QWidget *parent, const ServerInfoView &view, const Actions &actions)
{
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(view.name);
    dialog->resize(640, 620);
    auto *layout = new QVBoxLayout(dialog);
    auto *clicks = new ClickFilter(dialog, [actions](QObject *watched) {
        const QString url = watched->property("qordImageUrl").toString();
        if (!url.isEmpty() && actions.openImage)
            actions.openImage(QUrl(url));
    });

    // Header: icon, name and description
    auto *header = new QHBoxLayout;
    QLabel *icon = picture(dialog, view.iconUrl, 64, view.acronym, actions, true);
    icon->installEventFilter(clicks);
    header->addWidget(icon);
    auto *title = new QLabel(QStringLiteral("<span style=\"font-size: 18px; font-weight: 600;\">%1</span>%2")
                                     .arg(view.name.toHtmlEscaped(),
                                          view.description.isEmpty()
                                                  ? QString()
                                                  : QStringLiteral("<br>") + view.description.toHtmlEscaped()),
                             dialog);
    title->setWordWrap(true);
    header->addWidget(title, 1);
    layout->addLayout(header);

    // The banner, on the Server Info tab only
    QLabel *banner = nullptr;
    if (!view.bannerUrl.isEmpty()) {
        banner = new QLabel(dialog);
        banner->setFixedHeight(160);
        banner->setAlignment(Qt::AlignCenter);
        banner->setCursor(Qt::PointingHandCursor);
        banner->setProperty("qordImageUrl", view.bannerUrl);
        banner->installEventFilter(clicks);
        if (actions.loadImage) {
            QPointer<QLabel> target = banner;
            actions.loadImage(QUrl(view.bannerUrl), banner, [target](const QPixmap &pixmap) {
                if (target)
                    target->setPixmap(pixmap.scaledToHeight(160, Qt::SmoothTransformation));
            });
        }
        layout->addWidget(banner);
    }

    auto *tabs = new QTabWidget(dialog);
    auto *info = new QWidget(tabs);
    auto *form = new QFormLayout(info);
    for (const auto &field : view.fields) {
        auto *name = new QLabel(QStringLiteral("<b>%1</b>").arg(field.label.toHtmlEscaped()), info);
        QWidget *value;
        if (field.userId) {
            auto *box = new QWidget(info);
            auto *row = new QHBoxLayout(box);
            row->setContentsMargins(0, 0, 0, 0);
            QLabel *avatar = picture(box, field.avatarUrl, 20, field.text.mid(1, 1), actions, true);
            avatar->installEventFilter(clicks);
            row->addWidget(avatar);
            auto *link = new QLabel(QStringLiteral("<a href=\"#\">%1</a>").arg(field.text.toHtmlEscaped()), box);
            const Qord::Plugins::Id userId = field.userId;
            QObject::connect(link, &QLabel::linkActivated, dialog, [actions, userId]() {
                if (actions.openProfile)
                    actions.openProfile(userId);
            });
            row->addWidget(link);
            row->addStretch(1);
            value = box;
        } else {
            auto *label = new QLabel(field.time.isValid() ? ChatDelegate::headerTimestamp(field.time.toLocalTime())
                                                          : field.text,
                                     info);
            label->setTextInteractionFlags(Qt::TextSelectableByMouse);
            value = label;
        }
        form->addRow(name, value);
    }
    tabs->addTab(info, QObject::tr("Server Info"));

    for (const auto &tab : view.tabs) {
        auto *list = new QListWidget(tabs);
        for (const ServerInfoUser &user : tab.users) {
            auto *item = new QListWidgetItem(list);
            item->setData(Qt::UserRole, QVariant::fromValue<quint64>(user.userId));
            QWidget *row = userRow(list, user, actions);
            item->setSizeHint(row->sizeHint());
            list->setItemWidget(item, row);
        }
        QObject::connect(list, &QListWidget::itemClicked, dialog, [actions](QListWidgetItem *item) {
            if (actions.openProfile)
                actions.openProfile(item->data(Qt::UserRole).toULongLong());
        });
        tabs->addTab(list, tab.title);
    }
    if (banner)
        QObject::connect(tabs, &QTabWidget::currentChanged, banner, [banner](int index) { banner->setVisible(index == 0); });
    layout->addWidget(tabs, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);
    dialog->show();
}

} // namespace ServerInfoDialog
} // namespace UI
} // namespace Acheron
