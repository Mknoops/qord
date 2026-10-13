#pragma once

#include <QColor>
#include <QPixmap>
#include <QPainter>
#include <QString>

namespace Acheron {
namespace Core {

// Discord's accessibility setting "Role Colors": a member's role color on
// their name (the default), as a dot next to it, or not at all. Applies to
// names in messages and the member list.
enum class RoleStyle { Username, Dot, Hidden };

inline RoleStyle &roleStyle()
{
    static RoleStyle style = RoleStyle::Username;
    return style;
}

inline QString roleStyleKey(RoleStyle style)
{
    switch (style) {
    case RoleStyle::Dot:
        return QStringLiteral("dot");
    case RoleStyle::Hidden:
        return QStringLiteral("hidden");
    case RoleStyle::Username:
        break;
    }
    return QStringLiteral("username");
}

inline RoleStyle roleStyleFromKey(const QString &key)
{
    if (key == QLatin1String("dot"))
        return RoleStyle::Dot;
    if (key == QLatin1String("hidden"))
        return RoleStyle::Hidden;
    return RoleStyle::Username;
}

// Qord: BetterRoleDot. Clicking a role dot copies its color; optionally
// names keep their color next to the dot, and profile roles get clickable
// dots.
struct RoleDotOptions
{
    bool enabled = false; // dots copy their color when clicked
    bool bothStyles = false;
    bool inProfile = false;
};

inline RoleDotOptions &roleDotOptions()
{
    static RoleDotOptions options;
    return options;
}

// Whether names are drawn in their role color, and whether a dot follows
inline bool roleColorOnName()
{
    return roleStyle() == RoleStyle::Username || roleDotOptions().bothStyles;
}
inline bool roleDotAfterName()
{
    return roleStyle() == RoleStyle::Dot || roleDotOptions().bothStyles;
}
// Profile roles' dots can be clicked to copy (BetterRoleDot)
inline bool profileRoleDotsCopy()
{
    const auto &options = roleDotOptions();
    return options.enabled && (roleStyle() == RoleStyle::Dot || options.bothStyles || options.inProfile);
}

constexpr int RoleDotSize = 8;

inline QPixmap roleDotPixmap(const QColor &color, qreal dpr)
{
    QPixmap pixmap(QSize(RoleDotSize, RoleDotSize) * dpr);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(dpr);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QRectF(0, 0, RoleDotSize, RoleDotSize));
    return pixmap;
}

} // namespace Core
} // namespace Acheron
