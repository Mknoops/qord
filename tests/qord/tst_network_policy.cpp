#include "Plugins/Client/NetworkPolicy.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QBuffer>
#include <memory>

using namespace Qord::Plugins;

class TestPluginNetworkPolicy : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qunsetenv("QORD_MOCK_DISCORD");
    }

    void urls()
    {
        QVERIFY(isPluginNetworkUrl(QUrl("https://api.example.test/path")));
        for (const char *url : { "http://api.example.test", "file:///tmp/a", "https:///path",
                                 "https://user:dummy-secret@api.example.test", "/relative" })
            QVERIFY2(!isPluginNetworkUrl(QUrl(QString::fromLatin1(url))), url);
    }

    void responseLimitAcrossChunks()
    {
        PluginResponseBuffer bounded;
        QByteArray bytes("1234");
        QBuffer source(&bytes);
        QVERIFY(source.open(QIODevice::ReadOnly));
        QVERIFY(bounded.read(source, 4));
        QCOMPARE(bounded.data, bytes); // An exact-size body remains usable.
        source.close();
        bytes = "5";
        QVERIFY(source.open(QIODevice::ReadOnly));
        QVERIFY(!bounded.read(source, 4));
        QVERIFY(bounded.exceeded);
        QVERIFY(bounded.data.isEmpty()); // Never deliver a truncated JSON body.
        source.close();
        bytes = "";
        QVERIFY(source.open(QIODevice::ReadOnly));
        QVERIFY(!bounded.read(source, 4)); // Failure remains sticky at finish.

        PluginResponseBuffer single;
        source.close();
        bytes = "12345";
        QVERIFY(source.open(QIODevice::ReadOnly));
        QVERIFY(!single.read(source, 4));
        QVERIFY(single.data.isEmpty());
    }

    void redirects_data()
    {
        QTest::addColumn<bool>("sameOrigin");
        QTest::newRow("same-origin") << true;
        QTest::newRow("different-port") << false;
    }

    void redirects()
    {
        QFETCH(bool, sameOrigin);
        // Test Qt's redirect enforcement on loopback only. Production admission
        // requires HTTPS; HTTP here avoids certificates and external services.
        QTcpServer source, other;
        QVERIFY(source.listen(QHostAddress::LocalHost));
        QVERIFY(other.listen(QHostAddress::LocalHost));
        int sourceRequests = 0;
        bool otherReached = false;
        QByteArray redirectedRequest;
        connect(&other, &QTcpServer::newConnection, this, [&] {
            otherReached = true;
            auto *socket = other.nextPendingConnection();
            socket->close();
        });
        connect(&source, &QTcpServer::newConnection, this, [&] {
            auto *socket = source.nextPendingConnection();
            auto incoming = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket, incoming, responded = false]() mutable {
                if (responded)
                    return;
                *incoming += socket->readAll();
                const int headerEnd = incoming->indexOf("\r\n\r\n");
                // Both 307 requests preserve this fixed dummy POST body.
                if (headerEnd < 0 || incoming->size() < headerEnd + 4 + QByteArray("dummy-body").size())
                    return;
                responded = true;
                ++sourceRequests;
                if (sourceRequests == 1) {
                    const auto port = sameOrigin ? source.serverPort() : other.serverPort();
                    socket->write("HTTP/1.1 307 Temporary Redirect\r\nLocation: http://127.0.0.1:" +
                                  QByteArray::number(port) + "/target\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                } else {
                    redirectedRequest = *incoming;
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");
                }
                socket->disconnectFromHost();
            });
        });
        QNetworkAccessManager manager;
        manager.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        auto request = pluginNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1/start").arg(source.serverPort())));
        request.setRawHeader("X-Api-Key", "dummy-secret");
        auto *reply = manager.post(request, "dummy-body");
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 5000);
        QVERIFY(!otherReached);
        QCOMPARE(sourceRequests, sameOrigin ? 2 : 1);
        if (sameOrigin) {
            QCOMPARE(reply->error(), QNetworkReply::NoError);
            QCOMPARE(reply->readAll(), QByteArray("ok"));
            QVERIFY(redirectedRequest.toLower().contains("x-api-key: dummy-secret"));
            QVERIFY(redirectedRequest.endsWith("dummy-body"));
        } else {
            QCOMPARE(reply->error(), QNetworkReply::InsecureRedirectError);
        }
        delete reply;
    }
};

QTEST_GUILESS_MAIN(TestPluginNetworkPolicy)
#include "tst_network_policy.moc"
