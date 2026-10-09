#pragma once

#include <QHash>
#include <QSet>
#include <QObject>
#include <QPixmap>
#include <QString>

#include "Core/Snowflake.hpp"

class QPainter;

namespace Acheron {
namespace Core {
class Session;
class ClientInstance;
} // namespace Core

namespace UI {

// Icons plugins draw after user names (Hook::UserDecorations), rendered and
// cached for the member list, message headers and profiles. Painting asks
// forUser() every frame, which is a hash lookup after the first call; the
// cache is dropped for users whose presence changes and when plugins change.
class NameDecorations : public QObject
{
    Q_OBJECT
public:
    enum class Place { MemberList, Message, Profile };

    struct Decoration
    {
        QPixmap pixmap; // device pixels; size() is in logical pixels
        QSize size;
        QString tooltip;
    };

    explicit NameDecorations(Core::Session *session, QObject *parent = nullptr);
    ~NameDecorations() override;

    // The window's instance, or null when no plugin can decorate names
    static NameDecorations *instance();

    QList<Decoration> forUser(Core::Snowflake accountId, Core::Snowflake guildId, Core::Snowflake userId, Place place,
                              qreal dpr);

    static constexpr int Spacing = 4;
    // Width of the icons including the gap before each one
    static int width(const QList<Decoration> &decorations);
    // Draws the icons left to right from `x`, centred on `centerY`; returns
    // each icon's rect (for tooltips)
    static QList<QRect> paint(QPainter &painter, const QList<Decoration> &decorations, int x, int centerY);

    void watch(Core::ClientInstance *instance);
    void invalidate();

signals:
    void changed();

private:
    QPixmap render(const QByteArray &svg, const QString &color, QSize size, qreal dpr);

    Core::Session *session;
    QHash<QString, QList<Decoration>> cache; // "account/guild/user/place"
    QHash<QString, QPixmap> pixmaps;
    QSet<quint64> watched;
};

} // namespace UI
} // namespace Acheron
