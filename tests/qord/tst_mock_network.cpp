#include "Core/NetworkAccessManager.hpp"
#include "Core/OpenExternalUrl.hpp"
#include "Qord/DevMock.hpp"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

class TestMockNetwork : public QObject
{
    Q_OBJECT
    QTcpServer server;
private slots:
    void initTestCase()
    {
        QVERIFY(server.listen(QHostAddress::LocalHost));
        qputenv("QORD_MOCK_DISCORD", QByteArray("http://127.0.0.1:") + QByteArray::number(server.serverPort()));
        QVERIFY(Qord::DevMock::enabled());
    }
    void configuration()
    {
        using Qord::DevMock::parseBaseUrl;
        QCOMPARE(parseBaseUrl("http://localhost:8765").host(), QStringLiteral("127.0.0.1"));
        QCOMPARE(parseBaseUrl("http://[::1]:8765").host(), QStringLiteral("::1"));
        for (const char *text : { "http://external.example.test:8765", "https://127.0.0.1:8765",
                                 "http://127.0.0.1", "http://user:secret@127.0.0.1:8765" })
            QVERIFY(parseBaseUrl(QString::fromLatin1(text)).isEmpty());
    }
    void desktopHandlersAreDisabled()
    {
        QVERIFY(!Acheron::Core::openExternalUrl(QUrl("https://external.example.test")));
        QVERIFY(!Acheron::Core::openExternalUrl(QUrl("file:///tmp/mock-document")));
        QVERIFY(!Acheron::Core::openExternalUrl(Qord::DevMock::baseUrl()));
    }
    void rewritesEveryNetworkHost()
    {
        using namespace Qord::DevMock;
        for (const char *text : { "https://unknown.example.test/image", "https://api.github.com/users/a",
                                 "http://192.0.2.1:1234/file", "wss://gateway.example.test/" }) {
            const QUrl result = rewrite(QUrl(QString::fromLatin1(text)));
            QVERIFY(allowsNetworkUrl(result));
            QCOMPARE(result.host(), baseUrl().host());
            QCOMPARE(result.port(), baseUrl().port());
        }
        QVERIFY(!allowsNetworkUrl(QUrl("http://127.0.0.1:1")));
        QVERIFY(!allowsNetworkUrl(QUrl("https://external.example.test")));
        QVERIFY(rewrite(QUrl("file:///tmp/file")).isEmpty());
        QCOMPARE(rewrite(baseUrl()), baseUrl());
    }
    void dispatchOverridesProxyAndRedirects()
    {
        QByteArray received;
        int requests = 0;
        const auto connection = connect(&server, &QTcpServer::newConnection, this, [&] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                received += socket->readAll();
                if (!received.contains("\r\n\r\n") || socket->property("answered").toBool())
                    return;
                socket->setProperty("answered", true);
                ++requests;
                socket->write("HTTP/1.1 302 Found\r\nLocation: https://external.example.test/escape\r\n"
                              "Content-Length: 0\r\nConnection: close\r\n\r\n");
                socket->disconnectFromHost();
            });
        });
        Acheron::Core::NetworkAccessManager manager;
        Acheron::Core::ProxyConfig proxy;
        proxy.type = Acheron::Core::ProxyConfig::Type::Socks5;
        proxy.host = "proxy.example.test";
        proxy.port = 1080;
        manager.configureProxy(proxy);
        QCOMPARE(manager.proxy().type(), QNetworkProxy::NoProxy);
        QNetworkRequest request(QUrl("https://unknown.example.test/image"));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        auto *reply = manager.get(request);
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 3000);
        QCOMPARE(reply->error(), QNetworkReply::NoError);
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 302);
        QCOMPARE(requests, 1);
        QVERIFY(received.startsWith("GET /cdn/unknown.example.test/image "));
        delete reply;
        disconnect(connection);
    }
};

QTEST_GUILESS_MAIN(TestMockNetwork)
#include "tst_mock_network.moc"
