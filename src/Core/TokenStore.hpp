#pragma once

#include <QCoreApplication>
#include <QString>

#include "Result.hpp"
#include "Snowflake.hpp"

namespace Acheron {
namespace Core {

class TokenStore
{
    Q_DECLARE_TR_FUNCTIONS(Acheron::Core::TokenStore)

public:
    // Mock mode never accesses the system keychain: reads/writes fail and
    // deletions are successful no-ops. Plugin mock secrets use MemorySecretStore.
    static constexpr char const *SERVICE_NAME = "Acheron";

    static Result<void> saveToken(Snowflake accountId, const QString &token);
    static Result<QString> loadToken(Snowflake accountId);
    static bool deleteToken(Snowflake accountId);

    // Qord: other secrets (a plugin's API key) under their own key. Values
    // are never logged.
    static Result<QString> loadSecret(const QString &key);
    static bool saveSecret(const QString &key, const QString &value);
    static bool deleteSecret(const QString &key);

private:
    static QString keyForAccount(Snowflake accountId);
    static QString describeKeychainFailure(const QString &keychainError);
};

} // namespace Core
} // namespace Acheron
