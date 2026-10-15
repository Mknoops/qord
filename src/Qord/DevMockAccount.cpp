#include "Qord/DevMock.hpp"

#include "Storage/AccountRepository.hpp"

namespace Qord::DevMock {

void installAccount()
{
    if (!enabled())
        return;
    const QUrl base = baseUrl();
    Acheron::Storage::AccountRepository repo;
    Acheron::Core::AccountInfo acc = repo.getAccount(AccountId);
    acc.id = AccountId;
    if (acc.username.isEmpty())
        acc.username = QStringLiteral("qord_tester");
    QUrl gateway = base;
    gateway.setScheme(QStringLiteral("ws"));
    acc.gatewayUrl = gateway.toString() + QStringLiteral("/?encoding=json&v=9&compress=zlib-stream");
    acc.restUrl = base.toString() + QStringLiteral("/api/v9");
    acc.cdnUrl = base.toString() + QStringLiteral("/cdn");
    acc.autoConnect = true;
    const auto saved = repo.saveAccount(acc);
    if (!saved.success())
        qWarning("Could not install the mock account settings");
}

} // namespace Qord::DevMock
