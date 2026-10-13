#pragma once

// PermissionsViewer's two views: the "<name> Permissions" dialog (roles,
// members and the owner on the left; what the selected one allows and
// denies on the right) and a member's permissions popout (each permission
// as a chip, with the role granting it in the tooltip).

#include "Plugins/Types.hpp"

#include <QPixmap>
#include <QUrl>

#include <functional>

class QObject;
class QPoint;

namespace Acheron {
namespace UI {

namespace PermissionsView {

// Loads an image (role icon or avatar) for the views; `done` may never run
using ImageLoader = std::function<void(const QUrl &url, QObject *context, std::function<void(const QPixmap &)> done)>;

void showDialog(const Qord::Plugins::PermissionsView &view, const ImageLoader &loadImage);
void showMemberPopout(const Qord::Plugins::MemberPermissionsView &view, const QPoint &globalPos);

} // namespace PermissionsView

} // namespace UI
} // namespace Acheron
