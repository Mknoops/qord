#include "Qord/DevMock.hpp"

#include <QHostAddress>

namespace Qord::DevMock {

static QUrl parseBase()
{
    const QUrl url(qEnvironmentVariable("QORD_MOCK_DISCORD"));
    if (!url.isValid() || url.scheme() != QLatin1String("http") || url.port() <= 0)
        return {};
    const QString host = url.host();
    if (host != QLatin1String("localhost") && !QHostAddress(host).isLoopback())
        return {};
    QUrl base;
    base.setScheme(QStringLiteral("http"));
    base.setHost(host);
    base.setPort(url.port());
    return base;
}

QUrl baseUrl()
{
    static const QUrl base = parseBase();
    return base;
}

bool enabled()
{
    return baseUrl().isValid() && !baseUrl().isEmpty();
}

QString token()
{
    return QStringLiteral("qord-mock-session");
}

QUrl rewrite(const QUrl &url)
{
    if (!enabled())
        return url;
    const QString host = url.host();
    if (host != QLatin1String("cdn.discordapp.com") && host != QLatin1String("media.discordapp.net")
        && host != QLatin1String("discord.com"))
        return url;
    QUrl out = baseUrl();
    out.setPath(QStringLiteral("/cdn/") + host + url.path());
    out.setQuery(url.query());
    return out;
}

} // namespace Qord::DevMock
