#include "SlashCommandPopup.hpp"

#include "Core/Theme/Manager.hpp"

#include <QJsonValue>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QTextEdit>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

namespace {
constexpr int ValueRole = Qt::UserRole + 1;
}

SlashCommandPopup::SlashCommandPopup(QTextEdit *editor, QWidget *anchor, IndexProvider provider,
                                     AutocompleteRequester autocomplete)
    : QFrame(anchor, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus),
      editor(editor),
      anchor(anchor),
      provider(std::move(provider)),
      autocomplete(std::move(autocomplete))
{
    autocompleteDelay.setSingleShot(true);
    autocompleteDelay.setInterval(AutocompleteDelayMs);
    connect(&autocompleteDelay, &QTimer::timeout, this, &SlashCommandPopup::requestAutocomplete);
    setObjectName(QStringLiteral("EmojiPicker")); // same frame style as the pickers
    setAttribute(Qt::WA_ShowWithoutActivating);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    hint = new QLabel(this);
    hint->setObjectName(QStringLiteral("EmojiPickerPreview"));
    hint->setTextFormat(Qt::PlainText);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    list = new QListWidget(this);
    list->setObjectName(QStringLiteral("EmojiPickerGrid"));
    list->setFocusPolicy(Qt::NoFocus);
    list->setUniformItemSizes(true);
    layout->addWidget(list);
    connect(list, &QListWidget::itemClicked, this, [this]() { accept(); });
    editor->installEventFilter(this);
    connect(editor, &QTextEdit::textChanged, this, &SlashCommandPopup::refresh);
    hide();
}

void SlashCommandPopup::refresh()
{
    if (!editor)
        return;
    const QString text = editor->toPlainText();
    if (!text.startsWith(QLatin1Char('/')) || text.contains(QLatin1Char('\n'))) {
        picked.clear();
        hide();
        return;
    }
    const Qord::SlashCommands::Index *index = provider ? provider() : nullptr;
    list->clear();
    mode = Mode::Commands;
    autocompleteDelay.stop();
    const int space = text.indexOf(QLatin1Char(' '));
    if (!index) {
        hint->setText(tr("Loading commands…"));
        list->hide();
    } else if (space < 0) {
        // Still typing the name: list matching commands
        picked.clear();
        const auto matches = Qord::SlashCommands::suggest(*index, text.mid(1), 25);
        for (const auto &c : matches) {
            auto *item = new QListWidgetItem(QStringLiteral("/%1  —  %2").arg(c.name, c.description), list);
            item->setData(Qt::UserRole, c.name);
            if (!c.applicationName.isEmpty())
                item->setToolTip(c.applicationName);
        }
        hint->setText(matches.isEmpty() ? tr("No commands match") : tr("Commands — Tab or Enter to choose"));
        list->setVisible(!matches.isEmpty());
        if (!matches.isEmpty())
            list->setCurrentRow(0);
    } else if (const auto *command = Qord::SlashCommands::find(*index, text)) {
        // Writing options: show what the command takes
        QString line = Qord::SlashCommands::usage(*command, text);
        if (!command->description.isEmpty())
            line += QStringLiteral("\n") + command->description;
        if (!command->applicationName.isEmpty())
            line += QStringLiteral(" · ") + command->applicationName;
        list->hide();
        // ...and the values the option being written can take
        const auto focus = Qord::SlashCommands::focus(*index, text, picked);
        // A value followed by a space is finished (a picked choice ends so):
        // no list, so Enter sends
        if (focus && focus->option && !focus->typed.endsWith(QLatin1Char(' '))) {
            focusedOption = focus->option->name;
            focusedValueStart = focus->valueStart;
            const QString header = QStringLiteral("%1: %2").arg(focus->option->name, focus->option->description);
            if (!focus->option->choices.isEmpty()) {
                showChoices(Qord::SlashCommands::matchingChoices(*focus->option, focus->typed), header);
                place();
                return;
            }
            if (focus->option->autocomplete && autocomplete) {
                hint->setText(header + QStringLiteral("\n") + tr("Loading suggestions…"));
                autocompleteText = text;
                autocompleteDelay.start(); // wait for a pause in typing
                place();
                return;
            }
            line += QStringLiteral("\n") + header;
        }
        hint->setText(line);
    } else {
        hide();
        return;
    }
    place();
}

void SlashCommandPopup::showChoices(const Choices &choices, const QString &header)
{
    list->clear();
    mode = Mode::Choices;
    for (const auto &[name, value] : choices) {
        auto *item = new QListWidgetItem(name, list);
        item->setData(Qt::UserRole, name);
        item->setData(ValueRole, value.toVariant());
    }
    hint->setText(header + QStringLiteral("\n") +
                  (choices.isEmpty() ? tr("No choices match") : tr("Tab or Enter to choose")));
    list->setVisible(!choices.isEmpty());
    if (!choices.isEmpty())
        list->setCurrentRow(0);
}

void SlashCommandPopup::requestAutocomplete()
{
    if (!editor || !autocomplete || editor->toPlainText() != autocompleteText)
        return;
    const Qord::SlashCommands::Index *index = provider ? provider() : nullptr;
    const auto focus = index ? Qord::SlashCommands::focus(*index, autocompleteText, picked) : std::nullopt;
    if (!focus || !focus->option || !focus->option->autocomplete)
        return;
    const int request = ++autocompleteGeneration;
    const QString header = QStringLiteral("%1: %2").arg(focus->option->name, focus->option->description);
    const QPointer<SlashCommandPopup> guard(this);
    autocomplete(*focus->command, focus->autocompleteData, [guard, request, header](const Choices &choices) {
        // Only the answer to the latest request, while the text is unchanged
        if (!guard || request != guard->autocompleteGeneration || !guard->editor ||
            guard->editor->toPlainText() != guard->autocompleteText)
            return;
        guard->showChoices(choices, header);
        guard->place();
    });
}

void SlashCommandPopup::place()
{
    if (!anchor)
        return;
    const int rows = list->isVisible() ? qMin(list->count(), 8) : 0;
    list->setFixedHeight(rows * 26 + (rows ? 6 : 0));
    resize(qMax(360, anchor->width() / 2), sizeHint().height());
    move(anchor->mapToGlobal(QPoint(0, -height() - 4)));
    show();
    raise();
}

void SlashCommandPopup::accept()
{
    if (!editor || !list->currentItem())
        return;
    const QString name = list->currentItem()->data(Qt::UserRole).toString();
    QString text = editor->toPlainText();
    if (mode == Mode::Choices && focusedValueStart >= 0 && focusedValueStart <= text.size()) {
        // Put the choice's name in place of what was typed, and remember its value
        picked.insert(focusedOption, { name, QJsonValue::fromVariant(list->currentItem()->data(ValueRole)) });
        text = text.left(focusedValueStart) + name + QLatin1Char(' ');
    } else {
        picked.clear();
        text = QStringLiteral("/%1 ").arg(name);
    }
    editor->setPlainText(text);
    QTextCursor cursor = editor->textCursor();
    cursor.movePosition(QTextCursor::End);
    editor->setTextCursor(cursor);
}

bool SlashCommandPopup::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == editor && event->type() == QEvent::KeyPress && isVisible() && list->isVisible() && list->count()) {
        auto *key = static_cast<QKeyEvent *>(event);
        switch (key->key()) {
        case Qt::Key_Down:
            list->setCurrentRow(qMin(list->currentRow() + 1, list->count() - 1));
            return true;
        case Qt::Key_Up:
            list->setCurrentRow(qMax(list->currentRow() - 1, 0));
            return true;
        case Qt::Key_Tab:
        case Qt::Key_Return:
        case Qt::Key_Enter:
            accept();
            return true;
        case Qt::Key_Escape:
            hide();
            return true;
        default:
            break;
        }
    }
    if (watched == editor && event->type() == QEvent::FocusOut)
        hide();
    return QFrame::eventFilter(watched, event);
}

} // namespace UI
} // namespace Acheron
