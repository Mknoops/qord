#include "FriendsPage.hpp"

#include "Core/ClientInstance.hpp"
#include "Core/ImageManager.hpp"
#include "Core/Presence/PresenceManager.hpp"
#include "Core/RelationshipManager.hpp"
#include "Core/Theme/Icons.hpp"
#include "Core/Theme/Manager.hpp"
#include "Core/UserManager.hpp"
#include "UI/StatusIndicator.hpp"
#include "Plugins/PluginHost.hpp"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

using Core::Snowflake;
using Discord::RelationshipType;

namespace {

constexpr int AvatarSize = 32;

QString statusText(const Core::PresenceBadge &badge)
{
    switch (badge.status) {
    case Discord::StatusType::ONLINE:
        return FriendsPage::tr("Online");
    case Discord::StatusType::IDLE:
        return FriendsPage::tr("Idle");
    case Discord::StatusType::DND:
        return FriendsPage::tr("Do Not Disturb");
    default:
        return FriendsPage::tr("Offline");
    }
}

QPixmap roundAvatar(const QPixmap &src, const Core::PresenceBadge *badge, const QColor &ring)
{
    QPixmap out(AvatarSize + 2, AvatarSize + 2);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRect r(0, 0, AvatarSize, AvatarSize);
    if (!src.isNull()) {
        QPainterPath clip;
        clip.addEllipse(QRectF(r));
        p.setClipPath(clip);
        p.drawPixmap(r, src);
        p.setClipping(false);
    }
    if (badge)
        StatusIndicator::paintOnAvatar(p, r, 10, *badge, ring);
    return out;
}

QPushButton *tabButton(const QString &text, QWidget *parent)
{
    auto *b = new QPushButton(text, parent);
    b->setCheckable(true);
    b->setCursor(Qt::PointingHandCursor);
    b->setObjectName(QStringLiteral("FriendsTab"));
    return b;
}

// Discord's relationship type for implicit relationships
constexpr int ImplicitType = 5;
// Button ids of plugin tabs start here
constexpr int PluginTabBase = 100;
} // namespace

FriendsPage::FriendsPage(Core::ImageManager *images, QWidget *parent) : QWidget(parent), images(images)
{
    setObjectName(QStringLiteral("FriendsPage"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Header: "Friends | Online All Pending Blocked [Add Friend]"
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("FriendsHeader"));
    header->setAttribute(Qt::WA_StyledBackground, true);
    header->setFixedHeight(48);
    headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 0, 16, 0);
    headerLayout->setSpacing(8);
    auto *icon = new QLabel(header);
    icon->setPixmap(Core::Theme::Icons::pixmap(Core::Theme::Icons::Name::Users, 22, Core::Theme::Token::MutedText,
                                               devicePixelRatioF()));
    headerLayout->addWidget(icon);
    auto *title = new QLabel(tr("Friends"), header);
    title->setObjectName(QStringLiteral("ChannelHeaderName"));
    headerLayout->addWidget(title);
    auto *divider = new QFrame(header);
    divider->setObjectName(QStringLiteral("ChannelHeaderDivider"));
    divider->setFixedSize(1, 24);
    headerLayout->addWidget(divider);

    tabs = new QButtonGroup(this);
    tabs->setExclusive(true);
    const auto addTab = [&](const QString &text, Tab tab) {
        QPushButton *b = tabButton(text, header);
        tabs->addButton(b, int(tab));
        headerLayout->addWidget(b);
        return b;
    };
    addTab(tr("Online"), Tab::Online)->setChecked(true);
    addTab(tr("All"), Tab::All);
    pendingTab = addTab(tr("Pending"), Tab::Pending);
    addTab(tr("Blocked"), Tab::Blocked);
    QPushButton *addButton = addTab(tr("Add Friend"), Tab::Add);
    addButton->setObjectName(QStringLiteral("AddFriendTab"));
    addFriendTab = addButton;
    headerLayout->addStretch(1);
    layout->addWidget(header);
    connect(tabs, &QButtonGroup::idClicked, this, [this](int id) {
        if (id >= PluginTabBase) {
            currentSection = pluginTabs.value(id - PluginTabBase)->property("qordSection").toString();
            showTab(Tab::Plugin);
        } else {
            showTab(Tab(id));
        }
    });
    if (auto *host = Qord::Plugins::PluginHost::instance())
        connect(host, &Qord::Plugins::PluginHost::friendsChanged, this, [this]() {
            rebuildPluginTabs();
            refresh();
        });

    stack = new QStackedWidget(this);
    layout->addWidget(stack, 1);

    // List of people
    auto *listPage = new QWidget(stack);
    auto *listLayout = new QVBoxLayout(listPage);
    listLayout->setContentsMargins(20, 16, 20, 0);
    listLayout->setSpacing(8);
    countLabel = new QLabel(listPage);
    countLabel->setObjectName(QStringLiteral("FriendsCount"));
    listLayout->addWidget(countLabel);
    list = new QListWidget(listPage);
    list->setObjectName(QStringLiteral("FriendsList"));
    list->setFrameShape(QFrame::NoFrame);
    list->setSelectionMode(QAbstractItemView::NoSelection);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    listLayout->addWidget(list, 1);
    stack->addWidget(listPage);

    // Add Friend
    auto *addPage = new QWidget(stack);
    auto *addLayout = new QVBoxLayout(addPage);
    addLayout->setContentsMargins(30, 20, 30, 20);
    addLayout->setSpacing(8);
    auto *addTitle = new QLabel(tr("ADD FRIEND"), addPage);
    addTitle->setObjectName(QStringLiteral("ChannelHeaderName"));
    addLayout->addWidget(addTitle);
    auto *addHint = new QLabel(tr("You can add friends with their Discord username."), addPage);
    addHint->setObjectName(QStringLiteral("FriendsCount"));
    addLayout->addWidget(addHint);
    auto *addRow = new QWidget(addPage);
    addRow->setObjectName(QStringLiteral("AddFriendBox"));
    addRow->setAttribute(Qt::WA_StyledBackground, true);
    auto *addRowLayout = new QHBoxLayout(addRow);
    addRowLayout->setContentsMargins(12, 6, 6, 6);
    addField = new QLineEdit(addRow);
    addField->setObjectName(QStringLiteral("AddFriendField"));
    addField->setPlaceholderText(tr("You can add friends with their Discord username."));
    addRowLayout->addWidget(addField, 1);
    auto *send = new QPushButton(tr("Send Friend Request"), addRow);
    send->setObjectName(QStringLiteral("PrimaryButton"));
    send->setCursor(Qt::PointingHandCursor);
    send->setEnabled(false);
    addRowLayout->addWidget(send);
    addLayout->addWidget(addRow);
    addStatus = new QLabel(addPage);
    addStatus->setWordWrap(true);
    addLayout->addWidget(addStatus);
    addLayout->addStretch(1);
    stack->addWidget(addPage);

    connect(addField, &QLineEdit::textChanged, this, [send](const QString &t) { send->setEnabled(!t.trimmed().isEmpty()); });
    const auto sendRequest = [this]() {
        const QString name = addField->text().trimmed();
        if (name.isEmpty() || !instance)
            return;
        addStatus->setText(tr("Sending…"));
        const QPointer<FriendsPage> guard(this);
        instance->discord()->sendFriendRequest(name, [guard, name](const Core::Result<void> &result) {
            if (!guard)
                return;
            if (result.success()) {
                guard->addStatus->setStyleSheet(QStringLiteral("color: #23a55a;"));
                guard->addStatus->setText(tr("Success! Your friend request to %1 was sent.").arg(name));
                guard->addField->clear();
            } else {
                guard->addStatus->setStyleSheet(QStringLiteral("color: #f23f43;"));
                guard->addStatus->setText(tr("Hm, didn't work. Double check that the username is correct. (%1)")
                                                  .arg(result.error));
            }
        });
    };
    connect(send, &QPushButton::clicked, this, sendRequest);
    connect(addField, &QLineEdit::returnPressed, this, sendRequest);

    connect(images, &Core::ImageManager::imageFetched, this, [this](const QUrl &, const QSize &, const QPixmap &) {
        if (isVisible() && current != Tab::Add)
            refresh();
    });
}

void FriendsPage::setInstance(Core::ClientInstance *newInstance)
{
    for (const auto &c : connections)
        disconnect(c);
    connections.clear();
    if (instance != newInstance)
        requestedUsers.clear();
    instance = newInstance;
    rebuildPluginTabs();
    if (instance) {
        // Users a plugin tab asked for arrive with member chunks
        connections.append(connect(instance, &Core::ClientInstance::membersUpdated, this,
                                   [this](Snowflake, const QList<Snowflake> &) {
                                       if (isVisible() && current == Tab::Plugin)
                                           refresh();
                                   }));
        connections.append(connect(instance->relationships(), &Core::RelationshipManager::relationshipChanged, this,
                                   [this](Snowflake) { refresh(); }));
        connections.append(connect(instance->presences(), &Core::PresenceManager::presencesChanged, this,
                                   [this](const QList<Snowflake> &) {
                                       if (isVisible() && current == Tab::Online)
                                           refresh();
                                   }));
    }
    refresh();
    if (isVisible())
        pagesOpened();
}

void FriendsPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    pagesOpened();
}

void FriendsPage::pagesOpened()
{
    // Where Discord fetches relationships (ImplicitRelationships piggybacks)
    auto *host = Qord::Plugins::PluginHost::instance();
    if (instance && host && host->hasHook(Qord::Plugins::Hook::FriendsSections))
        host->dispatchFriendsPageOpened({ quint64(instance->accountId()) });
}

void FriendsPage::rebuildPluginTabs()
{
    for (QPushButton *b : std::as_const(pluginTabs)) {
        tabs->removeButton(b);
        b->deleteLater();
    }
    pluginTabs.clear();
    auto *host = Qord::Plugins::PluginHost::instance();
    QList<Qord::Plugins::FriendsSection> sections;
    if (instance && host && host->hasHook(Qord::Plugins::Hook::FriendsSections))
        sections = host->dispatchFriendsSections({ quint64(instance->accountId()) });
    const int at = headerLayout->indexOf(addFriendTab);
    bool currentFound = false;
    for (int i = 0; i < sections.size(); ++i) {
        QPushButton *b = tabButton(sections[i].label, headerLayout->parentWidget());
        b->setProperty("qordSection", sections[i].id);
        tabs->addButton(b, PluginTabBase + i);
        headerLayout->insertWidget(at + i, b);
        pluginTabs.append(b);
        if (current == Tab::Plugin && sections[i].id == currentSection) {
            b->setChecked(true);
            currentFound = true;
        }
    }
    // The tab went away with its plugin
    if (current == Tab::Plugin && !currentFound) {
        current = Tab::Online;
        if (QAbstractButton *b = tabs->button(int(Tab::Online)))
            b->setChecked(true);
    }
}

void FriendsPage::showTab(Tab tab)
{
    current = tab;
    if (tab != Tab::Plugin) {
        if (QAbstractButton *b = tabs->button(int(tab)))
            b->setChecked(true);
    }
    stack->setCurrentIndex(tab == Tab::Add ? 1 : 0);
    if (tab == Tab::Add)
        addField->setFocus();
    refresh();
}

void FriendsPage::refresh()
{
    list->clear();
    if (!instance)
        return;

    int pending = 0;
    QList<QPair<QString, QPair<Snowflake, int>>> rows;
    for (const auto &rel : instance->relationships()->all()) {
        const Snowflake userId = rel.id.get();
        const int type = int(rel.type.get());
        if (type == int(RelationshipType::INCOMING_REQUEST))
            ++pending;
        bool include = false;
        switch (current) {
        case Tab::Online:
            include = type == int(RelationshipType::FRIEND)
                      && instance->presences()->badge(userId).status != Discord::StatusType::OFFLINE;
            break;
        case Tab::All:
            include = type == int(RelationshipType::FRIEND);
            break;
        case Tab::Pending:
            include = type == int(RelationshipType::INCOMING_REQUEST) || type == int(RelationshipType::OUTGOING_REQUEST);
            break;
        case Tab::Blocked:
            include = type == int(RelationshipType::BLOCKED);
            break;
        case Tab::Add:
        case Tab::Plugin:
            break;
        }
        if (include)
            rows.append({ instance->users()->getDisplayName(userId).toLower(), { userId, type } });
    }
    pendingTab->setText(pending > 0 ? tr("Pending (%1)").arg(pending) : tr("Pending"));
    if (current == Tab::Add)
        return;

    QString emptyText;
    if (current == Tab::Plugin) {
        // A plugin's tab: its people, in its order, as implicit relationships
        // (type 5); unknown ones are asked for and shown when they arrive
        std::optional<Qord::Plugins::FriendsSection> section;
        if (auto *host = Qord::Plugins::PluginHost::instance())
            for (const auto &candidate : host->dispatchFriendsSections({ quint64(instance->accountId()) }))
                if (candidate.id == currentSection)
                    section = candidate;
        if (!section)
            return;
        QList<Snowflake> unknown;
        int order = 0;
        for (const Qord::Plugins::Id id : section->userIds) {
            const Snowflake userId(id);
            if (!instance->users()->getUser(userId)) {
                if (!requestedUsers.contains(id)) {
                    requestedUsers.insert(id);
                    unknown.append(userId);
                }
                continue;
            }
            const QString key = section->sortByName ? instance->users()->getDisplayName(userId).toLower()
                                                    : QStringLiteral("%1").arg(order++, 8, 10, QLatin1Char('0'));
            rows.append({ key, { userId, ImplicitType } });
        }
        if (!unknown.isEmpty() && requestUsers)
            requestUsers(instance, unknown);
        countLabel->setText(section->countTitle.arg(rows.size()).toUpper());
        emptyText = section->emptyText;
    } else {
        static const char *const titles[] = { "ONLINE — %1", "ALL FRIENDS — %1", "PENDING — %1", "BLOCKED — %1" };
        countLabel->setText(tr(titles[int(current)]).arg(rows.size()));
    }
    std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &row : rows)
        addRow(row.second.first, row.second.second);
    if (rows.isEmpty()) {
        auto *item = new QListWidgetItem(!emptyText.isEmpty()       ? emptyText
                                         : current == Tab::Blocked ? tr("You can't unblock the Wumpus.")
                                         : current == Tab::Pending ? tr("There are no pending friend requests. Here's Wumpus for now.")
                                         : current == Tab::Online ? tr("No one's around to play with Wumpus.")
                                                                  : tr("Wumpus is waiting on friends. You don't have to though!"),
                                         list);
        item->setTextAlignment(Qt::AlignCenter);
        item->setSizeHint(QSize(0, 120));
    }
}

void FriendsPage::addRow(Snowflake userId, int type)
{
    auto *row = new QWidget(list);
    row->setObjectName(QStringLiteral("FriendRow"));
    row->setAttribute(Qt::WA_StyledBackground, true);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(8, 6, 8, 6);
    h->setSpacing(12);

    const auto user = instance->users()->getUser(userId);
    const Core::PresenceBadge badge = instance->presences()->badge(userId);
    QPixmap avatar;
    if (user)
        avatar = images->get(instance->users()->getAvatarUrl(*user, {}, 64), QSize(AvatarSize, AvatarSize),
                             instance->accountId());
    auto *avatarLabel = new QLabel(row);
    const bool showStatus = type == int(RelationshipType::FRIEND) || type == ImplicitType;
    avatarLabel->setPixmap(roundAvatar(avatar, showStatus ? &badge : nullptr,
                                       Core::Theme::Manager::instance().color(Core::Theme::Token::BaseBg)));
    h->addWidget(avatarLabel);

    auto *text = new QVBoxLayout;
    text->setSpacing(0);
    auto *name = new QLabel(instance->users()->getDisplayName(userId), row);
    name->setObjectName(QStringLiteral("UserPanelName"));
    text->addWidget(name);
    QString sub;
    if (type == int(RelationshipType::INCOMING_REQUEST))
        sub = tr("Incoming Friend Request");
    else if (type == int(RelationshipType::OUTGOING_REQUEST))
        sub = tr("Outgoing Friend Request");
    else if (type == int(RelationshipType::BLOCKED))
        sub = tr("Blocked");
    else if (const auto *activity = instance->presences()->primaryActivity(userId);
             activity && activity->state.hasValue() && activity->isCustom())
        sub = activity->state.get();
    else
        sub = statusText(badge);
    if (user)
        sub = user->username.get() + QStringLiteral("  ·  ") + sub;
    auto *subLabel = new QLabel(sub, row);
    subLabel->setObjectName(QStringLiteral("UserPanelSubtitle"));
    text->addWidget(subLabel);
    h->addLayout(text, 1);

    const auto action = [&](const char *iconName, const QString &tip, Core::Theme::Token color,
                            std::function<void()> fn) {
        auto *b = new QToolButton(row);
        b->setObjectName(QStringLiteral("FriendAction"));
        b->setIcon(Core::Theme::Icons::icon(iconName, color));
        b->setIconSize(QSize(20, 20));
        b->setFixedSize(36, 36);
        b->setToolTip(tip);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QToolButton::clicked, this, fn);
        h->addWidget(b);
    };
    const QPointer<Core::ClientInstance> inst = instance;
    const auto reportError = [this](const QString &what) {
        return [guard = QPointer<FriendsPage>(this), what](const Core::Result<void> &r) {
            if (guard && !r.success())
                guard->countLabel->setText(tr("%1 failed: %2").arg(what, r.error));
        };
    };

    if (type == ImplicitType) {
        action(Core::Theme::Icons::Name::MessageCircle, tr("Message"), Core::Theme::Token::MutedText,
               [this, userId]() { emit openDmRequested(userId); });
    } else if (type == int(RelationshipType::FRIEND)) {
        action(Core::Theme::Icons::Name::MessageCircle, tr("Message"), Core::Theme::Token::MutedText,
               [this, userId]() { emit openDmRequested(userId); });
        action(Core::Theme::Icons::Name::UserMinus, tr("Remove Friend"), Core::Theme::Token::StatusDnd,
               [inst, userId, reportError]() {
                   if (inst)
                       inst->discord()->removeRelationship(userId, reportError(tr("Remove Friend")));
               });
    } else if (type == int(RelationshipType::INCOMING_REQUEST)) {
        action(Core::Theme::Icons::Name::UserPlus, tr("Accept"), Core::Theme::Token::StatusOnline,
               [inst, userId, reportError]() {
                   if (inst)
                       inst->discord()->putRelationship(userId, 1, reportError(tr("Accept")));
               });
        action(Core::Theme::Icons::Name::X, tr("Ignore"), Core::Theme::Token::StatusDnd, [inst, userId, reportError]() {
            if (inst)
                inst->discord()->removeRelationship(userId, reportError(tr("Ignore")));
        });
    } else if (type == int(RelationshipType::OUTGOING_REQUEST)) {
        action(Core::Theme::Icons::Name::X, tr("Cancel"), Core::Theme::Token::StatusDnd, [inst, userId, reportError]() {
            if (inst)
                inst->discord()->removeRelationship(userId, reportError(tr("Cancel")));
        });
    } else if (type == int(RelationshipType::BLOCKED)) {
        action(Core::Theme::Icons::Name::UserMinus, tr("Unblock"), Core::Theme::Token::StatusDnd,
               [inst, userId, reportError]() {
                   if (inst)
                       inst->discord()->removeRelationship(userId, reportError(tr("Unblock")));
               });
    }

    auto *item = new QListWidgetItem(list);
    item->setSizeHint(QSize(0, 62));
    list->setItemWidget(item, row);
}

} // namespace UI
} // namespace Acheron
