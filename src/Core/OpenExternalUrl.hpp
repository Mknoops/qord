#pragma once

#include "Qord/DevMock.hpp"
#include <QDesktopServices>
#include <QUrl>

namespace Acheron::Core {

inline bool openExternalUrl(const QUrl &url)
{
    // Desktop handlers escape our network policy (browser, mail, files, etc.).
    // Mock mode never launches them, even for an apparently local target.
    if (Qord::DevMock::enabled())
        return false;
    return QDesktopServices::openUrl(url);
}

} // namespace Acheron::Core
