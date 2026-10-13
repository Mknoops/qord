#pragma once

#include <QDialog>

#include "Core/Snowflake.hpp"
#include "Plugins/Types.hpp"

class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

namespace Acheron {
namespace Core {
class ImageManager;
} // namespace Core

namespace UI {

// A plugin's list window (UiService::openListDialog, ReviewDB's reviews): a
// notice, items with an avatar, a name, text and a "…" menu, pages, and an
// input that submits on Enter.
class ListDialog : public QDialog
{
    Q_OBJECT
public:
    ListDialog(Core::ImageManager *images, Core::Snowflake accountId, Qord::Plugins::ListDialogEvents events,
               QWidget *parent = nullptr);

    void setView(const Qord::Plugins::ListDialogView &view);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget *itemWidget(const Qord::Plugins::ListDialogItem &item);

    Core::ImageManager *images;
    Core::Snowflake accountId;
    Qord::Plugins::ListDialogEvents events;
    bool inputEnabled = true;
    QLabel *notice;
    QVBoxLayout *list;
    QWidget *pager;
    QPushButton *previous;
    QPushButton *next;
    QLabel *pageText;
    QLineEdit *input;
};

} // namespace UI
} // namespace Acheron
