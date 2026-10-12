#pragma once

// ServerInfo's window: the server's icon, name, description and banner, its
// details, and tabs of users (friends, mutual users, blocked, ignored).

#include "Plugins/Types.hpp"

#include <QPixmap>
#include <QUrl>

#include <functional>

class QObject;
class QWidget;

namespace Acheron {
namespace UI {
namespace ServerInfoDialog {

struct Actions
{
    // Loads an image; `done` may never run
    std::function<void(const QUrl &url, QObject *context, std::function<void(const QPixmap &)> done)> loadImage;
    std::function<void(const QUrl &url)> openImage; // full size
    std::function<void(Qord::Plugins::Id userId)> openProfile;
};

void show(QWidget *parent, const Qord::Plugins::ServerInfoView &view, const Actions &actions);

} // namespace ServerInfoDialog
} // namespace UI
} // namespace Acheron
