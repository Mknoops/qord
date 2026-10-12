#pragma once

#include <QFrame>
#include <QPointer>
#include <QTimer>

#include <functional>

#include "Qord/SlashCommands.hpp"

class QLabel;
class QListWidget;
class QTextEdit;

namespace Acheron {
namespace UI {

// Discord's slash command menu: typing "/" in the message box lists the
// channel's commands (name, description, app); Up/Down/Tab/Enter pick one.
// After the name it shows the command's options as a hint, and while an
// option's value is typed, its choices: the option's own, or the ones its app
// suggests (autocomplete). A picked choice shows its name in the text and is
// sent as its value.
class SlashCommandPopup : public QFrame
{
    Q_OBJECT
public:
    // Returns the index for the current channel, or null while it loads
    // (the provider starts loading and calls refresh() when done)
    using IndexProvider = std::function<const Qord::SlashCommands::Index *()>;
    using Choices = QList<QPair<QString, QJsonValue>>;
    // Asks the command's app for suggestions (an autocomplete interaction)
    using AutocompleteRequester = std::function<void(const Qord::SlashCommands::Command &command,
                                                     const QJsonObject &data, std::function<void(const Choices &)> done)>;

    SlashCommandPopup(QTextEdit *editor, QWidget *anchor, IndexProvider provider,
                      AutocompleteRequester autocomplete = nullptr);

    void refresh();
    // The choices picked for this command, for sending it
    [[nodiscard]] const Qord::SlashCommands::Picks &picks() const { return picked; }
    void clearPicks() { picked.clear(); }

    static constexpr int AutocompleteDelayMs = 250;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class Mode { Commands, Choices };
    void accept();
    void place();
    void showChoices(const Choices &choices, const QString &header);
    void requestAutocomplete();

    QPointer<QTextEdit> editor;
    QPointer<QWidget> anchor;
    IndexProvider provider;
    AutocompleteRequester autocomplete;
    QListWidget *list;
    QLabel *hint;
    Mode mode = Mode::Commands;
    Qord::SlashCommands::Picks picked;
    QString focusedOption; // the option the choices are for
    int focusedValueStart = -1;
    QTimer autocompleteDelay;
    int autocompleteGeneration = 0;
    QString autocompleteText; // the text the pending request was for
};

} // namespace UI
} // namespace Acheron
