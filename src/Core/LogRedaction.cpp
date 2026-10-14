#include "LogRedaction.hpp"

#include <QJsonArray>
#include <QJsonDocument>

namespace Acheron {
namespace Core {

namespace {

const QString Redacted = QStringLiteral("<redacted>");

QJsonValue redactValue(const QJsonValue &value);

QJsonArray redactArray(const QJsonArray &array)
{
    QJsonArray out;
    for (const QJsonValue &v : array)
        out.append(redactValue(v));
    return out;
}

QJsonValue redactValue(const QJsonValue &value)
{
    if (value.isObject())
        return redactSecrets(value.toObject());
    if (value.isArray())
        return redactArray(value.toArray());
    return value;
}

} // namespace

QJsonObject gatewayLogMetadata(const QJsonObject &object)
{
    return { { QStringLiteral("op"), object.value(QStringLiteral("op")).toInt(-1) } };
}

bool isSecretKey(const QString &key)
{
    // token: account and voice/stream server tokens; secret_key: the voice
    // session's encryption key; analytics_token: READY's; ticket: MFA login
    static const QStringList keys = { QStringLiteral("token"),          QStringLiteral("secret_key"),
                                      QStringLiteral("analytics_token"), QStringLiteral("ticket"),
                                      QStringLiteral("password"),        QStringLiteral("new_password"),
                                      QStringLiteral("authorization") };
    return keys.contains(key, Qt::CaseInsensitive);
}

QJsonObject redactSecrets(const QJsonObject &object)
{
    QJsonObject out;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (isSecretKey(it.key()) && !it.value().isNull())
            out.insert(it.key(), Redacted);
        else
            out.insert(it.key(), redactValue(it.value()));
    }
    return out;
}

QByteArray redactSecrets(const QByteArray &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return QByteArrayLiteral("<payload, ") + QByteArray::number(json.size()) + QByteArrayLiteral(" bytes>");
    return QJsonDocument(redactSecrets(doc.object())).toJson(QJsonDocument::Compact);
}

} // namespace Core
} // namespace Acheron
