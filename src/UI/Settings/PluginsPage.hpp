#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QScrollArea;

namespace Qord {
namespace Plugins {
class PluginHost;
}
} // namespace Qord

namespace Acheron {
namespace UI {

// Qord plugin manager: enable plugins, grant their permissions, and edit
// their settings (editors are generated from each plugin's schema).
class PluginsPage : public QWidget
{
    Q_OBJECT
public:
    explicit PluginsPage(QWidget *parent = nullptr);

private:
    void populate();
    void applyFilter();
    void showDetails(const QString &id);
    void onItemChanged(QListWidgetItem *item);
    QWidget *buildSettingsEditor(const QString &id, QWidget *parent);

    Qord::Plugins::PluginHost *host;
    QLineEdit *search;
    QComboBox *category;
    QListWidget *list;
    QScrollArea *detailsScroll;
    QString shownId;
    bool updating = false;
};

} // namespace UI
} // namespace Acheron
