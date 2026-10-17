#pragma once

#include <QAbstractButton>
#include <QScrollArea>
#include <QVariant>

#include <functional>

class QComboBox;
class QLabel;
class QSlider;
class QVBoxLayout;

namespace Acheron {
namespace UI {

// Discord's on/off switch
class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
};

// A settings page in Discord's layout: sections of rows, each a title and an
// explanation on the left and its control on the right, divided by lines.
// Controls made with a preference key read and write Qord::Preferences and
// follow it when it changes elsewhere (a keybind, another window).
class SettingsForm : public QScrollArea
{
    Q_OBJECT
public:
    using Options = QList<QPair<QString, QVariant>>;

    explicit SettingsForm(QWidget *parent = nullptr);

    void addSection(const QString &title, const QString &description = {});
    ToggleSwitch *addToggle(const char *key, const QString &title, const QString &description = {});
    ToggleSwitch *addSwitch(const QString &title, const QString &description, bool checked,
                            const std::function<void(bool)> &toggled);
    QComboBox *addChoice(const char *key, const QString &title, const QString &description, const Options &options);
    QSlider *addSlider(const char *key, const QString &title, const QString &description, int min, int max, int step,
                       const QString &suffix);
    // Any control; a null `control` makes a full-width explanation row
    QWidget *addRow(const QString &title, const QString &description, QWidget *control);
    void addWidget(QWidget *widget, int stretch = 0);
    void addNote(const QString &text);
    // The row of `control` is usable only while the preference `key` is on
    void dependsOn(QWidget *control, const char *key);
    void finish();

    // Every title and explanation, for the settings search
    [[nodiscard]] const QStringList &searchText() const { return words; }

private:
    QWidget *row(const QString &title, const QString &description, QWidget *control);

    QWidget *body;
    QVBoxLayout *layout;
    QStringList words;
    bool needsDivider = false;
};

} // namespace UI
} // namespace Acheron
