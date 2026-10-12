#pragma once

namespace Acheron {
namespace Core {
namespace Audio {

// Qord: user volume sliders go up to 200%, like Discord's desktop app;
// VolumeBooster multiplies that limit.
constexpr int DefaultMaxUserVolume = 200;

inline double &userVolumeMultiplier()
{
    static double multiplier = 1.0;
    return multiplier;
}

inline int maxUserVolume()
{
    return int(DefaultMaxUserVolume * userVolumeMultiplier() + 0.5);
}

} // namespace Audio
} // namespace Core
} // namespace Acheron
