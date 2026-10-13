#pragma once

#include <functional>

namespace Acheron {
namespace Core {

// Qord: ImageZoom. Holding the left button over the image viewer's picture
// shows a magnifying lens; the wheel changes its zoom, Shift+wheel its size.
struct ImageLensOptions
{
    bool enabled = false;
    double zoom = 2.0; // relative to the picture as shown
    int size = 100; // px
    double zoomSpeed = 0.5;
    bool square = false;
    bool nearestNeighbour = false;
    bool invertScroll = true;
    // Called when the wheel changed zoom or size while magnifying
    std::function<void(double zoom, int size)> changed;
};

inline ImageLensOptions &imageLensOptions()
{
    static ImageLensOptions options;
    return options;
}

} // namespace Core
} // namespace Acheron
