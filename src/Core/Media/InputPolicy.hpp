#pragma once

#include <QDir>
#include <QUrl>
#include "Qord/DevMock.hpp"

namespace Acheron::Core::Media {

inline bool allowedRemoteMediaUrl(const QUrl &url)
{
    if (!url.isValid() || url.isRelative() || url.host().isEmpty() ||
        !url.userInfo().isEmpty() || url.hasFragment())
        return false;
    if (Qord::DevMock::enabled())
        return url.scheme() == QStringLiteral("http") && Qord::DevMock::allowsNetworkUrl(url);
    return url.scheme() == QStringLiteral("https");
}

// Only explicitly selected/staged files may enter this path. Remote chat
// metadata must pass allowedRemoteMediaUrl at its UI boundary.
inline QString localMediaPath(const QString &input)
{
    const QUrl url(input, QUrl::StrictMode);
    if (url.isLocalFile()) {
        if (!url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment())
            return {};
        return QDir::isAbsolutePath(url.toLocalFile()) ? url.toLocalFile() : QString();
    }
    return QDir::isAbsolutePath(input) && !input.startsWith(QStringLiteral("//")) &&
                   !input.startsWith(QStringLiteral("\\\\")) ? input : QString();
}

inline bool allowedChatMediaUrl(const QUrl &url, bool localPreview = false)
{
    if (url.isLocalFile())
        return localPreview && !localMediaPath(url.toString()).isEmpty();
    return allowedRemoteMediaUrl(Qord::DevMock::rewrite(url));
}

} // namespace Acheron::Core::Media
