/*
 * Qord, a native Discord client
 * Settings UI of the TextReplace port (upstream: src/plugins/textReplace)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "TextReplace.hpp"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace Qord {
namespace Plugins {

class PluginSettings;

// Editor for one ordered rule list: search, drag to reorder, add, delete,
// and a form for the selected rule.
class TextReplaceRuleList : public QWidget
{
    Q_OBJECT
public:
    TextReplaceRuleList(const QString &title, const QString &description, bool isRegex, QWidget *parent = nullptr);

    void setRules(const QList<TextReplace::Rule> &rules);
    [[nodiscard]] const QList<TextReplace::Rule> &rules() const { return ruleList; }

signals:
    void rulesChanged();

private:
    void rebuildList();
    void applyFilter();
    void showRule(int index);
    void commitField();
    void syncOrderFromList();
    QString displayName(int index) const;

    bool isRegex;
    QList<TextReplace::Rule> ruleList;
    int current = -1;
    bool loading = false;

    QLineEdit *search;
    QListWidget *list;
    QPushButton *addButton;
    QWidget *form;
    QLineEdit *nameEdit;
    QLineEdit *findEdit;
    QLineEdit *replaceEdit;
    QLineEdit *includesEdit;
    QComboBox *scopeBox;
    QLabel *errorLabel;
    QPushButton *deleteButton;
};

class TextReplaceSettingsWidget : public QWidget
{
    Q_OBJECT
public:
    TextReplaceSettingsWidget(PluginSettings &settings, QWidget *parent = nullptr);

private:
    void save();
    void updateTester();

    PluginSettings &settings;
    QLineEdit *testInput;
    QLineEdit *testOutput;
    TextReplaceRuleList *stringList;
    TextReplaceRuleList *regexList;
};

} // namespace Plugins
} // namespace Qord
