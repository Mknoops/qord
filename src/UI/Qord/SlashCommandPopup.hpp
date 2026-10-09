#pragma once

#include <QFrame>
#include <QPointer>

#include <functional>

#include "Qord/SlashCommands.hpp"

class QLabel;
class QListWidget;
class QTextEdit;

namespace Acheron {
namespace UI {

// Discord's slash command menu: typing "/" in the message box lists the
// channel's commands (name, description, app); Up/Down/Tab/Enter pick one.
// After the name it shows the command's options as a hint.
class SlashCommandPopup : public QFrame
{
    Q_OBJECT
public:
    // Returns the index for the current channel, or null while it loads
    // (the provider starts loading and calls refresh() when done)
    using IndexProvider = std::function<const Qord::SlashCommands::Index *()>;

    SlashCommandPopup(QTextEdit *editor, QWidget *anchor, IndexProvider provider);

    void refresh();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void accept();
    void place();

    QPointer<QTextEdit> editor;
    QPointer<QWidget> anchor;
    IndexProvider provider;
    QListWidget *list;
    QLabel *hint;
};

} // namespace UI
} // namespace Acheron
