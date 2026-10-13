#include "MessageListPopup.hpp"

#include <QApplication>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QTabBar>
#include <QToolButton>
#include <QScreen>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

namespace {
constexpr int RoleChannel = Qt::UserRole + 1;
constexpr int RoleMessage = Qt::UserRole + 2;

QString authorName(const Discord::Message &m)
{
    if (m.member.hasValue() && m.member->nick.hasValue() && !m.member->nick->isEmpty())
        return m.member->nick.get();
    const Discord::User &u = m.author.get();
    if (u.globalName.hasValue() && !u.globalName->isEmpty())
        return u.globalName.get();
    return u.username.get();
}
} // namespace

MessageListPopup::MessageListPopup(QWidget *parent) : QFrame(parent, Qt::Popup)
{
    setObjectName(QStringLiteral("MessageListPopup"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFrameShape(QFrame::StyledPanel);
    resize(440, 520);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("MessageListPopupHeader"));
    header->setAttribute(Qt::WA_StyledBackground, true);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 12, 16, 12);
    titleLabel = new QLabel(header);
    QFont tf = titleLabel->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() * 1.1);
    titleLabel->setFont(tf);
    headerLayout->addWidget(titleLabel, 1);
    searchField = new QLineEdit(header);
    searchField->setClearButtonEnabled(true);
    searchField->setMinimumWidth(200);
    searchField->hide();
    headerLayout->addWidget(searchField);
    clearButton = new QToolButton(header);
    clearButton->setText(tr("Clear All"));
    clearButton->setAutoRaise(true);
    clearButton->setCursor(Qt::PointingHandCursor);
    clearButton->hide();
    connect(clearButton, &QToolButton::clicked, this, [this]() {
        if (clearHandler)
            clearHandler();
    });
    headerLayout->addWidget(clearButton);
    header->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(header);

    tabs = new QTabBar(this);
    tabs->setObjectName(QStringLiteral("MessageListPopupTabs"));
    tabs->setDrawBase(false);
    tabs->setExpanding(false);
    tabs->hide();
    connect(tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (index >= 0 && tabs->isVisible())
            emit tabSelected(index);
    });
    layout->addWidget(tabs);

    statusLabel = new QLabel(this);
    statusLabel->setAlignment(Qt::AlignCenter);
    statusLabel->setWordWrap(true);
    statusLabel->setContentsMargins(16, 24, 16, 24);
    statusLabel->setObjectName(QStringLiteral("MessageListPopupStatus"));
    layout->addWidget(statusLabel);

    list = new QListWidget(this);
    list->setObjectName(QStringLiteral("MessageListPopupList"));
    list->setWordWrap(true);
    list->setSpacing(4);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setCursor(Qt::PointingHandCursor);
    layout->addWidget(list, 1);

    connect(list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        emit jumpRequested(Core::Snowflake(item->data(RoleChannel).toULongLong()),
                           Core::Snowflake(item->data(RoleMessage).toULongLong()));
        hide();
    });
    connect(list, &QListWidget::itemClicked, list, &QListWidget::itemActivated);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *item = list->itemAt(pos);
        if (!item || !removeHandler)
            return;
        QMenu menu(this);
        menu.addAction(tr("Remove"), this, [this, id = item->data(RoleMessage).toULongLong()]() {
            if (removeHandler)
                removeHandler(Core::Snowflake(id));
        });
        menu.exec(list->viewport()->mapToGlobal(pos));
    });
    connect(searchField, &QLineEdit::returnPressed, this, [this]() {
        if (searchHandler && !searchField->text().trimmed().isEmpty())
            searchHandler(searchField->text().trimmed());
    });
}

void MessageListPopup::setTitle(const QString &title)
{
    titleLabel->setText(title);
}

void MessageListPopup::setSearchHandler(const QString &placeholder, std::function<void(const QString &)> handler)
{
    searchHandler = std::move(handler);
    searchField->setPlaceholderText(placeholder);
    searchField->setVisible(static_cast<bool>(searchHandler));
}

void MessageListPopup::setSearchText(const QString &text)
{
    searchField->setText(text);
}

void MessageListPopup::setChannelNameResolver(std::function<QString(Core::Snowflake)> resolver)
{
    channelNameResolver = std::move(resolver);
}

static void setStatusFills(QLabel *label, bool fill)
{
    label->setSizePolicy(QSizePolicy::Preferred, fill ? QSizePolicy::Expanding : QSizePolicy::Fixed);
    label->setContentsMargins(16, fill ? 24 : 6, 16, fill ? 24 : 6);
}

void MessageListPopup::showLoading()
{
    setStatusFills(statusLabel, true);
    list->clear();
    list->hide();
    statusLabel->setText(tr("Loading…"));
    statusLabel->show();
}

void MessageListPopup::showError(const QString &error)
{
    setStatusFills(statusLabel, true);
    list->clear();
    list->hide();
    statusLabel->setText(error);
    statusLabel->show();
}

void MessageListPopup::setMessages(const QList<Discord::Message> &messages, const QString &emptyText,
                                   int totalResults)
{
    list->clear();
    setStatusFills(statusLabel, messages.isEmpty());
    if (messages.isEmpty()) {
        list->hide();
        statusLabel->setText(emptyText);
        statusLabel->show();
        return;
    }
    if (totalResults >= 0) {
        statusLabel->setText(tr("%n result(s)", nullptr, totalResults));
        statusLabel->show();
    } else {
        statusLabel->hide();
    }
    for (const auto &m : messages)
        addMessageItem(m);
    list->show();
}

void MessageListPopup::addMessageItem(const Discord::Message &m)
{
    QString text;
    if (channelNameResolver) {
        const QString channel = channelNameResolver(m.channelId.get());
        if (!channel.isEmpty())
            text += channel + QLatin1Char('\n');
    }
    const QDateTime when = m.timestamp.get().toLocalTime();
    text += authorName(m) + QStringLiteral("   ") + QLocale().toString(when, QLocale::ShortFormat);
    QString body = m.content.get();
    if (body.isEmpty() && m.attachments.hasValue() && !m.attachments->isEmpty())
        body = tr("[%n attachment(s)]", nullptr, int(m.attachments->size()));
    if (body.isEmpty() && m.embeds.hasValue() && !m.embeds->isEmpty())
        body = tr("[embed]");
    if (body.size() > 400)
        body = body.left(400) + QStringLiteral("…");
    text += QLatin1Char('\n') + body;

    auto *item = new QListWidgetItem(text, list);
    item->setData(RoleChannel, quint64(m.channelId.get()));
    item->setData(RoleMessage, quint64(m.id.get()));
    item->setToolTip(tr("Jump to message"));
}

void MessageListPopup::setTabs(const QStringList &labels, int current)
{
    const QSignalBlocker block(tabs);
    while (tabs->count() > 0)
        tabs->removeTab(0);
    for (const QString &label : labels)
        tabs->addTab(label);
    tabs->setVisible(!labels.isEmpty());
    if (!labels.isEmpty())
        tabs->setCurrentIndex(qBound(0, current, int(labels.size()) - 1));
}

void MessageListPopup::setClearHandler(std::function<void()> handler)
{
    clearHandler = std::move(handler);
    clearButton->setVisible(static_cast<bool>(clearHandler));
}

void MessageListPopup::setRemoveHandler(std::function<void(Core::Snowflake)> handler)
{
    removeHandler = std::move(handler);
}

void MessageListPopup::popup(QWidget *anchor)
{
    QPoint pos = anchor->mapToGlobal(QPoint(anchor->width() - width(), anchor->height() + 4));
    if (QScreen *screen = anchor->screen()) {
        const QRect avail = screen->availableGeometry();
        pos.setX(qBound(avail.left(), pos.x(), avail.right() - width()));
        pos.setY(qBound(avail.top(), pos.y(), avail.bottom() - height()));
    }
    move(pos);
    show();
    raise();
    if (searchField->isVisible())
        searchField->setFocus();
}

} // namespace UI
} // namespace Acheron
