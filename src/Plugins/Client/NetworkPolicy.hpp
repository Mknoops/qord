#pragma once

#include "Core/NetworkRequest.hpp"
#include <QIODevice>

namespace Qord::Plugins {

// Consume decoded bytes as they arrive, rather than relying on throttled
// downloadProgress signals (which may describe compressed wire bytes).
struct PluginResponseBuffer
{
    QByteArray data;
    bool exceeded = false;

    bool read(QIODevice &source, qint64 limit)
    {
        if (exceeded)
            return false;
        const qint64 remaining = limit - data.size();
        const QByteArray chunk = source.read(remaining + 1);
        if (chunk.size() > remaining) {
            data.clear();
            exceeded = true;
            return false;
        }
        data.append(chunk);
        return true;
    }
};

inline bool isPluginNetworkUrl(const QUrl &url)
{
    return url.isValid() && url.scheme() == QLatin1String("https") &&
           !url.host().isEmpty() && url.userInfo().isEmpty();
}

inline QNetworkRequest pluginNetworkRequest(const QUrl &url)
{
    QNetworkRequest request = Acheron::Core::networkRequest(url);
    // Headers and request bodies can contain API credentials. TLS alone does
    // not authorize forwarding them to a different host or port.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    request.setMaximumRedirectsAllowed(5);
    request.setTransferTimeout(60'000);
    return request;
}

} // namespace Qord::Plugins
