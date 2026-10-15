#pragma once

#include "Qord/DevMock.hpp"
#include <QUrl>

namespace Acheron::Discord {

inline QUrl safeGatewayUrl(const QUrl &input)
{
    const QUrl url = Qord::DevMock::rewrite(input);
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty())
        return {};
    if (Qord::DevMock::enabled())
        return url.scheme() == QLatin1String("ws") && Qord::DevMock::allowsNetworkUrl(url) ? url : QUrl();
    return url.scheme() == QLatin1String("wss") ? url : QUrl();
}

} // namespace Acheron::Discord
