#include "SlashCommandPopup.hpp"

#include "Core/Theme/Manager.hpp"

#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QTextEdit>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

SlashCommandPopup::SlashCommandPopup(QTextEdit *editor, QWidget *anchor, IndexProvider provider)
    : QFrame(anchor, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus),
      editor(editor),
      anchor(anchor),
      provider(std::move(provider))
{
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
        hide();
        return;
    }
    const Qord::SlashCommands::Index *index = provider ? provider() : nullptr;
    list->clear();
    const int space = text.indexOf(QLatin1Char(' '));
    if (!index) {
        hint->setText(tr("Loading commands…"));
        list->hide();
    } else if (space < 0) {
        // Still typing the name: list matching commands
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
        hint->setText(line);
        list->hide();
    } else {
        hide();
        return;
    }
    place();
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
    editor->setPlainText(QStringLiteral("/%1 ").arg(name));
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
