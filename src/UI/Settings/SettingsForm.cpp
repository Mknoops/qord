#include "SettingsForm.hpp"

#include "Core/Theme/Manager.hpp"
#include "Core/Theme/Tokens.hpp"
#include "Qord/Preferences.hpp"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

using Core::Theme::Manager;
using Core::Theme::Token;
using Qord::Preferences;

ToggleSwitch::ToggleSwitch(QWidget *parent) : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize ToggleSwitch::sizeHint() const
{
    return { 40, 24 };
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
    const auto &theme = Manager::instance();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF track = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QColor trackColor = isChecked() ? theme.color(Token::Highlight) : theme.color(Token::MutedText).darker(150);
    if (!isEnabled())
        trackColor.setAlphaF(0.4);
    painter.setPen(Qt::NoPen);
    painter.setBrush(trackColor);
    painter.drawRoundedRect(track, track.height() / 2, track.height() / 2);
    if (hasFocus()) {
        painter.setPen(QPen(theme.color(Token::LinkText), 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(track.adjusted(-0.5, -0.5, 0.5, 0.5), track.height() / 2, track.height() / 2);
        painter.setPen(Qt::NoPen);
    }
    const qreal knob = track.height() - 6;
    const qreal x = isChecked() ? track.right() - 3 - knob : track.left() + 3;
    QColor knobColor(Qt::white);
    if (!isEnabled())
        knobColor.setAlphaF(0.6);
    painter.setBrush(knobColor);
    painter.drawEllipse(QRectF(x, track.top() + 3, knob, knob));
    if (isChecked()) {
        // A check in the knob, like Discord's
        painter.setPen(QPen(trackColor, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPointF c(x + knob / 2, track.top() + 3 + knob / 2);
        const QPointF tick[] = { c + QPointF(-3.5, 0), c + QPointF(-1, 2.5), c + QPointF(3.5, -2.5) };
        painter.drawPolyline(tick, 3);
    }
}

SettingsForm::SettingsForm(QWidget *parent) : QScrollArea(parent)
{
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    body = new QWidget(this);
    body->setObjectName(QStringLiteral("SettingsFormBody"));
    layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 0, 16, 24);
    layout->setSpacing(0);
    setWidget(body);
}

void SettingsForm::addSection(const QString &title, const QString &description)
{
    auto *label = new QLabel(title, body);
    label->setObjectName(QStringLiteral("SettingsSectionTitle"));
    if (layout->count() > 0)
        layout->addSpacing(28);
    layout->addWidget(label);
    layout->addSpacing(8);
    words << title;
    if (!description.isEmpty()) {
        auto *help = new QLabel(description, body);
        help->setObjectName(QStringLiteral("SettingsHelp"));
        help->setWordWrap(true);
        layout->addWidget(help);
        layout->addSpacing(8);
        words << description;
    }
    needsDivider = false;
}

QWidget *SettingsForm::row(const QString &title, const QString &description, QWidget *control)
{
    if (needsDivider) {
        auto *line = new QFrame(body);
        line->setObjectName(QStringLiteral("SettingsDivider"));
        line->setFixedHeight(1);
        layout->addWidget(line);
    }
    needsDivider = true;

    auto *row = new QWidget(body);
    row->setObjectName(QStringLiteral("SettingsRow"));
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 12, 0, 12);
    h->setSpacing(24);
    auto *text = new QVBoxLayout;
    text->setSpacing(4);
    if (!title.isEmpty()) {
        auto *name = new QLabel(title, row);
        name->setObjectName(QStringLiteral("SettingsRowTitle"));
        name->setWordWrap(true);
        text->addWidget(name);
        words << title;
    }
    if (!description.isEmpty()) {
        auto *help = new QLabel(description, row);
        help->setObjectName(QStringLiteral("SettingsHelp"));
        help->setWordWrap(true);
        help->setTextFormat(Qt::PlainText);
        text->addWidget(help);
        words << description;
    }
    h->addLayout(text, 1);
    if (control) {
        control->setParent(row);
        h->addWidget(control, 0, Qt::AlignTop | Qt::AlignRight);
    }
    layout->addWidget(row);
    return row;
}

ToggleSwitch *SettingsForm::addToggle(const char *key, const QString &title, const QString &description)
{
    const QString name = QString::fromLatin1(key);
    auto *toggle = new ToggleSwitch;
    toggle->setChecked(Preferences::flag(name));
    toggle->setAccessibleName(title);
    row(title, description, toggle);
    connect(toggle, &ToggleSwitch::toggled, this, [name](bool on) { Preferences::set(name, on); });
    connect(&Preferences::instance(), &Preferences::changed, toggle, [toggle, name](const QString &changed) {
        if (changed != name)
            return;
        const QSignalBlocker block(toggle);
        toggle->setChecked(Preferences::flag(name));
        toggle->update();
    });
    return toggle;
}

ToggleSwitch *SettingsForm::addSwitch(const QString &title, const QString &description, bool checked,
                                      const std::function<void(bool)> &toggled)
{
    auto *toggle = new ToggleSwitch;
    toggle->setChecked(checked);
    toggle->setAccessibleName(title);
    row(title, description, toggle);
    connect(toggle, &ToggleSwitch::toggled, this, toggled);
    return toggle;
}

QComboBox *SettingsForm::addChoice(const char *key, const QString &title, const QString &description,
                                   const Options &options)
{
    const QString name = QString::fromLatin1(key);
    auto *combo = new QComboBox;
    combo->setMinimumWidth(180);
    combo->setAccessibleName(title);
    for (const auto &option : options)
        combo->addItem(option.first, option.second);
    const auto select = [combo, name]() {
        const QSignalBlocker block(combo);
        const int at = combo->findData(Preferences::value(name));
        combo->setCurrentIndex(at >= 0 ? at : 0);
    };
    select();
    row(title, description, combo);
    connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [combo, name](int index) { Preferences::set(name, combo->itemData(index)); });
    connect(&Preferences::instance(), &Preferences::changed, combo, [select, name](const QString &changed) {
        if (changed == name)
            select();
    });
    return combo;
}

QSlider *SettingsForm::addSlider(const char *key, const QString &title, const QString &description, int min, int max,
                                 int step, const QString &suffix)
{
    const QString name = QString::fromLatin1(key);
    auto *box = new QWidget;
    auto *h = new QHBoxLayout(box);
    h->setContentsMargins(0, 0, 0, 0);
    auto *slider = new QSlider(Qt::Horizontal, box);
    slider->setRange(min, max);
    slider->setSingleStep(step);
    slider->setPageStep(step);
    slider->setFixedWidth(200);
    slider->setAccessibleName(title);
    auto *value = new QLabel(box);
    value->setObjectName(QStringLiteral("SettingsHelp"));
    value->setMinimumWidth(48);
    h->addWidget(slider);
    h->addWidget(value);
    const auto show = [slider, value, suffix](int v) { value->setText(QString::number(v) + suffix); };
    slider->setValue(Preferences::number(name));
    show(slider->value());
    row(title, description, box);
    connect(slider, &QSlider::valueChanged, this, [slider, show, name, step](int v) {
        const int snapped = step > 1 ? qRound(double(v) / step) * step : v;
        if (snapped != v) {
            slider->setValue(snapped);
            return;
        }
        show(v);
        Preferences::set(name, v);
    });
    return slider;
}

QWidget *SettingsForm::addRow(const QString &title, const QString &description, QWidget *control)
{
    return row(title, description, control);
}

void SettingsForm::addWidget(QWidget *widget, int stretch)
{
    widget->setParent(body);
    layout->addWidget(widget, stretch);
    needsDivider = false;
}

void SettingsForm::addNote(const QString &text)
{
    auto *note = new QLabel(text, body);
    note->setObjectName(QStringLiteral("SettingsHelp"));
    note->setWordWrap(true);
    layout->addSpacing(8);
    layout->addWidget(note);
    words << text;
    needsDivider = false;
}

void SettingsForm::dependsOn(QWidget *control, const char *key)
{
    const QString name = QString::fromLatin1(key);
    QWidget *target = control->parentWidget() && control->parentWidget()->objectName() == QLatin1String("SettingsRow")
                              ? control->parentWidget()
                              : control;
    // A control inside a box (the slider's) belongs to the box's row
    if (target == control && control->parentWidget() && control->parentWidget()->parentWidget()
        && control->parentWidget()->parentWidget()->objectName() == QLatin1String("SettingsRow"))
        target = control->parentWidget()->parentWidget();
    target->setEnabled(Preferences::flag(name));
    connect(&Preferences::instance(), &Preferences::changed, target, [target, name](const QString &changed) {
        if (changed == name)
            target->setEnabled(Preferences::flag(name));
    });
}

void SettingsForm::finish()
{
    layout->addStretch(1);
}

} // namespace UI
} // namespace Acheron
