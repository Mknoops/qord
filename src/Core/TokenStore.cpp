#include "TokenStore.hpp"

#include <QEventLoop>

#if __has_include(<qtkeychain/keychain.h>)
#include <qtkeychain/keychain.h>
#else
#include <qt6keychain/keychain.h>
#endif

#include "Logging.hpp"
#include "Qord/DevMock.hpp"

namespace Acheron {
namespace Core {

QString TokenStore::keyForAccount(Snowflake accountId)
{
    return QString("account_%1_token").arg(accountId.toString());
}

QString TokenStore::describeKeychainFailure(const QString &keychainError)
{
    QString description = tr("The system keychain reported: %1").arg(keychainError);
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    description += "\n\n" + tr("Acheron keeps tokens in the system keyring, which needs a Secret Service provider such as GNOME Keyring, KWallet or KeePassXC to be installed and running.");
#endif
    return description;
}

Result<void> TokenStore::saveToken(Snowflake accountId, const QString &token)
{
    if (Qord::DevMock::enabled())
        return Result<void>::makeError(tr("Account tokens cannot be saved in mock mode."));

    QKeychain::WritePasswordJob job(SERVICE_NAME);
    job.setAutoDelete(false);
    job.setKey(keyForAccount(accountId));
    job.setTextData(token);

    QEventLoop loop;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job.start();
    loop.exec();

    if (job.error() != QKeychain::NoError) {
        qCWarning(LogCore) << "TokenStore: Failed to save token for account"
                           << accountId << "error code" << int(job.error());
        return Result<void>::makeError(describeKeychainFailure(job.errorString()));
    }

    return Result<void>::makeOk();
}

Result<QString> TokenStore::loadToken(Snowflake accountId)
{
    if (Qord::DevMock::enabled())
        return Result<QString>::makeError(tr("Account tokens cannot be loaded in mock mode."));

    QKeychain::ReadPasswordJob job(SERVICE_NAME);
    job.setAutoDelete(false);
    job.setKey(keyForAccount(accountId));

    QEventLoop loop;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job.start();
    loop.exec();

    if (job.error() != QKeychain::NoError && job.error() != QKeychain::EntryNotFound) {
        qCWarning(LogCore) << "TokenStore: Failed to load token for account"
                           << accountId << "error code" << int(job.error());
        return Result<QString>::makeError(describeKeychainFailure(job.errorString()));
    }

    QString token = job.textData();
    if (token.isEmpty())
        return Result<QString>::makeError(tr("No token is stored for this account. Use \"Set Token\" to enter it again."));

    return Result<QString>::makeOk(token);
}

bool TokenStore::deleteToken(Snowflake accountId)
{
    if (Qord::DevMock::enabled())
        return true; // Mock account removal must never delete a real credential.

    QKeychain::DeletePasswordJob job(SERVICE_NAME);
    job.setAutoDelete(false);
    job.setKey(keyForAccount(accountId));

    QEventLoop loop;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job.start();
    loop.exec();

    if (job.error() != QKeychain::NoError && job.error() != QKeychain::EntryNotFound) {
        qCWarning(LogCore) << "TokenStore: Failed to delete token for account"
                           << accountId << "error code" << int(job.error());
        return false;
    }

    return true;
}

Result<QString> TokenStore::loadSecret(const QString &key)
{
    if (Qord::DevMock::enabled())
        return Result<QString>::makeError(tr("Keychain secrets cannot be loaded in mock mode."));

    QKeychain::ReadPasswordJob job(SERVICE_NAME);
    job.setAutoDelete(false);
    job.setKey(key);

    QEventLoop loop;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job.start();
    loop.exec();

    if (job.error() == QKeychain::EntryNotFound)
        return Result<QString>::makeOk(QString());
    if (job.error() != QKeychain::NoError) {
        qCWarning(LogCore) << "TokenStore: Failed to load secret" << "error code" << int(job.error());
        return Result<QString>::makeError(describeKeychainFailure(job.errorString()));
    }
    return Result<QString>::makeOk(job.textData());
}

bool TokenStore::saveSecret(const QString &key, const QString &value)
{
    if (Qord::DevMock::enabled())
        return false;

    QKeychain::WritePasswordJob job(SERVICE_NAME);
    job.setAutoDelete(false);
    job.setKey(key);
    job.setTextData(value);

    QEventLoop loop;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job.start();
    loop.exec();

    if (job.error() != QKeychain::NoError) {
        qCWarning(LogCore) << "TokenStore: Failed to save secret" << "error code" << int(job.error());
        return false;
    }
    return true;
}

bool TokenStore::deleteSecret(const QString &key)
{
    if (Qord::DevMock::enabled())
        return true; // No persistent secrets exist in the mock profile.

    QKeychain::DeletePasswordJob job(SERVICE_NAME);
    job.setAutoDelete(false);
    job.setKey(key);

    QEventLoop loop;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job.start();
    loop.exec();

    if (job.error() != QKeychain::NoError && job.error() != QKeychain::EntryNotFound) {
        qCWarning(LogCore) << "TokenStore: Failed to delete secret" << "error code" << int(job.error());
        return false;
    }
    return true;
}

} // namespace Core
} // namespace Acheron
