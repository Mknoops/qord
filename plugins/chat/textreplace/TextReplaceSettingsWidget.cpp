/*
 * Qord, a native Discord client
 * Settings UI of the TextReplace port (upstream: src/plugins/textReplace)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "TextReplaceSettingsWidget.hpp"

#include "Plugins/SettingsWidgets.hpp"
#include "Plugins/Settings.hpp"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QUuid>
#include <QVBoxLayout>

namespace Qord {
namespace Plugins {

using namespace TextReplace;

static bool isBlank(const Rule &r)
{
    return r.name.isEmpty() && r.find.isEmpty() && r.replace.isEmpty() && r.onlyIfIncludes.isEmpty();
}

static bool matchesSearch(const Rule &r, const QString &query)
{
    const QString q = query.trimmed().toLower();
    if (q.isEmpty())
        return true;
    for (const QString &v : { r.name, r.find, r.replace, r.onlyIfIncludes })
        if (v.toLower().contains(q))
            return true;
    return false;
}

TextReplaceRuleList::TextReplaceRuleList(const QString &title, const QString &description, bool isRegex, QWidget *parent)
    : QWidget(parent), isRegex(isRegex)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *box = new QGroupBox(title, this);
    outer->addWidget(box);
    auto *layout = new QVBoxLayout(box);

    auto *desc = new QLabel(description, box);
    desc->setWordWrap(true);
    layout->addWidget(desc);

    search = new QLineEdit(box);
    search->setPlaceholderText(tr("Search for a rule..."));
    search->setClearButtonEnabled(true);
    layout->addWidget(search);

    auto *row = new QHBoxLayout;
    layout->addLayout(row);

    auto *left = new QVBoxLayout;
    list = new QListWidget(box);
    list->setDragDropMode(QAbstractItemView::InternalMove);
    list->setDefaultDropAction(Qt::MoveAction);
    list->setMinimumWidth(180);
    left->addWidget(list, 1);
    addButton = new QPushButton(tr("Add Rule"), box);
    left->addWidget(addButton);
    row->addLayout(left, 1);

    form = new QWidget(box);
    auto *formLayout = new QFormLayout(form);
    nameEdit = new QLineEdit(form);
    nameEdit->setPlaceholderText(tr("An optional name to help you identify this rule."));
    findEdit = new QLineEdit(form);
    findEdit->setPlaceholderText(isRegex ? tr("The regex pattern") : tr("The text to replace"));
    replaceEdit = new QLineEdit(form);
    replaceEdit->setPlaceholderText(tr("The text to replace the found text with"));
    includesEdit = new QLineEdit(form);
    includesEdit->setPlaceholderText(tr("Optionally, only apply this rule if the message includes this text."));
    scopeBox = new QComboBox(form);
    scopeBox->addItem(tr("Apply to your messages (visible to everyone)"), scopeToString(Scope::MyMessages));
    scopeBox->addItem(tr("Apply to others' messages (only visible to you)"), scopeToString(Scope::OthersMessages));
    scopeBox->addItem(tr("Apply to all messages"), scopeToString(Scope::AllMessages));
    errorLabel = new QLabel(form);
    errorLabel->setStyleSheet(QStringLiteral("color: #f23f43;"));
    errorLabel->setWordWrap(true);
    errorLabel->setVisible(false);
    deleteButton = new QPushButton(tr("Delete Rule"), form);
    formLayout->addRow(tr("Name"), nameEdit);
    formLayout->addRow(tr("Find"), findEdit);
    formLayout->addRow(tr("Replace"), replaceEdit);
    formLayout->addRow(tr("Only if includes"), includesEdit);
    formLayout->addRow(tr("Scope"), scopeBox);
    formLayout->addRow(errorLabel);
    formLayout->addRow(deleteButton);
    row->addWidget(form, 2);
    form->setEnabled(false);

    connect(search, &QLineEdit::textChanged, this, &TextReplaceRuleList::applyFilter);
    connect(list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (loading)
            return;
        showRule(row < 0 ? -1 : list->item(row)->data(Qt::UserRole + 1).toInt());
    });
    connect(list->model(), &QAbstractItemModel::rowsMoved, this, &TextReplaceRuleList::syncOrderFromList);
    for (QLineEdit *e : { nameEdit, findEdit, replaceEdit, includesEdit })
        connect(e, &QLineEdit::editingFinished, this, &TextReplaceRuleList::commitField);
    connect(scopeBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        if (!loading)
            commitField();
    });
    connect(findEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (!this->isRegex)
            return;
        const QString error = text.isEmpty() ? QString() : checkRegex(text);
        errorLabel->setText(error);
        errorLabel->setVisible(!error.isEmpty());
    });
    connect(addButton, &QPushButton::clicked, this, [this] {
        search->clear();
        Rule r;
        r.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        ruleList.append(r);
        rebuildList();
        list->setCurrentRow(int(ruleList.size()) - 1);
        findEdit->setFocus();
        emit rulesChanged();
    });
    connect(deleteButton, &QPushButton::clicked, this, [this] {
        if (current < 0 || current >= ruleList.size())
            return;
        ruleList.removeAt(current);
        current = -1;
        rebuildList();
        emit rulesChanged();
    });
}

void TextReplaceRuleList::setRules(const QList<Rule> &rules)
{
    ruleList = rules;
    current = -1;
    rebuildList();
}

QString TextReplaceRuleList::displayName(int index) const
{
    const Rule &r = ruleList[index];
    if (!r.name.isEmpty())
        return r.name;
    if (r.isEmpty())
        return tr("Empty Rule %1").arg(index + 1);
    return tr("Rule %1 - %2").arg(index + 1).arg(r.find);
}

void TextReplaceRuleList::rebuildList()
{
    const int keep = current;
    loading = true;
    list->clear();
    for (int i = 0; i < ruleList.size(); ++i) {
        auto *item = new QListWidgetItem(displayName(i), list);
        item->setData(Qt::UserRole, ruleList[i].id);
        item->setData(Qt::UserRole + 1, i);
    }
    loading = false;
    applyFilter();
    // Upstream: can't add another rule while the last one is still empty
    addButton->setEnabled(ruleList.isEmpty() || !ruleList.last().isEmpty());
    int keepRow = -1;
    for (int row = 0; row < list->count(); ++row)
        if (list->item(row)->data(Qt::UserRole + 1).toInt() == keep)
            keepRow = row;
    if (keepRow >= 0 && list->currentRow() != keepRow)
        list->setCurrentRow(keepRow);
    else
        showRule(keepRow >= 0 ? keep : -1);
}

void TextReplaceRuleList::applyFilter()
{
    const QString q = search->text();
    for (int row = 0; row < list->count(); ++row) {
        const int index = list->item(row)->data(Qt::UserRole + 1).toInt();
        list->item(row)->setHidden(!matchesSearch(ruleList[index], q));
    }
    // Reordering a filtered view would be ambiguous, as upstream
    list->setDragDropMode(q.isEmpty() ? QAbstractItemView::InternalMove : QAbstractItemView::NoDragDrop);
}

void TextReplaceRuleList::showRule(int index)
{
    current = index;
    const bool valid = index >= 0 && index < ruleList.size();
    form->setEnabled(valid);
    loading = true;
    const Rule r = valid ? ruleList[index] : Rule();
    nameEdit->setText(r.name);
    findEdit->setText(r.find);
    replaceEdit->setText(r.replace);
    includesEdit->setText(r.onlyIfIncludes);
    scopeBox->setCurrentIndex(scopeBox->findData(scopeToString(r.scope)));
    loading = false;
}

void TextReplaceRuleList::commitField()
{
    if (loading || current < 0 || current >= ruleList.size())
        return;
    Rule &r = ruleList[current];
    Rule updated = r;
    updated.name = nameEdit->text();
    updated.find = findEdit->text();
    updated.replace = replaceEdit->text();
    updated.onlyIfIncludes = includesEdit->text();
    updated.scope = scopeFromString(scopeBox->currentData().toString());
    if (updated.name == r.name && updated.find == r.find && updated.replace == r.replace &&
        updated.onlyIfIncludes == r.onlyIfIncludes && updated.scope == r.scope)
        return;
    r = updated;
    // Upstream: a rule emptied by editing is removed unless it's the last one
    if (isBlank(r) && current != ruleList.size() - 1) {
        ruleList.removeAt(current);
        current = -1;
        rebuildList();
    } else {
        // Update in place: rebuilding would reset the form and steal focus
        // from the field the user just moved to
        for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->data(Qt::UserRole + 1).toInt() == current)
                list->item(row)->setText(displayName(current));
        addButton->setEnabled(!ruleList.last().isEmpty());
        applyFilter();
    }
    emit rulesChanged();
}

void TextReplaceRuleList::syncOrderFromList()
{
    if (loading)
        return;
    QList<Rule> reordered;
    QString selectedId = current >= 0 && current < ruleList.size() ? ruleList[current].id : QString();
    for (int row = 0; row < list->count(); ++row) {
        const QString id = list->item(row)->data(Qt::UserRole).toString();
        for (const Rule &r : std::as_const(ruleList))
            if (r.id == id) {
                reordered.append(r);
                break;
            }
    }
    if (reordered.size() != ruleList.size())
        return;
    ruleList = reordered;
    current = -1;
    for (int i = 0; i < ruleList.size(); ++i)
        if (ruleList[i].id == selectedId)
            current = i;
    rebuildList();
    emit rulesChanged();
}

TextReplaceSettingsWidget::TextReplaceSettingsWidget(PluginSettings &settings, QWidget *parent)
    : QWidget(parent), settings(settings)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *testerBox = new QGroupBox(tr("Rule Tester"), this);
    auto *testerLayout = new QVBoxLayout(testerBox);
    testInput = new QLineEdit(testerBox);
    testInput->setPlaceholderText(tr("Type a message to test rules on"));
    testOutput = new QLineEdit(testerBox);
    testOutput->setPlaceholderText(tr("Message with rules applied"));
    testOutput->setReadOnly(true);
    testerLayout->addWidget(testInput);
    testerLayout->addWidget(testOutput);
    layout->addWidget(testerBox);

    stringList = new TextReplaceRuleList(
            tr("Simple Replacements"),
            tr("Simple find and replace rules. For example, find 'brb' and replace it with 'be right back'"), false, this);
    regexList = new TextReplaceRuleList(
            tr("Regex Replacements"),
            tr("More powerful replacements using Regular Expressions. This section is for advanced users. If you "
               "don't understand it, just ignore it"),
            true, this);
    layout->addWidget(stringList);
    layout->addWidget(regexList);

    stringList->setRules(rulesFromJson(settings.value(QStringLiteral("stringRules")).toArray()));
    regexList->setRules(rulesFromJson(settings.value(QStringLiteral("regexRules")).toArray()));

    connect(stringList, &TextReplaceRuleList::rulesChanged, this, &TextReplaceSettingsWidget::save);
    connect(regexList, &TextReplaceRuleList::rulesChanged, this, &TextReplaceSettingsWidget::save);
    connect(testInput, &QLineEdit::textChanged, this, &TextReplaceSettingsWidget::updateTester);
}

void TextReplaceSettingsWidget::save()
{
    settings.setValue(QStringLiteral("stringRules"), rulesToJson(stringList->rules()));
    settings.setValue(QStringLiteral("regexRules"), rulesToJson(regexList->rules()));
    updateTester();
}

void TextReplaceSettingsWidget::updateTester()
{
    testOutput->setText(applyRules(testInput->text(), Scope::AllMessages, stringList->rules(), regexList->rules()));
}

void qord_register_textreplace_ui(SettingsWidgetRegistry &registry)
{
    registry.add(QStringLiteral("textreplace"),
                 [](PluginSettings &settings, QWidget *parent) { return new TextReplaceSettingsWidget(settings, parent); });
}

} // namespace Plugins
} // namespace Qord
