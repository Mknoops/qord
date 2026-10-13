#pragma once

#include <QHash>
#include <QSet>
#include <QObject>
#include <QPixmap>
#include <QString>

#include "Core/Snowflake.hpp"

class QPainter;

namespace Qord {
namespace Plugins {
struct DecorationContext;
} // namespace Plugins
} // namespace Qord

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
        QString pluginId; // with clickId: clickable
        QString clickId;
        QString copyText; // copied when clicked (BetterRoleDot's role dot)
        [[nodiscard]] bool clickable() const { return !clickId.isEmpty() || !copyText.isEmpty(); }
    };

    explicit NameDecorations(Core::Session *session, QObject *parent = nullptr);
    ~NameDecorations() override;

    // The window's instance, or null when no plugin can decorate names
    static NameDecorations *instance();

    QList<Decoration> forUser(Core::Snowflake accountId, Core::Snowflake guildId, Core::Snowflake userId, Place place,
                              qreal dpr);

    // Tells the plugin that drew `decoration` it was clicked
    void click(Core::Snowflake accountId, Core::Snowflake guildId, Core::Snowflake userId, Place place,
               const Decoration &decoration, bool doubleClick);

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
    void dropUsers(quint64 accountId, const QList<Core::Snowflake> &userIds);
    [[nodiscard]] bool context(Core::Snowflake accountId, Core::Snowflake guildId, Core::Snowflake userId, Place place,
                               Qord::Plugins::DecorationContext &out) const;
    QPixmap render(const QByteArray &svg, const QString &color, QSize size, qreal dpr);

    Core::Session *session;
    QHash<QString, QList<Decoration>> cache; // "account/guild/user/place"
    QHash<QString, QPixmap> pixmaps;
    QSet<quint64> watched;
};

} // namespace UI
} // namespace Acheron
