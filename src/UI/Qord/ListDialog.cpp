#include "ListDialog.hpp"

#include "Core/ImageManager.hpp"
#include "Core/Theme/Icons.hpp"
#include "Core/Theme/Manager.hpp"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

namespace {
constexpr int AvatarSize = 40;
}

ListDialog::ListDialog(Core::ImageManager *images, Core::Snowflake accountId, Qord::Plugins::ListDialogEvents events,
                       QWidget *parent)
    : QDialog(parent), images(images), accountId(accountId), events(std::move(events))
{
    setAttribute(Qt::WA_DeleteOnClose);
    resize(560, 640);
    auto *v = new QVBoxLayout(this);
    notice = new QLabel(this);
    notice->setWordWrap(true);
    notice->setStyleSheet(QStringLiteral("background: rgba(240, 178, 50, 0.12); border: 1px solid #f0b232; "
                                         "border-radius: 6px; padding: 8px;"));
    v->addWidget(notice);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget(scroll);
    auto *contentLayout = new QVBoxLayout(content);
    list = new QVBoxLayout;
    list->setSpacing(10);
    contentLayout->addLayout(list);
    contentLayout->addStretch();
    scroll->setWidget(content);
    v->addWidget(scroll, 1);

    pager = new QWidget(this);
    auto *p = new QHBoxLayout(pager);
    p->setContentsMargins(0, 0, 0, 0);
    previous = new QPushButton(QStringLiteral("‹"), pager);
    next = new QPushButton(QStringLiteral("›"), pager);
    pageText = new QLabel(pager);
    p->addStretch();
    p->addWidget(previous);
    p->addWidget(pageText);
    p->addWidget(next);
    p->addStretch();
    connect(previous, &QPushButton::clicked, this, [this]() {
        if (this->events.page)
            this->events.page(-1);
    });
    connect(next, &QPushButton::clicked, this, [this]() {
        if (this->events.page)
            this->events.page(1);
    });
    v->addWidget(pager);

    input = new QLineEdit(this);
    input->installEventFilter(this);
    connect(input, &QLineEdit::returnPressed, this, [this]() {
        const QString text = input->text().trimmed();
        if (!inputEnabled || text.isEmpty() || !this->events.submitted)
            return;
        input->clear();
        this->events.submitted(text);
    });
    v->addWidget(input);
}

bool ListDialog::eventFilter(QObject *watched, QEvent *event)
{
    // A disabled input still reports clicks (ReviewDB opens its login)
    if (watched == input && !inputEnabled && event->type() == QEvent::MouseButtonPress) {
        if (events.inputBlocked)
            events.inputBlocked();
        return true;
    }
    return QDialog::eventFilter(watched, event);
}

void ListDialog::closeEvent(QCloseEvent *event)
{
    if (events.closed)
        events.closed();
    events = {};
    QDialog::closeEvent(event);
}

void ListDialog::setView(const Qord::Plugins::ListDialogView &view)
{
    setWindowTitle(view.title);
    notice->setText(view.notice);
    notice->setVisible(!view.notice.isEmpty());
    while (QLayoutItem *item = list->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    for (const auto &item : view.items)
        list->addWidget(itemWidget(item));
    if (view.items.isEmpty() && !view.emptyText.isEmpty()) {
        auto *empty = new QLabel(view.emptyText, this);
        empty->setWordWrap(true);
        empty->setAlignment(Qt::AlignCenter);
        empty->setForegroundRole(QPalette::PlaceholderText);
        list->addWidget(empty);
    }
    pager->setVisible(!view.pageText.isEmpty());
    pageText->setText(view.pageText);
    previous->setEnabled(view.canPrevious);
    next->setEnabled(view.canNext);
    input->setVisible(!view.inputPlaceholder.isEmpty());
    input->setPlaceholderText(view.inputPlaceholder);
    inputEnabled = view.inputEnabled;
    input->setReadOnly(!inputEnabled);
}

QWidget *ListDialog::itemWidget(const Qord::Plugins::ListDialogItem &item)
{
    auto *w = new QWidget(this);
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(10);
    auto *avatar = new QLabel(w);
    avatar->setFixedSize(AvatarSize, AvatarSize);
    if (!item.avatarUrl.isEmpty())
        images->assign(avatar, QUrl(item.avatarUrl), QSize(AvatarSize, AvatarSize), accountId);
    h->addWidget(avatar, 0, Qt::AlignTop);

    auto *text = new QVBoxLayout;
    text->setSpacing(2);
    auto *head = new QLabel(w);
    head->setTextFormat(Qt::RichText);
    const QString muted = palette().color(QPalette::PlaceholderText).name();
    QString headHtml = QStringLiteral("<b>%1</b>").arg(item.title.toHtmlEscaped());
    if (!item.badge.isEmpty())
        headHtml += QStringLiteral(" <span style=\"background:%1; color:white; font-size:10px;\">&nbsp;%2&nbsp;</span>")
                            .arg(Core::Theme::Manager::instance().color(Core::Theme::Token::Highlight).name(),
                                 item.badge.toHtmlEscaped());
    if (!item.meta.isEmpty())
        headHtml += QStringLiteral(" <span style=\"color:%1; font-size:11px;\">%2</span>").arg(muted, item.meta.toHtmlEscaped());
    head->setText(headHtml);
    text->addWidget(head);
    auto *body = new QLabel(item.body, w);
    body->setTextFormat(Qt::PlainText);
    body->setWordWrap(true);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    text->addWidget(body);
    h->addLayout(text, 1);

    if (!item.menu.isEmpty()) {
        auto *more = new QToolButton(w);
        more->setText(QStringLiteral("…"));
        more->setAutoRaise(true);
        more->setCursor(Qt::PointingHandCursor);
        connect(more, &QToolButton::clicked, this, [this, more, item]() {
            QMenu menu(this);
            for (const auto &entry : item.menu) {
                QAction *action = menu.addAction(entry.label);
                connect(action, &QAction::triggered, this, [this, itemId = item.id, menuId = entry.id]() {
                    if (events.menuAction)
                        events.menuAction(itemId, menuId);
                });
            }
            menu.exec(more->mapToGlobal(QPoint(0, more->height())));
        });
        h->addWidget(more, 0, Qt::AlignTop);
    }
    return w;
}

} // namespace UI
} // namespace Acheron
