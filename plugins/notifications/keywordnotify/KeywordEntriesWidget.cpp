/*
 * Qord, a native Discord client
 * Settings UI of the KeywordNotify port (upstream: components/KeywordEntries.tsx)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "KeywordNotify.hpp"

#include "Plugins/Settings.hpp"
#include "Plugins/SettingsWidgets.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace Qord {
namespace Plugins {

namespace {

QStringList splitIds(const QString &text)
{
    QStringList out;
    for (const QString &part : text.split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts))
        out.append(part.trimmed());
    return out;
}

// Upstream's KeywordEntries: each entry's pattern, Ignore Case, Ignore Bots,
// whitelist and blacklist (ids of users, channels or servers) and which list
// wins, with Remove and "Add Keyword Entry"
class KeywordEntriesWidget : public QWidget
{
public:
    KeywordEntriesWidget(PluginSettings &settings, QWidget *parent) : QWidget(parent), settings(settings)
    {
        layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        rebuild();
    }

private:
    void save(const QList<KeywordNotifyPlugin::Entry> &list) { KeywordNotifyPlugin::storeEntries(settings, list); }

    // Changes one entry and saves, without rebuilding (typing keeps focus)
    template<typename F>
    void edit(int index, F change)
    {
        auto list = KeywordNotifyPlugin::entries(settings);
        if (index < 0 || index >= list.size())
            return;
        change(list[index]);
        save(list);
    }

    void rebuild()
    {
        while (QLayoutItem *item = layout->takeAt(0)) {
            if (item->widget())
                item->widget()->deleteLater();
            delete item;
        }
        const auto list = KeywordNotifyPlugin::entries(settings);
        for (int i = 0; i < list.size(); ++i) {
            const auto &e = list[i];
            auto *box = new QGroupBox(tr("Keyword Entry %1").arg(i + 1), this);
            auto *form = new QFormLayout(box);

            auto *patternRow = new QHBoxLayout;
            auto *pattern = new QLineEdit(e.regex, box);
            pattern->setPlaceholderText(QStringLiteral("example|regex"));
            auto *error = new QLabel(box);
            error->setStyleSheet(QStringLiteral("color: #f23f43;"));
            const auto check = [error](const QString &text) {
                const QRegularExpression re(text);
                error->setText(text.isEmpty() || re.isValid() ? QString() : re.errorString());
            };
            check(e.regex);
            connect(pattern, &QLineEdit::textEdited, this, [this, i, check](const QString &text) {
                check(text);
                edit(i, [&](auto &entry) { entry.regex = text; });
            });
            patternRow->addWidget(pattern, 1);
            auto *remove = new QPushButton(tr("Remove"), box);
            connect(remove, &QPushButton::clicked, this, [this, i]() {
                auto current = KeywordNotifyPlugin::entries(settings);
                if (i < current.size())
                    current.removeAt(i);
                save(current);
                QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
            });
            patternRow->addWidget(remove);
            form->addRow(patternRow);
            form->addRow(error);

            auto *ignoreCase = new QCheckBox(tr("Ignore Case"), box);
            ignoreCase->setChecked(e.ignoreCase);
            connect(ignoreCase, &QCheckBox::toggled, this,
                    [this, i](bool on) { edit(i, [&](auto &entry) { entry.ignoreCase = on; }); });
            form->addRow(ignoreCase);
            auto *ignoreBots = new QCheckBox(tr("Ignore Bots"), box);
            ignoreBots->setToolTip(tr("Ignore messages from bots"));
            ignoreBots->setChecked(e.ignoreBots);
            connect(ignoreBots, &QCheckBox::toggled, this,
                    [this, i](bool on) { edit(i, [&](auto &entry) { entry.ignoreBots = on; }); });
            form->addRow(ignoreBots);

            auto *whitelist = new QLineEdit(e.whitelist.join(QStringLiteral(", ")), box);
            whitelist->setPlaceholderText(tr("User, channel or server IDs"));
            connect(whitelist, &QLineEdit::textEdited, this,
                    [this, i](const QString &text) { edit(i, [&](auto &entry) { entry.whitelist = splitIds(text); }); });
            form->addRow(tr("Whitelist"), whitelist);
            auto *blacklist = new QLineEdit(e.blacklist.join(QStringLiteral(", ")), box);
            blacklist->setPlaceholderText(tr("User, channel or server IDs"));
            connect(blacklist, &QLineEdit::textEdited, this,
                    [this, i](const QString &text) { edit(i, [&](auto &entry) { entry.blacklist = splitIds(text); }); });
            form->addRow(tr("Blacklist"), blacklist);

            auto *priority = new QComboBox(box);
            priority->addItem(tr("Blacklist"));
            priority->addItem(tr("Whitelist"));
            priority->setCurrentIndex(e.whitelistFirst ? 1 : 0);
            connect(priority, qOverload<int>(&QComboBox::currentIndexChanged), this,
                    [this, i](int index) { edit(i, [&](auto &entry) { entry.whitelistFirst = index == 1; }); });
            form->addRow(tr("List priority (Advanced)"), priority);
            layout->addWidget(box);
        }
        auto *add = new QPushButton(tr("Add Keyword Entry"), this);
        connect(add, &QPushButton::clicked, this, [this]() {
            auto current = KeywordNotifyPlugin::entries(settings);
            KeywordNotifyPlugin::Entry entry;
            entry.ignoreBots = settings.boolValue(QStringLiteral("ignoreBots"));
            current.append(entry);
            save(current);
            QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
        });
        auto *row = new QHBoxLayout;
        row->addWidget(add);
        row->addStretch();
        layout->addLayout(row);
    }

    PluginSettings &settings;
    QVBoxLayout *layout;
};
} // namespace

void qord_register_keywordnotify_ui(SettingsWidgetRegistry &registry)
{
    registry.add(QStringLiteral("keywordnotify"),
                 [](PluginSettings &settings, QWidget *parent) { return new KeywordEntriesWidget(settings, parent); });
}

} // namespace Plugins
} // namespace Qord
