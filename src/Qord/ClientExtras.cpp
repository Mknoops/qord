// Qord additions to Acheron's Discord::Client and Gateway: core Discord
// features the upstream client did not implement yet. Kept in one file so the
// edits to Acheron's sources stay limited to the declarations.

#include "Discord/Client.hpp"
#include "Discord/Gateway.hpp"

#include "Core/Logging.hpp"
#include "Proto/ProtoWriter.hpp"

#include <QDateTime>
#include <QUrlQuery>
#include <QPointer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>

namespace Acheron {
namespace Discord {

namespace {

Core::Result<QJsonDocument> jsonResult(const HttpResponse &response)
{
    if (!response.success)
        return Core::Result<QJsonDocument>::makeError(response.error.isEmpty()
                                                              ? QStringLiteral("HTTP %1").arg(response.statusCode)
                                                              : response.error,
                                                      response.statusCode);
    return Core::Result<QJsonDocument>::makeOk(QJsonDocument::fromJson(response.body));
}

Core::Result<void> actionResult(const HttpResponse &response)
{
    if (!response.success)
        return Core::Result<void>::makeError(response.error.isEmpty()
                                                     ? QStringLiteral("HTTP %1").arg(response.statusCode)
                                                     : response.error,
                                             response.statusCode);
    return Core::Result<void>::makeOk();
}

QList<Message> parseMessages(const QJsonArray &arr)
{
    QList<Message> out;
    out.reserve(arr.size());
    for (const QJsonValue &v : arr)
        out.append(Message::fromJson(v.toObject()));
    return out;
}

// PreloadedUserSettings { status (11): StatusSettings { status (1): StringValue,
//                                                       custom_status (2): CustomStatus } }
QByteArray statusSettingsProto(const QString &status, const QString &customText, const QString &customEmoji)
{
    Proto::ProtoWriter stringValue;
    stringValue.writeString(1, status);

    Proto::ProtoWriter custom;
    if (!customText.isEmpty())
        custom.writeString(1, customText);
    if (!customEmoji.isEmpty())
        custom.writeString(3, customEmoji);

    Proto::ProtoWriter statusSettings;
    statusSettings.writeBytes(1, stringValue.bytes());
    statusSettings.writeBytes(2, custom.bytes()); // empty message clears the custom status

    Proto::ProtoWriter root;
    root.writeBytes(11, statusSettings.bytes());
    return root.bytes();
}

} // namespace

void Gateway::sendPresenceUpdate(const QString &status, const QJsonArray &activities)
{
    QJsonObject d;
    d["status"] = status;
    d["since"] = 0;
    d["activities"] = activities;
    d["afk"] = false;
    QJsonObject payload;
    payload["op"] = static_cast<int>(OpCode::PRESENCE_UPDATE);
    payload["d"] = d;
    sendPayload(QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

void Client::sendTyping(Snowflake channelId)
{
    httpClient->post("/channels/" + QString::number(channelId) + "/typing", QJsonObject(),
                     [channelId](const HttpResponse &response) {
                         if (!response.success)
                             qCDebug(LogDiscord) << "typing failed for" << channelId << response.statusCode;
                     });
}

void Client::setStatus(const QString &status, const QString &customText, const QString &customEmoji)
{
    QJsonArray activities;
    if (!customText.isEmpty() || !customEmoji.isEmpty()) {
        QJsonObject custom;
        custom["name"] = QStringLiteral("Custom Status");
        custom["type"] = 4;
        if (!customText.isEmpty())
            custom["state"] = customText;
        if (!customEmoji.isEmpty())
            custom["emoji"] = QJsonObject{ { "name", customEmoji } };
        activities.append(custom);
    }
    gateway->sendPresenceUpdate(status, activities);

    QJsonObject payload;
    payload["settings"] = QString::fromLatin1(statusSettingsProto(status, customText, customEmoji).toBase64());
    httpClient->patch("/users/@me/settings-proto/" + QString::number(int(UserSettingsProtoType::PRELOADED)), payload,
                      [](const HttpResponse &response) {
                          if (!response.success)
                              qCWarning(LogDiscord) << "Failed to save status:" << response.statusCode << response.error;
                      });
}

void Client::fetchPinnedMessages(Snowflake channelId, MessagesCallback callback)
{
    QUrlQuery query;
    query.addQueryItem("limit", QStringLiteral("50"));
    httpClient->get("/channels/" + QString::number(channelId) + "/messages/pins", query,
                    [callback](const HttpResponse &response) {
                        if (!response.success) {
                            callback({ {}, response.error });
                            return;
                        }
                        const QJsonDocument doc = QJsonDocument::fromJson(response.body);
                        QList<Message> out;
                        if (doc.isArray()) {
                            out = parseMessages(doc.array()); // legacy /pins shape
                        } else {
                            for (const QJsonValue &item : doc.object().value("items").toArray())
                                out.append(Message::fromJson(item.toObject().value("message").toObject()));
                        }
                        callback({ out });
                    });
}

void Client::searchMessages(Snowflake guildId, Snowflake channelId, const QUrlQuery &query, JsonCallback callback)
{
    const QString endpoint = guildId.isValid()
                                     ? "/guilds/" + QString::number(guildId) + "/messages/search"
                                     : "/channels/" + QString::number(channelId) + "/messages/search";
    httpClient->get(endpoint, query, [callback](const HttpResponse &response) { callback(jsonResult(response)); });
}

void Client::fetchRecentMentions(int limit, bool everyone, MessagesCallback callback)
{
    QUrlQuery query;
    query.addQueryItem("limit", QString::number(limit));
    query.addQueryItem("roles", QStringLiteral("true"));
    query.addQueryItem("everyone", everyone ? QStringLiteral("true") : QStringLiteral("false"));
    httpClient->get("/users/@me/mentions", query, [callback](const HttpResponse &response) {
        if (!response.success) {
            callback({ {}, response.error });
            return;
        }
        callback({ parseMessages(QJsonDocument::fromJson(response.body).array()) });
    });
}

void Client::sendFriendRequest(const QString &username, ActionCallback callback)
{
    QJsonObject body;
    body["username"] = username;
    body["discriminator"] = QJsonValue::Null;
    httpClient->post("/users/@me/relationships", body, ContextProperties::location(QStringLiteral("Add Friend")),
                     [callback](const HttpResponse &response) { callback(actionResult(response)); });
}

void Client::putRelationship(Snowflake userId, int type, ActionCallback callback)
{
    QJsonObject body;
    if (type != 1)
        body["type"] = type;
    httpClient->put("/users/@me/relationships/" + QString::number(userId), body,
                    [callback](const HttpResponse &response) { callback(actionResult(response)); });
}

void Client::removeRelationship(Snowflake userId, ActionCallback callback)
{
    httpClient->delete_("/users/@me/relationships/" + QString::number(userId),
                        [callback](const HttpResponse &response) { callback(actionResult(response)); });
}

void Client::resolveInvite(const QString &code, JsonCallback callback)
{
    QUrlQuery query;
    query.addQueryItem("with_counts", QStringLiteral("true"));
    httpClient->get("/invites/" + QString::fromLatin1(encodeUriComponent(code)), query,
                    [callback](const HttpResponse &response) { callback(jsonResult(response)); });
}

void Client::joinGuildByInvite(const QString &code, JsonCallback callback)
{
    httpClient->post("/invites/" + QString::fromLatin1(encodeUriComponent(code)), QJsonObject(),
                     ContextProperties::location(QStringLiteral("Join Guild")),
                     [callback](const HttpResponse &response) { callback(jsonResult(response)); });
}

void Client::createGuild(const QString &name, JsonCallback callback)
{
    QJsonObject body;
    body["name"] = name;
    body["icon"] = QJsonValue::Null;
    body["channels"] = QJsonArray();
    body["system_channel_id"] = QJsonValue::Null;
    httpClient->post("/guilds", body, [callback](const HttpResponse &response) { callback(jsonResult(response)); });
}

void Client::createGuildChannel(Snowflake guildId, const QString &name, int type, Snowflake parentId,
                                JsonCallback callback)
{
    QJsonObject body;
    body["name"] = name;
    body["type"] = type;
    if (parentId.isValid())
        body["parent_id"] = QString::number(parentId);
    httpClient->post("/guilds/" + QString::number(guildId) + "/channels", body,
                     [callback](const HttpResponse &response) { callback(jsonResult(response)); });
}

void Client::updateGuildNotificationSettings(Snowflake guildId, const QJsonObject &patch, ActionCallback callback)
{
    // "@me" settings (DMs) use guild id 0 in the URL, like the official client
    const QString id = guildId.isValid() ? QString::number(guildId) : QStringLiteral("%40me");
    httpClient->patch("/users/@me/guilds/" + id + "/settings", patch,
                      [callback](const HttpResponse &response) { callback(actionResult(response)); });
}

void Client::setMessageFlags(Snowflake channelId, Snowflake messageId, quint64 flags)
{
    QJsonObject body;
    body["flags"] = static_cast<qint64>(flags);
    httpClient->patch("/channels/" + QString::number(channelId) + "/messages/" + QString::number(messageId), body,
                      [](const HttpResponse &response) {
                          if (!response.success)
                              qCWarning(LogDiscord) << "Failed to change message flags:" << response.statusCode;
                      });
}

void Client::fetchGifs(const QString &query, JsonCallback callback)
{
    QUrlQuery params;
    params.addQueryItem("provider", QStringLiteral("tenor"));
    params.addQueryItem("locale", QLocale().name().replace(QLatin1Char('_'), QLatin1Char('-')));
    params.addQueryItem("media_format", QStringLiteral("mp4"));
    QString endpoint = QStringLiteral("/gifs/trending-gifs");
    if (!query.trimmed().isEmpty()) {
        endpoint = QStringLiteral("/gifs/search");
        params.addQueryItem("q", query.trimmed());
    }
    httpClient->get(endpoint, params, [callback](const HttpResponse &response) { callback(jsonResult(response)); });
}

void Client::sendSticker(Snowflake channelId, Snowflake stickerId, Snowflake replyToMessageId, ActionCallback callback,
                         const QString &content)
{
    QJsonObject payload;
    payload["content"] = content;
    payload["sticker_ids"] = QJsonArray { QString::number(quint64(stickerId)) };
    payload["nonce"] = QString::number(quint64(Core::Snowflake::fromUnixMs(QDateTime::currentMSecsSinceEpoch())));
    payload["tts"] = false;
    payload["flags"] = 0;
    if (replyToMessageId.isValid()) {
        QJsonObject reference;
        reference["message_id"] = QString::number(quint64(replyToMessageId));
        reference["channel_id"] = QString::number(quint64(channelId));
        payload["message_reference"] = reference;
    }
    httpClient->post("/channels/" + QString::number(quint64(channelId)) + "/messages", payload,
                     ContextProperties::location(QStringLiteral("chat_input")),
                     [callback](const HttpResponse &response) { callback(actionResult(response)); });
}

void Client::fetchCommandIndex(Snowflake guildId, Snowflake channelId, JsonCallback callback)
{
    const QString endpoint = guildId.isValid()
                                     ? "/guilds/" + QString::number(quint64(guildId)) + "/application-command-index"
                                     : "/channels/" + QString::number(quint64(channelId)) + "/application-command-index";
    httpClient->get(endpoint, {}, [callback](const HttpResponse &response) { callback(jsonResult(response)); });
}

void Client::sendInteraction(const QJsonObject &payload, ActionCallback callback)
{
    httpClient->post("/interactions", payload, ContextProperties::location(QStringLiteral("slash_ui")),
                     [callback](const HttpResponse &response) { callback(actionResult(response)); });
}

void Client::sendGatewayOp(int op, const Core::OrderedJson &d)
{
    if (gateway)
        gateway->sendOp(op, d);
}

void Client::fetchStickerPacks(ResultCallback<QList<StickerPack>> callback)
{
    if (stickerPacks) {
        callback(Core::Result<QList<StickerPack>>::makeOk(*stickerPacks));
        return;
    }
    const QPointer<Client> self(this);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("locale"), QStringLiteral("en-US"));
    httpClient->get(QStringLiteral("/sticker-packs"), query, [self, callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Fetching sticker packs failed:" << response.error;
            callback(Core::Result<QList<StickerPack>>::makeError(response.error));
            return;
        }
        QList<StickerPack> packs;
        const QJsonArray list =
                QJsonDocument::fromJson(response.body).object().value(QStringLiteral("sticker_packs")).toArray();
        for (const auto &value : list) {
            const QJsonObject obj = value.toObject();
            StickerPack pack;
            pack.id = Snowflake(obj.value(QStringLiteral("id")).toString().toULongLong());
            pack.name = obj.value(QStringLiteral("name")).toString();
            for (const auto &sticker : obj.value(QStringLiteral("stickers")).toArray())
                pack.stickers.append(Sticker::fromJson(sticker.toObject()));
            packs.append(pack);
        }
        if (self)
            self->stickerPacks = packs;
        callback(Core::Result<QList<StickerPack>>::makeOk(packs));
    });
}

void Client::fetchUser(Snowflake userId,
                       std::function<void(const HttpResponse &, const std::optional<User> &)> callback)
{
    httpClient->get(QStringLiteral("/users/%1").arg(quint64(userId)), {}, [callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Fetching a user failed:" << response.statusCode << response.error;
            callback(response, std::nullopt);
            return;
        }
        callback(response, User::fromJson(QJsonDocument::fromJson(response.body).object()));
    });
}

void Client::fetchUserAffinities(
        std::function<void(const HttpResponse &, const QList<QPair<Snowflake, double>> &)> callback)
{
    httpClient->get(QStringLiteral("/users/@me/affinities/v2/users"), {}, [callback](const HttpResponse &response) {
        QList<QPair<Snowflake, double>> out;
        if (!response.success) {
            qCWarning(LogDiscord) << "Fetching user affinities failed:" << response.statusCode << response.error;
            callback(response, out);
            return;
        }
        const QJsonArray list =
                QJsonDocument::fromJson(response.body).object().value(QStringLiteral("user_affinities")).toArray();
        for (const QJsonValue &value : list) {
            const QJsonObject a = value.toObject();
            QString id = a.value(QStringLiteral("other_user_id")).toString();
            if (id.isEmpty())
                id = a.value(QStringLiteral("user_id")).toString();
            if (id.isEmpty())
                continue;
            out.append({ Snowflake(id.toULongLong()), a.value(QStringLiteral("communication_rank")).toDouble() });
        }
        callback(response, out);
    });
}

void Client::fetchReactionUsers(Snowflake channelId, Snowflake messageId, const QString &emoji, int type,
                                ResultCallback<QList<User>> callback)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("100"));
    query.addQueryItem(QStringLiteral("type"), QString::number(type));
    const QString endpoint = QStringLiteral("/channels/%1/messages/%2/reactions/%3")
                                     .arg(quint64(channelId))
                                     .arg(quint64(messageId))
                                     .arg(QString::fromLatin1(QUrl::toPercentEncoding(emoji, ":")));
    httpClient->get(endpoint, query, [callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Fetching reaction users failed:" << response.error;
            callback(Core::Result<QList<User>>::makeError(response.error));
            return;
        }
        QList<User> users;
        for (const auto &u : QJsonDocument::fromJson(response.body).array())
            users.append(User::fromJson(u.toObject()));
        callback(Core::Result<QList<User>>::makeOk(users));
    });
}

void Client::createThreadFromMessage(Snowflake channelId, Snowflake messageId, const QString &name,
                                     ThreadCallback callback)
{
    QJsonObject body;
    body["name"] = name.left(100);
    body["auto_archive_duration"] = 1440; // a day, Discord's default
    const QString endpoint = QStringLiteral("/channels/%1/messages/%2/threads")
                                     .arg(quint64(channelId))
                                     .arg(quint64(messageId));
    httpClient->post(endpoint, body, [callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to create a thread:" << response.error;
            if (callback)
                callback(Core::Result<Channel>::makeError(response.error));
            return;
        }
        if (callback)
            callback(Core::Result<Channel>::makeOk(Channel::fromJson(QJsonDocument::fromJson(response.body).object())));
    });
}

void Client::setSelfVideo(bool on)
{
    if (gateway)
        gateway->setSelfVideo(on);
}

QString Client::gatewaySessionId() const
{
    return gateway ? gateway->currentSessionId() : QString();
}

} // namespace Discord
} // namespace Acheron
