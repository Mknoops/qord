#pragma once

#include <QListView>

#include <functional>

class QMenu;

#include "Core/Snowflake.hpp"
#include "UI/GuildSettings/GuildSettingsSection.hpp"

namespace Acheron {
namespace UI {

class MentionJumpIndicator;

class ServerRailView : public QListView
{
    Q_OBJECT
public:
    explicit ServerRailView(QWidget *parent = nullptr);

    void setModel(QAbstractItemModel *model) override;
    void setGuildSettingsProvider(GuildSettingsAccess::SectionsProvider provider);

signals:
    void accountHomeClicked(Core::Snowflake accountId);
    void guildClicked(Core::Snowflake accountId, Core::Snowflake guildId);
    void folderToggleClicked(Core::Snowflake accountId, Core::Snowflake folderId);
    void markAsReadRequested(Core::Snowflake accountId, Core::Snowflake id, bool isFolder);
    void leaveGuildRequested(Core::Snowflake accountId, Core::Snowflake guildId);
    void guildSettingsRequested(Core::Snowflake accountId, Core::Snowflake guildId, GuildSettingsSection section);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

public:
    // Qord: lets the window add items (mute, notifications, plugins) to a server's menu
    using MenuExtender = std::function<void(QMenu *menu, Core::Snowflake accountId, Core::Snowflake guildId)>;
    void setMenuExtender(MenuExtender extender) { menuExtender = std::move(extender); }
    // Qord: extra rich text under a server's name in its tooltip (MemberCount)
    using TooltipExtender = std::function<QString(Core::Snowflake accountId, Core::Snowflake guildId)>;
    void setTooltipExtender(TooltipExtender extender) { tooltipExtender = std::move(extender); }

protected:
    bool viewportEvent(QEvent *event) override;

private:
    MenuExtender menuExtender;
    TooltipExtender tooltipExtender;

protected:

private:
    GuildSettingsAccess::SectionsProvider guildSettingsProvider;
    MentionJumpIndicator *mentionJump = nullptr;
};

} // namespace UI
} // namespace Acheron
