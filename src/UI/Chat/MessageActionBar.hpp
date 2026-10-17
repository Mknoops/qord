#pragma once

#include <QFrame>

#include "Core/Theme/Tokens.hpp"

class QToolButton;

namespace Acheron {
namespace UI {

class MessageActionBar : public QFrame
{
    Q_OBJECT
public:
    enum class Action {
        CopyId,
        CopyLink,
        Reply,
        Delete,
        AddReaction, // Qord: Discord's react button on the hover bar
    };
    Q_ENUM(Action)

    explicit MessageActionBar(QWidget *parent = nullptr);

    static QString label(Action action);

    void setShiftHeld(bool held);
    void setCanDelete(bool canDelete);
    void setMoreButtonDown(bool down);

    // Qord: plugin buttons (Hook::MessagePopover), before the bar's own
    struct PluginButton
    {
        QString pluginId;
        QString id;
        QString tooltip;
        QByteArray svg;
    };
    void setPluginButtons(const QList<PluginButton> &buttons);

signals:
    void triggered(Acheron::UI::MessageActionBar::Action action);
    void moreRequested(const QPoint &buttonTopLeft);
    void shiftHeldChanged(bool held);
    void pluginButtonClicked(const QString &pluginId, const QString &buttonId, bool rightClick);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    QToolButton *addButton(const QString &iconName, const QString &toolTip, Core::Theme::Token color);
    QToolButton *addActionButton(Action action, const QString &iconName, Core::Theme::Token color);
    void updateButtons();

    QToolButton *copyIdButton = nullptr;
    QToolButton *copyLinkButton = nullptr;
    QToolButton *deleteButton = nullptr;
    QToolButton *moreButton = nullptr;

    bool shiftHeld = false;
    bool canDelete = false;
    QList<QToolButton *> pluginButtons;
    QList<PluginButton> shownPluginButtons;
};

} // namespace UI
} // namespace Acheron
