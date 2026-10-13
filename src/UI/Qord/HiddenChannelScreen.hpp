#pragma once

// ShowHiddenChannels' lock screen: shown instead of a channel you can't see
// (or a voice channel you can't join): what kind of channel it is, its
// settings, and the roles and users that can see it.

#include "Plugins/Types.hpp"

#include <QScrollArea>

#include <functional>

class QLabel;
class QVBoxLayout;

namespace Acheron {
namespace UI {

class HiddenChannelScreen : public QScrollArea
{
    Q_OBJECT
public:
    // Loads an avatar or role icon; `done` may never run
    using ImageLoader = std::function<void(const QUrl &url, QObject *context, std::function<void(const QPixmap &)> done)>;

    explicit HiddenChannelScreen(QWidget *parent = nullptr);

    void setImageLoader(ImageLoader loader) { loadImage = std::move(loader); }
    void showScreen(const Qord::Plugins::LockScreen &screen);

private:
    QWidget *buildAllowed(const Qord::Plugins::LockScreen &screen, QWidget *parent);

    ImageLoader loadImage;
};

} // namespace UI
} // namespace Acheron
