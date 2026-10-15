#include "AccountRepository.hpp"

#include "Core/Logging.hpp"
#include "Core/TokenStore.hpp"
#include <QUuid>

namespace Acheron {
namespace Storage {

namespace {
QString proxyKeyPrefix(Core::Snowflake id)
{
    return QStringLiteral("account_%1_proxy_").arg(id.toString());
}

struct PreparedProxy {
    QString url;
    QString key;
    QString oldUrl;
    QString oldKey;
    bool existed = false;
    bool created = false;
};

Core::Result<PreparedProxy> prepareProxy(Core::Snowflake id, const Core::ProxyConfig &proxy, const QString *expectedLegacy = nullptr)
{
    using Result = Core::Result<PreparedProxy>;
    if (proxy.blocked)
        return Result::makeError(QStringLiteral("The proxy is unavailable. Re-enter its settings before saving."));
    if (proxy.enabled() && (proxy.host.isEmpty() || proxy.port == 0))
        return Result::makeError(QStringLiteral("The proxy host and port are required."));
    PreparedProxy out;
    auto publicProxy = proxy;
    publicProxy.password.clear();
    out.url = publicProxy.toString();
    QSqlQuery old(QSqlDatabase::database(DatabaseManager::PERSISTENT_CONN_NAME));
    old.prepare("SELECT proxy_url, proxy_secret_key FROM accounts WHERE id = :id");
    old.bindValue(":id", static_cast<qint64>(id));
    if (!old.exec())
        return Result::makeError(QStringLiteral("Could not read the stored proxy configuration."));
    out.existed = old.next();
    if (out.existed) {
        out.oldUrl = old.value(0).toString();
        out.oldKey = old.value(1).toString();
    }
    old.finish();
    if (expectedLegacy && (!out.existed || out.oldUrl != *expectedLegacy || !out.oldKey.isEmpty()))
        return Result::makeError(QStringLiteral("The proxy settings changed during migration. Retry the connection."));
    if (!out.oldKey.isEmpty() && !out.oldKey.startsWith(proxyKeyPrefix(id)))
        return Result::makeError(QStringLiteral("The stored proxy credential reference is invalid."));
    if (!proxy.password.isEmpty()) {
        if (proxy.username.isEmpty() || !proxy.enabled())
            return Result::makeError(QStringLiteral("A proxy password requires an enabled proxy and username."));
        if (!out.oldKey.isEmpty() && out.oldUrl == out.url) {
            const auto previous = Core::TokenStore::loadSecret(out.oldKey);
            if (!previous.success())
                return Result::makeError(QStringLiteral("Could not unlock the existing proxy password."));
            if (*previous.value == proxy.password) {
                out.key = out.oldKey;
                return Result::makeOk(out);
            }
        }
        // Never overwrite the live slot: SQL failure must leave old credentials usable.
        out.key = proxyKeyPrefix(id) + QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!Core::TokenStore::saveSecret(out.key, proxy.password))
            return Result::makeError(QStringLiteral("Could not save the proxy password in the system keychain. No settings were changed."));
        out.created = true;
    }
    return Result::makeOk(out);
}

void finishProxy(const PreparedProxy &proxy, bool committed)
{
    if (!committed && proxy.created)
        Core::TokenStore::deleteSecret(proxy.key);
    if (committed && !proxy.oldKey.isEmpty() && proxy.oldKey != proxy.key)
        Core::TokenStore::deleteSecret(proxy.oldKey);
    // Cleanup failure can leave an orphan keychain entry, never a plaintext fallback.
}

Core::Result<void> writeProxy(quint64 id, const Core::ProxyConfig &config, const QString *expectedLegacy = nullptr)
{
    const auto prepared = prepareProxy(Core::Snowflake(id), config, expectedLegacy);
    if (!prepared.success())
        return Core::Result<void>::makeError(prepared.error);
    const auto &proxy = *prepared.value;
    QSqlQuery query(QSqlDatabase::database(DatabaseManager::PERSISTENT_CONN_NAME));
    query.prepare("UPDATE accounts SET proxy_url = :url, proxy_secret_key = :key WHERE id = :id "
                  "AND COALESCE(proxy_url, '') = :old_url AND COALESCE(proxy_secret_key, '') = :old_key");
    query.bindValue(":url", proxy.url);
    query.bindValue(":key", proxy.key.isEmpty() ? QStringLiteral("") : proxy.key);
    query.bindValue(":id", static_cast<qint64>(id));
    query.bindValue(":old_url", proxy.oldUrl.isEmpty() ? QStringLiteral("") : proxy.oldUrl);
    query.bindValue(":old_key", proxy.oldKey.isEmpty() ? QStringLiteral("") : proxy.oldKey);
    const bool ok = query.exec() && query.numRowsAffected() == 1;
    finishProxy(proxy, ok);
    return ok ? Core::Result<void>::makeOk()
              : Core::Result<void>::makeError(QStringLiteral("Could not save proxy settings. The previous proxy credentials were retained."));
}

void restoreProxy(Core::AccountInfo &acc, const QString &url, const QString &key)
{
    const auto parsed = Core::ProxyConfig::parse(url);
    auto fail = [&acc](const QString &reason) {
        acc.proxy.password.clear();
        acc.proxy.blocked = true;
        acc.proxyError = reason;
    };
    if (!parsed) {
        fail(QStringLiteral("The stored proxy configuration is invalid. Re-enter the proxy settings."));
        return;
    }
    acc.proxy = *parsed;
    if (!key.isEmpty()) {
        if (!key.startsWith(proxyKeyPrefix(acc.id)) || !acc.proxy.enabled() || acc.proxy.username.isEmpty()) {
            fail(QStringLiteral("The stored proxy credential reference is invalid."));
            return;
        }
        const auto password = Core::TokenStore::loadSecret(key);
        if (!password.success() || password.value->isEmpty()) {
            fail(QStringLiteral("The proxy password could not be loaded from the system keychain. Unlock it or re-enter the proxy settings."));
            return;
        }
        acc.proxy.password = *password.value;
    } else if (!acc.proxy.password.isEmpty()) {
        // Migrate legacy plaintext only after the keychain write succeeds. Failed
        // migration retains the original row for recovery and blocks connection.
        const auto migrated = writeProxy(acc.id, acc.proxy, &url);
        if (!migrated.success())
            fail(migrated.error);
    }
}
} // namespace

Core::Result<void> AccountRepository::saveAccount(const Core::AccountInfo &acc)
{
    const auto prepared = prepareProxy(acc.id, acc.proxy);
    if (!prepared.success())
        return Core::Result<void>::makeError(prepared.error);
    const auto &proxy = *prepared.value;
    QSqlQuery query(QSqlDatabase::database(DatabaseManager::PERSISTENT_CONN_NAME));
    query.prepare(R"(
            INSERT INTO accounts
            (id, username, display_name, avatar, gateway_url, rest_url, cdn_url, display_order, auto_connect, proxy_url, proxy_secret_key)
            SELECT :id, :username, :display_name, :avatar, :gateway_url, :rest_url, :cdn_url, :display_order, :auto_connect, :proxy_url, :proxy_key
            WHERE :existed = 0 OR EXISTS (SELECT 1 FROM accounts WHERE id = :id)
            ON CONFLICT(id) DO UPDATE SET
                username = excluded.username,
                display_name = excluded.display_name,
                avatar = excluded.avatar,
                gateway_url = excluded.gateway_url,
                rest_url = excluded.rest_url,
                cdn_url = excluded.cdn_url,
                display_order = excluded.display_order,
                auto_connect = excluded.auto_connect,
                proxy_url = excluded.proxy_url,
                proxy_secret_key = excluded.proxy_secret_key
            WHERE :existed = 1 AND COALESCE(accounts.proxy_url, '') = :old_url
                AND COALESCE(accounts.proxy_secret_key, '') = :old_key
        )");
    query.bindValue(":id", static_cast<qint64>(acc.id));
    query.bindValue(":username", acc.username);
    query.bindValue(":display_name", acc.displayName);
    query.bindValue(":avatar", acc.avatar);
    query.bindValue(":gateway_url", acc.gatewayUrl);
    query.bindValue(":rest_url", acc.restUrl);
    query.bindValue(":cdn_url", acc.cdnUrl);
    query.bindValue(":display_order", acc.displayOrder);
    query.bindValue(":auto_connect", acc.autoConnect ? 1 : 0);
    query.bindValue(":proxy_url", proxy.url);
    query.bindValue(":proxy_key", proxy.key.isEmpty() ? QStringLiteral("") : proxy.key);
    query.bindValue(":old_url", proxy.oldUrl.isEmpty() ? QStringLiteral("") : proxy.oldUrl);
    query.bindValue(":old_key", proxy.oldKey.isEmpty() ? QStringLiteral("") : proxy.oldKey);
    query.bindValue(":existed", proxy.existed ? 1 : 0);
    const bool ok = query.exec() && query.numRowsAffected() == 1;
    finishProxy(proxy, ok);
    return ok ? Core::Result<void>::makeOk()
              : Core::Result<void>::makeError(QStringLiteral("Could not save account settings. The previous proxy credentials were retained."));
}

Core::AccountInfo AccountRepository::getAccount(quint64 id)
{
    QSqlDatabase db = QSqlDatabase::database(DatabaseManager::PERSISTENT_CONN_NAME);
    QSqlQuery query(db);

    query.prepare("SELECT * FROM accounts WHERE id = :id");

    query.bindValue(":id", static_cast<qint64>(id));

    if (!query.exec()) {
        qCWarning(LogDB) << "AccountRepository: Get failed:" << query.lastError().text();
        return {};
    }

    if (!query.next()) {
        qCWarning(LogDB) << "AccountRepository: Account not found";
        return {};
    }

    Core::AccountInfo acc;

    acc.id = static_cast<Core::Snowflake>(query.value("id").toLongLong());
    acc.username = query.value("username").toString();
    acc.displayName = query.value("display_name").toString();
    acc.avatar = query.value("avatar").toString();
    acc.gatewayUrl = query.value("gateway_url").toString();
    acc.restUrl = query.value("rest_url").toString();
    acc.cdnUrl = query.value("cdn_url").toString();
    acc.displayOrder = query.value("display_order").toInt();
    acc.autoConnect = query.value("auto_connect").toBool();
    const auto proxyUrl = query.value("proxy_url").toString();
    const auto proxyKey = query.value("proxy_secret_key").toString();
    query.finish();
    restoreProxy(acc, proxyUrl, proxyKey);

    return acc;
}

QVector<Core::AccountInfo> AccountRepository::getAllAccounts()
{
    QVector<Core::AccountInfo> results;
    QVector<QPair<QString, QString>> proxies;
    QSqlDatabase db = QSqlDatabase::database(DatabaseManager::PERSISTENT_CONN_NAME);

    if (!db.isOpen())
        return results;

    QSqlQuery query("SELECT * FROM accounts ORDER BY display_order ASC", db);

    while (query.next()) {
        Core::AccountInfo acc;

        acc.id = static_cast<quint64>(query.value("id").toLongLong());

        acc.username = query.value("username").toString();
        acc.displayName = query.value("display_name").toString();
        acc.avatar = query.value("avatar").toString();
        acc.gatewayUrl = query.value("gateway_url").toString();
        acc.restUrl = query.value("rest_url").toString();
        acc.cdnUrl = query.value("cdn_url").toString();
        acc.displayOrder = query.value("display_order").toInt();
        acc.autoConnect = query.value("auto_connect").toBool();
        proxies.append({query.value("proxy_url").toString(), query.value("proxy_secret_key").toString()});

        acc.state = Core::ConnectionState::Disconnected;

        results.append(acc);
    }

    query.finish();
    for (int i = 0; i < results.size(); ++i)
        restoreProxy(results[i], proxies[i].first, proxies[i].second);
    return results;
}

void AccountRepository::removeAccount(quint64 id)
{
    QSqlDatabase db = QSqlDatabase::database(DatabaseManager::PERSISTENT_CONN_NAME);
    QSqlQuery old(db);
    old.prepare("SELECT proxy_secret_key FROM accounts WHERE id = :id");
    old.bindValue(":id", static_cast<qint64>(id));
    if (!old.exec() || !old.next())
        return;
    const QString key = old.value(0).toString();
    old.finish();

    QSqlQuery query(db);
    query.prepare("DELETE FROM accounts WHERE id = :id");
    query.bindValue(":id", static_cast<qint64>(id));
    if (!query.exec()) {
        qCWarning(LogDB) << "AccountRepository: Remove failed:" << query.lastError().text();
        return;
    }
    Core::TokenStore::deleteToken(Core::Snowflake(id));
    if (!key.isEmpty() && key.startsWith(proxyKeyPrefix(Core::Snowflake(id))))
        Core::TokenStore::deleteSecret(key);
}

void AccountRepository::updateDisplayOrder(quint64 id, int order)
{
    QSqlDatabase db = QSqlDatabase::database(DatabaseManager::PERSISTENT_CONN_NAME);
    QSqlQuery query(db);

    query.prepare("UPDATE accounts SET display_order = :order WHERE id = :id");
    query.bindValue(":order", order);
    query.bindValue(":id", static_cast<qint64>(id));

    if (!query.exec())
        qCWarning(LogDB) << "AccountRepository: Update display order failed:"
                         << query.lastError().text();
}

Core::Result<void> AccountRepository::updateProxy(quint64 id, const Core::ProxyConfig &config)
{
    return writeProxy(id, config);
}

std::optional<Discord::HeartbeatSession> AccountRepository::getHeartbeatSession(quint64 id)
{
    QSqlDatabase db = QSqlDatabase::database(DatabaseManager::PERSISTENT_CONN_NAME);
    QSqlQuery query(db);

    query.prepare(R"(
            SELECT heartbeat_session_id, heartbeat_session_created_at, heartbeat_session_last_used_at
            FROM accounts WHERE id = :id
        )");
    query.bindValue(":id", static_cast<qint64>(id));

    if (!query.exec()) {
        qCWarning(LogDB) << "AccountRepository: Get heartbeat session failed:" << query.lastError().text();
        return std::nullopt;
    }

    if (!query.next() || query.value(0).isNull())
        return std::nullopt;

    Discord::HeartbeatSession session;
    session.id = query.value(0).toString();
    session.createdAtMs = query.value(1).toLongLong();
    session.lastUsedAtMs = query.value(2).toLongLong();
    return session;
}

void AccountRepository::updateHeartbeatSession(quint64 id, const Discord::HeartbeatSession &session)
{
    QSqlDatabase db = QSqlDatabase::database(DatabaseManager::PERSISTENT_CONN_NAME);
    QSqlQuery query(db);

    query.prepare(R"(
            UPDATE accounts SET
                heartbeat_session_id = :session_id,
                heartbeat_session_created_at = :created_at,
                heartbeat_session_last_used_at = :last_used_at
            WHERE id = :id
        )");
    query.bindValue(":session_id", session.id);
    query.bindValue(":created_at", session.createdAtMs);
    query.bindValue(":last_used_at", session.lastUsedAtMs);
    query.bindValue(":id", static_cast<qint64>(id));

    if (!query.exec())
        qCWarning(LogDB) << "AccountRepository: Update heartbeat session failed:" << query.lastError().text();
}

void AccountRepository::clearHeartbeatSession(quint64 id)
{
    QSqlDatabase db = QSqlDatabase::database(DatabaseManager::PERSISTENT_CONN_NAME);
    QSqlQuery query(db);

    query.prepare(R"(
            UPDATE accounts SET
                heartbeat_session_id = NULL,
                heartbeat_session_created_at = NULL,
                heartbeat_session_last_used_at = NULL
            WHERE id = :id
        )");
    query.bindValue(":id", static_cast<qint64>(id));

    if (!query.exec())
        qCWarning(LogDB) << "AccountRepository: Clear heartbeat session failed:" << query.lastError().text();
}

void AccountRepository::updateAutoConnect(quint64 id, bool autoConnect)
{
    QSqlDatabase db = QSqlDatabase::database(DatabaseManager::PERSISTENT_CONN_NAME);
    QSqlQuery query(db);

    query.prepare("UPDATE accounts SET auto_connect = :auto_connect WHERE id = :id");
    query.bindValue(":auto_connect", autoConnect ? 1 : 0);
    query.bindValue(":id", static_cast<qint64>(id));

    if (!query.exec())
        qCWarning(LogDB) << "AccountRepository: Update auto_connect failed:"
                         << query.lastError().text();
}

} // namespace Storage
} // namespace Acheron
