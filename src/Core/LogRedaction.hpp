#pragma once

// Structured redaction is useful for known schemas, but cannot identify secrets
// in message text or future protocol fields. Gateway logs use metadata only.

#include <QByteArray>
#include <QJsonObject>

namespace Acheron {
namespace Core {

// Only a numeric opcode; never copies server-controlled strings or payload data.
[[nodiscard]] QJsonObject gatewayLogMetadata(const QJsonObject &object);

// Whether a JSON key holds a secret ("token", "secret_key", ...)
[[nodiscard]] bool isSecretKey(const QString &key);

// A copy with every secret value replaced by "<redacted>"
[[nodiscard]] QJsonObject redactSecrets(const QJsonObject &object);

// The same for serialized JSON, compact. Text that isn't a JSON object is
// not logged at all, only its size.
[[nodiscard]] QByteArray redactSecrets(const QByteArray &json);

} // namespace Core
} // namespace Acheron
