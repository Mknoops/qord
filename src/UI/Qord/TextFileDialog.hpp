#pragma once

// A text file attachment's "View whole file": what Qord loaded of it, with
// word wrap, plugin buttons (CopyFileContents) and Download.

#include <QString>
#include <QUrl>

#include <functional>

class QWidget;

namespace Acheron {
struct AttachmentData;

namespace UI {
namespace TextFileDialog {

// `pluginButtonClicked` gets the index of the button in att.textButtons
void show(QWidget *parent, const AttachmentData &att, std::function<void(int index)> pluginButtonClicked,
          std::function<void()> download);

} // namespace TextFileDialog
} // namespace UI
} // namespace Acheron
