#pragma once

#include "NetworkRequest.hpp"
#include "ProxyConfig.hpp"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>

namespace Acheron::Core {

class BlockedNetworkReply final : public QNetworkReply
{
public:
    BlockedNetworkReply(const QNetworkRequest &request, QObject *parent) : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        setError(QNetworkReply::ProtocolInvalidOperationError, QStringLiteral("Network policy blocked this request"));
        QTimer::singleShot(0, this, [this] {
            setFinished(true);
            emit errorOccurred(error());
            emit finished();
        });
    }
    void abort() override {}
protected:
    qint64 readData(char *, qint64) override { return -1; }
};

// Enforce policy at dispatch, including callers which construct requests directly.
class NetworkAccessManager final : public QNetworkAccessManager
{
public:
    explicit NetworkAccessManager(QObject *parent = nullptr) : QNetworkAccessManager(parent)
    {
        configureProxy({});
    }
    void configureProxy(const ProxyConfig &config)
    {
        const auto next = Qord::DevMock::enabled()
                              ? std::optional<QNetworkProxy>(QNetworkProxy(QNetworkProxy::NoProxy))
                              : config.toQtProxy();
        if (configured && (blocked != !next || (next && proxy() != *next))) {
            // abort() can synchronously deliver callbacks which issue another
            // request. Close the gate before notifying any old request.
            blocked = true;
            QList<QPointer<QNetworkReply>> pending;
            for (auto *reply : findChildren<QNetworkReply *>())
                pending.append(reply);
            for (const auto &reply : pending)
                if (reply)
                    reply->abort();
        }
        configured = true;
        blocked = !next;
        // NoProxy is only bookkeeping while blocked; createRequest refuses all IO.
        setProxy(next.value_or(QNetworkProxy(QNetworkProxy::NoProxy)));
    }
protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &original, QIODevice *outgoing = nullptr) override
    {
        QNetworkRequest request(original);
        if (blocked)
            return new BlockedNetworkReply(request, this);
        if (Qord::DevMock::enabled()) {
            request.setUrl(Qord::DevMock::rewrite(request.url()));
            if (!Qord::DevMock::allowsNetworkUrl(request.url()))
                return new BlockedNetworkReply(request, this);
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
            setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        } else if (!request.url().isValid() || request.url().host().isEmpty() ||
                   (request.url().scheme() != QLatin1String("https") &&
                    request.url().scheme() != QLatin1String("http"))) {
            // Server-provided image/attachment URLs are never local files or
            // alternate Qt backends. Explicit local previews bypass this API.
            return new BlockedNetworkReply(request, this);
        }
        return QNetworkAccessManager::createRequest(op, request, outgoing);
    }
private:
    bool blocked = false;
    bool configured = false;
};

} // namespace Acheron::Core
