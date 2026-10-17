#include "ServerRailView.hpp"

#include "MentionJumpIndicator.hpp"
#include "ServerRailModel.hpp"

#include <QClipboard>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QHelpEvent>
#include <QToolTip>

namespace Acheron {
namespace UI {

ServerRailView::ServerRailView(QWidget *parent)
    : QListView(parent)
{
    setSelectionMode(QAbstractItemView::NoSelection);
    setFocusPolicy(Qt::NoFocus);
    setUniformItemSizes(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
}

void ServerRailView::setModel(QAbstractItemModel *model)
{
    QListView::setModel(model);
    delete mentionJump;
    mentionJump = nullptr;
    if (!model)
        return;

    mentionJump = new MentionJumpIndicator(
            this,
            [](const QModelIndex &row) { return row.sibling(row.row() - 1, 0); },
            [](const QModelIndex &row) { return row.sibling(row.row() + 1, 0); },
            [](const QModelIndex &row) {
                const bool childRowsShowTheirOwn = row.data(ServerRailModel::IsExpandedRole).toBool();
                return !childRowsShowTheirOwn && row.data(ServerRailModel::MentionCountRole).toInt() > 0;
            });
}

void ServerRailView::setGuildSettingsProvider(GuildSettingsAccess::SectionsProvider provider)
{
    guildSettingsProvider = std::move(provider);
}

void ServerRailView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return QListView::mousePressEvent(event);

    QModelIndex idx = indexAt(event->pos());
    if (!idx.isValid())
        return QListView::mousePressEvent(event);

    auto kind = static_cast<ServerRailModel::Kind>(idx.data(ServerRailModel::KindRole).toInt());
    Core::Snowflake accountId(idx.data(ServerRailModel::AccountIdRole).toULongLong());
    Core::Snowflake id(idx.data(ServerRailModel::IdRole).toULongLong());

    switch (kind) {
    case ServerRailModel::Kind::AccountHome:
        emit accountHomeClicked(accountId);
        break;
    case ServerRailModel::Kind::Server:
        emit guildClicked(accountId, id);
        break;
    case ServerRailModel::Kind::Folder:
        emit folderToggleClicked(accountId, id);
        break;
    }

    event->accept();
}

void ServerRailView::contextMenuEvent(QContextMenuEvent *event)
{
    QModelIndex idx = indexAt(event->pos());
    if (!idx.isValid())
        return;

    auto kind = static_cast<ServerRailModel::Kind>(idx.data(ServerRailModel::KindRole).toInt());
    if (kind != ServerRailModel::Kind::Folder && kind != ServerRailModel::Kind::Server)
        return;

    Core::Snowflake accountId(idx.data(ServerRailModel::AccountIdRole).toULongLong());
    Core::Snowflake id(idx.data(ServerRailModel::IdRole).toULongLong());
    execMenu(accountId, id, kind == ServerRailModel::Kind::Folder, idx.data(ServerRailModel::IsUnreadRole).toBool(),
             idx.data(ServerRailModel::MentionCountRole).toInt(),
             Core::Snowflake(idx.data(ServerRailModel::OwnerIdRole).toULongLong()), event->globalPos());
}

void ServerRailView::showGuildMenu(Core::Snowflake accountId, Core::Snowflake guildId, const QPoint &globalPos)
{
    // The server's row, for its unread state and owner; a server in a closed
    // folder has none, and its menu still opens
    QModelIndex found;
    for (int row = 0; model() && row < model()->rowCount() && !found.isValid(); ++row) {
        const QModelIndex idx = model()->index(row, 0);
        if (static_cast<ServerRailModel::Kind>(idx.data(ServerRailModel::KindRole).toInt()) == ServerRailModel::Kind::Server
            && Core::Snowflake(idx.data(ServerRailModel::AccountIdRole).toULongLong()) == accountId
            && Core::Snowflake(idx.data(ServerRailModel::IdRole).toULongLong()) == guildId)
            found = idx;
    }
    execMenu(accountId, guildId, false, !found.isValid() || found.data(ServerRailModel::IsUnreadRole).toBool(),
             found.data(ServerRailModel::MentionCountRole).toInt(),
             Core::Snowflake(found.data(ServerRailModel::OwnerIdRole).toULongLong()), globalPos);
}

void ServerRailView::execMenu(Core::Snowflake accountId, Core::Snowflake id, bool isFolder, bool isUnread, int mentions,
                              Core::Snowflake ownerId, const QPoint &globalPos)
{
    QMenu menu(this);
    QAction *markRead = menu.addAction(tr("Mark As Read"));
    markRead->setEnabled(isUnread || mentions > 0);
    connect(markRead, &QAction::triggered, this, [this, accountId, id, isFolder]() {
        emit markAsReadRequested(accountId, id, isFolder);
    });

    if (!isFolder) {
        GuildSettingsAccess::addMenu(&menu, guildSettingsProvider, accountId, id, [this, accountId, id](GuildSettingsSection section) {
            emit guildSettingsRequested(accountId, id, section);
        });

        if (menuExtender)
            menuExtender(&menu, accountId, id);

        menu.addSeparator();
        QAction *leaveAction = menu.addAction(tr("Leave"));
        leaveAction->setEnabled(ownerId != accountId);
        connect(leaveAction, &QAction::triggered, this, [this, accountId, id]() {
            emit leaveGuildRequested(accountId, id);
        });

        menu.addSeparator();
        QAction *copyIdAction = menu.addAction(tr("Copy Server ID"));
        connect(copyIdAction, &QAction::triggered, this, [id]() {
            QGuiApplication::clipboard()->setText(QString::number(quint64(id)));
        });
    }

    menu.exec(globalPos);
}

bool ServerRailView::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::ToolTip && tooltipExtender) {
        auto *help = static_cast<QHelpEvent *>(event);
        const QModelIndex idx = indexAt(help->pos());
        if (idx.isValid() &&
            static_cast<ServerRailModel::Kind>(idx.data(ServerRailModel::KindRole).toInt()) == ServerRailModel::Kind::Server) {
            const QString extra = tooltipExtender(Core::Snowflake(idx.data(ServerRailModel::AccountIdRole).toULongLong()),
                                                  Core::Snowflake(idx.data(ServerRailModel::IdRole).toULongLong()));
            if (!extra.isEmpty()) {
                QToolTip::showText(help->globalPos(),
                                   QStringLiteral("<b>%1</b><br>%2")
                                           .arg(idx.data(Qt::ToolTipRole).toString().toHtmlEscaped(), extra),
                                   viewport(), visualRect(idx));
                return true;
            }
        }
    }
    return QListView::viewportEvent(event);
}

void ServerRailView::mouseMoveEvent(QMouseEvent *event)
{
    viewport()->setCursor(indexAt(event->pos()).isValid() ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QListView::mouseMoveEvent(event);
}

} // namespace UI
} // namespace Acheron
