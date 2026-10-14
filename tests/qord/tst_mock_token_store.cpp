#include "Core/Logging.hpp"
#include "Core/TokenStore.hpp"
#include "Qord/DevMock.hpp"

#include <QCoreApplication>
#include <QTest>

Q_LOGGING_CATEGORY(LogCore, "qord.test.mock-keychain")

using Acheron::Core::Snowflake;
using Acheron::Core::TokenStore;

class MockTokenStoreTest : public QObject
{
    Q_OBJECT
private slots:
    void accountOperationsNeverConstructKeychainJobs()
    {
        QVERIFY(Qord::DevMock::enabled());
        const Snowflake id(Qord::DevMock::AccountId);
        const auto saved = TokenStore::saveToken(id, QStringLiteral("dummy-token"));
        QVERIFY(!saved.success());
        QVERIFY(!saved.error.isEmpty());
        const auto loaded = TokenStore::loadToken(id);
        QVERIFY(!loaded.success());
        QVERIFY(!loaded.error.isEmpty());
        QVERIFY(TokenStore::deleteToken(id));
    }

    void secretOperationsNeverConstructKeychainJobs()
    {
        const QString key = QStringLiteral("dummy-plugin-secret");
        QVERIFY(!TokenStore::saveSecret(key, QStringLiteral("dummy-api-key")));
        const auto loaded = TokenStore::loadSecret(key);
        QVERIFY(!loaded.success());
        QVERIFY(!loaded.error.isEmpty());
        QVERIFY(TokenStore::deleteSecret(key));
    }
};

int main(int argc, char **argv)
{
    // Set this before DevMock's cached environment is inspected; no server needed.
    qputenv("QORD_MOCK_DISCORD", "http://127.0.0.1:8765");
    QCoreApplication app(argc, argv);
    MockTokenStoreTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_mock_token_store.moc"
