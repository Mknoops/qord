/*
 * Qord, a native Discord client
 * Settings UI of the FavoriteEmojiFirst port (upstream: src/plugins/favEmojiFirst)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "FavoriteEmojiFirst.hpp"

#include "Plugins/Settings.hpp"
#include "Plugins/SettingsWidgets.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace Qord {
namespace Plugins {

namespace {
// Upstream's AliasListSetting and ClearAllAliasesSetting: each alias with
// rename and remove, and "Delete all aliases"
class AliasListWidget : public QWidget
{
public:
    AliasListWidget(PluginSettings &settings, QWidget *parent)
        : QWidget(parent), settings(settings)
    {
        layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        rebuild();
    }

private:
    void rebuild()
    {
        while (QLayoutItem *item = layout->takeAt(0)) {
            if (item->widget())
                item->widget()->deleteLater();
            delete item;
        }
        auto *title = new QLabel(tr("Emoji aliases"), this);
        title->setStyleSheet(QStringLiteral("font-weight: bold;"));
        layout->addWidget(title);
        const auto map = FavoriteEmojiFirstPlugin::aliases(settings);
        if (map.isEmpty())
            layout->addWidget(new QLabel(tr("No aliases set yet. Right-click an emoji in the emoji picker to set one."), this));
        for (auto it = map.begin(); it != map.end(); ++it) {
            auto *row = new QWidget(this);
            auto *rl = new QHBoxLayout(row);
            rl->setContentsMargins(0, 0, 0, 0);
            auto *alias = new QLineEdit(it.key(), row);
            alias->setMaxLength(32);
            alias->setFixedWidth(160);
            rl->addWidget(alias);
            rl->addWidget(new QLabel(QStringLiteral("→ %1").arg(it->displayName()), row), 1);
            auto *error = new QLabel(row);
            error->setStyleSheet(QStringLiteral("color: #f23f43;"));
            rl->addWidget(error);
            auto *remove = new QPushButton(tr("Remove"), row);
            rl->addWidget(remove);
            const QString key = it.key();
            const auto ref = *it;
            connect(alias, &QLineEdit::editingFinished, this, [this, alias, error, key, ref]() {
                const QString next = FavoriteEmojiFirstPlugin::normalizeAlias(alias->text());
                if (next == key)
                    return;
                QString message = FavoriteEmojiFirstPlugin::validationError(next);
                auto current = FavoriteEmojiFirstPlugin::aliases(settings);
                if (message.isEmpty() && current.contains(next))
                    message = tr("Duplicate alias");
                if (!message.isEmpty()) {
                    error->setText(message);
                    return;
                }
                current.remove(key);
                current.insert(next, ref);
                FavoriteEmojiFirstPlugin::storeAliases(settings, current);
                QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
            });
            connect(remove, &QPushButton::clicked, this, [this, key]() {
                auto current = FavoriteEmojiFirstPlugin::aliases(settings);
                current.remove(key);
                FavoriteEmojiFirstPlugin::storeAliases(settings, current);
                QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
            });
            layout->addWidget(row);
        }
        auto *clear = new QPushButton(tr("Delete all aliases"), this);
        clear->setEnabled(!map.isEmpty());
        connect(clear, &QPushButton::clicked, this, [this]() {
            if (QMessageBox::question(this, tr("Delete all aliases"),
                                      tr("Delete every emoji alias? This can't be undone."))
                != QMessageBox::Yes)
                return;
            FavoriteEmojiFirstPlugin::storeAliases(settings, {});
            QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
        });
        auto *row = new QHBoxLayout;
        row->addWidget(clear);
        row->addStretch();
        layout->addLayout(row);
    }

    PluginSettings &settings;
    QVBoxLayout *layout;
};
} // namespace

void qord_register_favoriteemojifirst_ui(SettingsWidgetRegistry &registry)
{
    registry.add(QStringLiteral("favoriteemojifirst"),
                 [](PluginSettings &settings, QWidget *parent) { return new AliasListWidget(settings, parent); });
}

} // namespace Plugins
} // namespace Qord
