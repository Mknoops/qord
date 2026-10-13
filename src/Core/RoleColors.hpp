#pragma once

#include <QColor>

#include <algorithm>
#include <cmath>

namespace Acheron {
namespace Core {

// Qord: RoleColorEverywhere. Where members' role colors are shown besides
// their names in the chat and member list.
struct RoleColorOptions
{
    bool mentions = false; // user mentions in messages
    bool memberListHeaders = false; // the member list's role headers
    bool voiceUsers = false; // people in voice channels, in the channel list
    bool messages = false; // message text, mixed with the normal text color
    int messageSaturation = 30; // percent of the role color in message text
};

inline RoleColorOptions &roleColorOptions()
{
    static RoleColorOptions options;
    return options;
}

// CSS color-mix(in oklab, color percent%, base)
inline QColor mixOklab(const QColor &color, int percent, const QColor &base)
{
    struct Lab
    {
        double l, a, b;
    };
    auto toLinear = [](double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
    auto fromLinear = [](double c) {
        c = std::clamp(c, 0.0, 1.0);
        return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
    };
    auto toLab = [&](const QColor &c) {
        const double r = toLinear(c.redF()), g = toLinear(c.greenF()), b = toLinear(c.blueF());
        const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
        const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
        const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
        return Lab { 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
                     1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                     0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s };
    };
    const double t = std::clamp(percent, 0, 100) / 100.0;
    const Lab x = toLab(color), y = toLab(base);
    const Lab mix { x.l * t + y.l * (1 - t), x.a * t + y.a * (1 - t), x.b * t + y.b * (1 - t) };
    const double l = std::pow(mix.l + 0.3963377774 * mix.a + 0.2158037573 * mix.b, 3);
    const double m = std::pow(mix.l - 0.1055613458 * mix.a - 0.0638541728 * mix.b, 3);
    const double s = std::pow(mix.l - 0.0894841775 * mix.a - 1.2914855480 * mix.b, 3);
    QColor out;
    out.setRgbF(float(fromLinear(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s)),
                float(fromLinear(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s)),
                float(fromLinear(-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s)),
                float(color.alphaF() * t + base.alphaF() * (1 - t)));
    return out;
}

} // namespace Core
} // namespace Acheron
