#pragma once

// Qord's keyboard shortcuts: each action has a default key, which the user can
// change or clear in Settings > Keybinds. The main window binds them.

#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QString>

namespace Qord {

class Keybinds : public QObject
{
    Q_OBJECT
public:
    struct Action
    {
        QString id;
        QString name;
        QString description;
        QKeySequence defaultKey;
    };

    static Keybinds &instance();
    static const QList<Action> &actions();

    [[nodiscard]] static QKeySequence key(const QString &id);
    [[nodiscard]] static QKeySequence defaultKey(const QString &id);
    static void setKey(const QString &id, const QKeySequence &key); // empty clears it
    static void reset(const QString &id);
    // The action that already uses `key`, other than `except`
    [[nodiscard]] static QString conflict(const QKeySequence &key, const QString &except);

signals:
    void changed(const QString &id);

private:
    Keybinds() = default;
};

} // namespace Qord
