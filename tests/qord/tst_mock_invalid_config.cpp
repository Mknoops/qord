#include "Core/NetworkAccessManager.hpp"
#include "Qord/DevMock.hpp"
#include <QTest>

class TestInvalidMockConfig : public QObject
{
    Q_OBJECT
private slots:
    void invalidConfigurationDoesNotFallBackToProduction()
    {
        qputenv("QORD_MOCK_DISCORD", "http://external.example.test:8765");
        QVERIFY(Qord::DevMock::enabled());
        QVERIFY(Qord::DevMock::baseUrl().isEmpty());
        QVERIFY(Qord::DevMock::rewrite(QUrl("https://discord.com/api/v9")).isEmpty());
        Acheron::Core::NetworkAccessManager manager;
        auto *reply = manager.get(QNetworkRequest(QUrl("https://discord.com/api/v9")));
        QTRY_VERIFY(reply->isFinished());
        QCOMPARE(reply->error(), QNetworkReply::ProtocolInvalidOperationError);
        delete reply;
    }
};

QTEST_GUILESS_MAIN(TestInvalidMockConfig)
#include "tst_mock_invalid_config.moc"
