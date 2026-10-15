#include "Storage/AccountRepository.hpp"
#include "Core/TokenStore.hpp"
#include "Core/Logging.hpp"

#include <QHash>
#include <QTemporaryDir>
#include <QTest>
#include <functional>

Q_LOGGING_CATEGORY(LogDB, "qord.test.proxy-storage")

namespace {
QHash<QString, QString> secrets;
bool failRead = false;
bool failWrite = false;
int writes = 0;
std::function<void()> duringWrite;
}

// Link-time fake: this test never links TokenStore.cpp or a real keychain backend.
namespace Acheron::Core {
Result<QString> TokenStore::loadSecret(const QString &key)
{
    return failRead ? Result<QString>::makeError(QStringLiteral("locked"))
                    : Result<QString>::makeOk(secrets.value(key));
}
bool TokenStore::saveSecret(const QString &key, const QString &value)
{
    ++writes;
    if (failWrite)
        return false;
    if (duringWrite) {
        auto callback = std::move(duringWrite);
        duringWrite = {};
        callback();
    }
    secrets.insert(key, value);
    return true;
}
bool TokenStore::deleteSecret(const QString &key) { secrets.remove(key); return true; }
bool TokenStore::deleteToken(Snowflake) { return true; }
}

using namespace Acheron;

class ProxyStorageTest : public QObject
{
    Q_OBJECT
    QTemporaryDir directory;
    QSqlDatabase db;
    Storage::AccountRepository repo;

    Core::AccountInfo account() const
    {
        Core::AccountInfo acc;
        acc.id = Core::Snowflake(123);
        acc.username = QStringLiteral("test-account");
        acc.proxy = *Core::ProxyConfig::parse(QStringLiteral("http://alice:dummy-password@127.0.0.1:9999"));
        return acc;
    }

    QString field(const QString &column)
    {
        QSqlQuery query(db);
        if (!query.exec("SELECT " + column + " FROM accounts WHERE id = 123") || !query.next())
            return {};
        return query.value(0).toString();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(directory.isValid());
        db = QSqlDatabase::addDatabase("QSQLITE", Storage::DatabaseManager::PERSISTENT_CONN_NAME);
        db.setDatabaseName(directory.filePath("dummy.sqlite"));
        QVERIFY(db.open());
    }
    void init()
    {
        secrets.clear();
        failRead = failWrite = false;
        writes = 0;
        duringWrite = {};
        QSqlQuery q(db);
        QVERIFY(q.exec("DROP TABLE IF EXISTS accounts"));
        QVERIFY(q.exec("CREATE TABLE accounts (id INTEGER PRIMARY KEY, username TEXT, display_name TEXT, "
                       "avatar TEXT, gateway_url TEXT, rest_url TEXT, cdn_url TEXT, display_order INTEGER, "
                       "auto_connect INTEGER, proxy_url TEXT, proxy_secret_key TEXT NOT NULL DEFAULT '')"));
    }
    void cleanupTestCase()
    {
        db.close();
        db = {};
        QSqlDatabase::removeDatabase(Storage::DatabaseManager::PERSISTENT_CONN_NAME);
    }
    void storesOnlyCredentialReferenceAndReusesUnchangedSecret()
    {
        const auto acc = account();
        QVERIFY(repo.saveAccount(acc).success());
        QVERIFY(!field("proxy_url").contains(QStringLiteral("dummy-password")));
        const QString key = field("proxy_secret_key");
        QVERIFY(!key.isEmpty());
        QCOMPARE(secrets.value(key), acc.proxy.password);
        QCOMPARE(repo.getAccount(123).proxy.password, acc.proxy.password);
        QVERIFY(repo.saveAccount(acc).success());
        QCOMPARE(writes, 1);
        QCOMPARE(field("proxy_secret_key"), key);
    }
    void newAccountFailsWithoutPlaintextFallback()
    {
        failWrite = true;
        QVERIFY(!repo.saveAccount(account()).success());
        QSqlQuery q(db);
        QVERIFY(q.exec("SELECT COUNT(*) FROM accounts"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
        QVERIFY(secrets.isEmpty());
    }
    void keychainFailurePreservesOldSettings()
    {
        auto acc = account();
        QVERIFY(repo.saveAccount(acc).success());
        const QString key = field("proxy_secret_key");
        failWrite = true;
        acc.proxy.password = QStringLiteral("replacement-dummy");
        QVERIFY(!repo.updateProxy(123, acc.proxy).success());
        QCOMPARE(field("proxy_secret_key"), key);
        QCOMPARE(secrets.value(key), QStringLiteral("dummy-password"));
    }
    void databaseFailurePreservesOldSecretAndRemovesNewSlot()
    {
        auto acc = account();
        QVERIFY(repo.saveAccount(acc).success());
        const QString key = field("proxy_secret_key");
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TRIGGER reject_updates BEFORE UPDATE ON accounts "
                       "BEGIN SELECT RAISE(ABORT, 'simulated write failure'); END"));
        acc.proxy.password = QStringLiteral("replacement-dummy");
        QVERIFY(!repo.updateProxy(123, acc.proxy).success());
        QCOMPARE(field("proxy_secret_key"), key);
        QCOMPARE(secrets.size(), 1);
        QCOMPARE(secrets.value(key), QStringLiteral("dummy-password"));
    }
    void legacyMigrationIsFailClosedAndRecoverable()
    {
        QSqlQuery q(db);
        q.prepare("INSERT INTO accounts(id, proxy_url) VALUES(123, :url)");
        const QString legacy = account().proxy.toString();
        q.bindValue(":url", legacy);
        QVERIFY(q.exec());
        failWrite = true;
        auto loaded = repo.getAccount(123);
        QVERIFY(loaded.proxy.blocked);
        QVERIFY(!loaded.proxyError.isEmpty());
        QVERIFY(loaded.proxy.password.isEmpty());
        QCOMPARE(field("proxy_url"), legacy);
        failWrite = false;
        loaded = repo.getAccount(123);
        QVERIFY(!loaded.proxy.blocked);
        QCOMPARE(loaded.proxy.password, account().proxy.password);
        QVERIFY(!field("proxy_url").contains(QStringLiteral("dummy-password")));
        QVERIFY(!field("proxy_secret_key").isEmpty());
    }
    void legacyMigrationSqlFailurePreservesRecoverableRow()
    {
        QSqlQuery q(db);
        q.prepare("INSERT INTO accounts(id, proxy_url) VALUES(123, :url)");
        const QString legacy = account().proxy.toString();
        q.bindValue(":url", legacy);
        QVERIFY(q.exec());
        QVERIFY(q.exec("CREATE TRIGGER reject_migration BEFORE UPDATE ON accounts "
                       "BEGIN SELECT RAISE(ABORT, 'simulated migration failure'); END"));
        const auto loaded = repo.getAccount(123);
        QVERIFY(loaded.proxy.blocked);
        QCOMPARE(field("proxy_url"), legacy);
        QVERIFY(field("proxy_secret_key").isEmpty());
        QVERIFY(secrets.isEmpty());
    }
    void clearingProxyAndDeletingAccountRemoveOnlyCommittedSecrets()
    {
        QVERIFY(repo.saveAccount(account()).success());
        QVERIFY(repo.updateProxy(123, Core::ProxyConfig{}).success());
        QVERIFY(field("proxy_secret_key").isEmpty());
        QVERIFY(secrets.isEmpty());
        QVERIFY(repo.updateProxy(123, account().proxy).success());
        repo.removeAccount(123);
        QVERIFY(secrets.isEmpty());
    }
    void deletionDuringKeychainWriteDoesNotResurrectAccount()
    {
        auto acc = account();
        QVERIFY(repo.saveAccount(acc).success());
        duringWrite = [this] {
            QSqlQuery q(db);
            QVERIFY(q.exec("DELETE FROM accounts WHERE id = 123"));
        };
        acc.proxy.password = QStringLiteral("replacement-dummy");
        QVERIFY(!repo.saveAccount(acc).success());
        QSqlQuery q(db);
        QVERIFY(q.exec("SELECT COUNT(*) FROM accounts"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 0);
    }
    void migrationDoesNotOverwriteSettingsChangedDuringAnotherKeychainJob()
    {
        QSqlQuery q(db);
        q.prepare("INSERT INTO accounts(id, display_order, proxy_url) VALUES(:id, :order, :url)");
        for (int i = 0; i < 2; ++i) {
            q.bindValue(":id", 123 + i);
            q.bindValue(":order", i);
            q.bindValue(":url", account().proxy.toString());
            QVERIFY(q.exec());
        }
        duringWrite = [this] {
            QSqlQuery changed(db);
            QVERIFY(changed.exec("UPDATE accounts SET proxy_url = 'http://127.0.0.1:7777' WHERE id = 124"));
        };
        const auto accounts = repo.getAllAccounts();
        QCOMPARE(accounts.size(), 2);
        QVERIFY(!accounts[0].proxy.blocked);
        QVERIFY(accounts[1].proxy.blocked);
        QVERIFY(q.exec("SELECT proxy_url, proxy_secret_key FROM accounts WHERE id = 124"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QStringLiteral("http://127.0.0.1:7777"));
        QVERIFY(q.value(1).toString().isEmpty());
    }
    void missingOrLockedCredentialsNeverBecomeDirect()
    {
        QVERIFY(repo.saveAccount(account()).success());
        failRead = true;
        auto loaded = repo.getAccount(123);
        QVERIFY(loaded.proxy.blocked);
        QVERIFY(!loaded.proxyError.isEmpty());
        failRead = false;
        secrets.clear();
        loaded = repo.getAccount(123);
        QVERIFY(loaded.proxy.blocked);
        QVERIFY(!loaded.proxyError.isEmpty());
    }
    void malformedProxyNeverBecomesDirect()
    {
        QSqlQuery q(db);
        QVERIFY(q.exec("INSERT INTO accounts(id, proxy_url) VALUES(123, 'not-a-proxy')"));
        const auto loaded = repo.getAccount(123);
        QVERIFY(loaded.proxy.blocked);
        QVERIFY(!loaded.proxyError.isEmpty());
    }
};

QTEST_GUILESS_MAIN(ProxyStorageTest)
#include "tst_proxy_storage.moc"
