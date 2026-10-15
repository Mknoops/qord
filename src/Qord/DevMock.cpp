#include "Qord/DevMock.hpp"

#include <QHostAddress>
#include <QStringList>

namespace Qord::DevMock {

QUrl parseBaseUrl(const QString &text)
{
    const QUrl url(text);
    if (!url.isValid() || url.scheme() != QLatin1String("http") || url.port() <= 0 ||
        url.port() > 65535 || !url.userInfo().isEmpty())
        return {};
    const QHostAddress address(url.host() == QLatin1String("localhost") ? QStringLiteral("127.0.0.1") : url.host());
    if (!address.isLoopback())
        return {};
    QUrl base;
    base.setScheme(QStringLiteral("http"));
    base.setHost(address.toString());
    base.setPort(url.port());
    return base;
}

QUrl baseUrl()
{
    static const QUrl base = parseBaseUrl(qEnvironmentVariable("QORD_MOCK_DISCORD"));
    return base;
}

bool enabled()
{
    // Even invalid configuration selects the isolated mock profile and blocks
    // transport, rather than accidentally loading a real account/keychain.
    static const bool requested = qEnvironmentVariableIsSet("QORD_MOCK_DISCORD");
    return requested;
}

bool allowsNetworkUrl(const QUrl &url)
{
    if (!enabled())
        return true;
    const QUrl base = baseUrl();
    return base.isValid() && !base.isEmpty() && url.isValid() && url.userInfo().isEmpty() &&
           (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("ws")) &&
           url.host() == base.host() && url.port() == base.port();
}

QString token()
{
    return QStringLiteral("qord-mock-session");
}

QUrl rewrite(const QUrl &url)
{
    if (!enabled())
        return url;
    if (allowsNetworkUrl(url))
        return url;
    const QString scheme = url.scheme();
    if (!url.isValid() || url.host().isEmpty() ||
        (scheme != QLatin1String("http") && scheme != QLatin1String("https") &&
         scheme != QLatin1String("ws") && scheme != QLatin1String("wss")))
        return {};
    QUrl out = baseUrl();
    if (!out.isValid() || out.isEmpty())
        return {};
    if (scheme == QLatin1String("ws") || scheme == QLatin1String("wss"))
        out.setScheme(QStringLiteral("ws"));
    out.setPath(QStringLiteral("/cdn/") + url.host() + url.path());
    out.setQuery(url.query());
    return out;
}

} // namespace Qord::DevMock
