#include "TextFileDialog.hpp"

#include "Core/Theme/Icons.hpp"
#include "UI/Chat/ChatLayout.hpp"
#include "UI/Chat/ChatModel.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSvgRenderer>
#include <QToolButton>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {
namespace TextFileDialog {

void show(QWidget *parent, const AttachmentData &att, std::function<void(int)> pluginButtonClicked,
          std::function<void()> download)
{
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(att.filename);
    dialog->resize(820, 600);
    auto *layout = new QVBoxLayout(dialog);

    auto *header = new QHBoxLayout;
    auto *name = new QLabel(QStringLiteral("<b>%1</b>&nbsp;&nbsp;<span style=\"color: gray;\">%2</span>")
                                    .arg(att.filename.toHtmlEscaped(), ChatLayout::formatFileSize(att.fileSizeBytes)),
                            dialog);
    header->addWidget(name, 1);
    const QColor color = dialog->palette().color(QPalette::Text);
    const qreal dpr = dialog->devicePixelRatioF();
    for (int i = 0; i < att.textButtons.size(); ++i) {
        QByteArray svg = att.textButtons[i].svg;
        svg.replace("currentColor", color.name().toLatin1());
        QSvgRenderer renderer(svg);
        QPixmap icon(QSize(18, 18) * dpr);
        icon.setDevicePixelRatio(dpr);
        icon.fill(Qt::transparent);
        if (renderer.isValid()) {
            QPainter painter(&icon);
            renderer.render(&painter, QRectF(0, 0, 18, 18));
        }
        auto *button = new QToolButton(dialog);
        button->setAutoRaise(true);
        button->setIcon(QIcon(icon));
        button->setToolTip(att.textButtons[i].tooltip);
        QObject::connect(button, &QToolButton::clicked, dialog, [pluginButtonClicked, i]() {
            if (pluginButtonClicked)
                pluginButtonClicked(i);
        });
        header->addWidget(button);
    }
    auto *wrap = new QCheckBox(QObject::tr("Word wrap"), dialog);
    header->addWidget(wrap);
    layout->addLayout(header);

    auto *text = new QPlainTextEdit(att.textPreview, dialog);
    text->setReadOnly(true);
    text->setFont(ChatLayout::textPreviewFont());
    text->setLineWrapMode(QPlainTextEdit::NoWrap);
    QObject::connect(wrap, &QCheckBox::toggled, text, [text](bool on) {
        text->setLineWrapMode(on ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    });
    layout->addWidget(text, 1);

    auto *footer = new QHBoxLayout;
    if (att.textBytesLeft > 0)
        footer->addWidget(new QLabel(QObject::tr("%1 bytes left").arg(QLocale().toString(att.textBytesLeft)), dialog));
    footer->addStretch(1);
    auto *downloadButton = new QPushButton(QObject::tr("Download"), dialog);
    QObject::connect(downloadButton, &QPushButton::clicked, dialog, [download]() {
        if (download)
            download();
    });
    footer->addWidget(downloadButton);
    auto *close = new QPushButton(QObject::tr("Close"), dialog);
    QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::close);
    footer->addWidget(close);
    layout->addLayout(footer);
    dialog->show();
}

} // namespace TextFileDialog
} // namespace UI
} // namespace Acheron
