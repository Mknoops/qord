#pragma once

// Qord: payloads written to the log never carry credentials. Gateway and
// voice gateway payloads are logged at debug level; these helpers replace
// the values of secret keys (tokens, encryption keys, tickets, passwords)
// at any depth before they are.

#include <QByteArray>
#include <QJsonObject>

namespace Acheron {
namespace Core {

// Whether a JSON key holds a secret ("token", "secret_key", ...)
[[nodiscard]] bool isSecretKey(const QString &key);

// A copy with every secret value replaced by "<redacted>"
[[nodiscard]] QJsonObject redactSecrets(const QJsonObject &object);

// The same for serialized JSON, compact. Text that isn't a JSON object is
// not logged at all, only its size.
[[nodiscard]] QByteArray redactSecrets(const QByteArray &json);

} // namespace Core
} // namespace Acheron
