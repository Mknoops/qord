#include "MessageContexts.hpp"

#include "Discord/Entities.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace Qord {
namespace Plugins {

using Acheron::Discord::Message;

namespace {
QString id(Acheron::Core::Snowflake s)
{
    return QString::number(quint64(s));
}

QJsonValue parsedJson(const QString &json)
{
    if (json.isEmpty())
        return QJsonValue();
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (doc.isArray())
        return doc.array();
    if (doc.isObject())
        return doc.object();
    return QJsonValue();
}
} // namespace

MessageContext toMessageContext(const Message &msg, Id accountId, Id guildId, Id parentChannelId, bool withJson)
{
    MessageContext c;
    c.accountId = accountId;
    c.channelId = quint64(msg.channelId.get());
    c.guildId = pluginId(guildId);
    c.parentChannelId = pluginId(parentChannelId);
    c.messageId = quint64(msg.id.get());
    if (msg.author.hasValue()) {
        c.authorId = quint64(msg.author->id.get());
        c.authorIsBot = msg.author->bot.hasValue() && msg.author->bot.get();
    }
    c.isOwn = c.authorId && c.authorId == accountId;
    c.type = msg.type.hasValue() ? int(msg.type.get()) : 0;
    c.flags = msg.flags.hasValue() ? quint64(msg.flags.get()) : 0;
    if (msg.referencedMessage && msg.referencedMessage->author.hasValue())
        c.referencedAuthorId = quint64(msg.referencedMessage->author->id.get());
    c.content = msg.content.hasValue() ? msg.content.get() : QString();
    if (msg.timestamp.hasValue())
        c.timestamp = msg.timestamp.get();
    if (msg.editedTimestamp.hasValue())
        c.editedTimestamp = msg.editedTimestamp.get();
    c.deleted = msg.qordDeleted;
    c.hasEmbeds = msg.embeds.hasValue() && !msg.embeds->isEmpty();
    c.mentionEveryone = msg.mentionEveryone.hasValue() && msg.mentionEveryone.get();
    if (msg.mentions.hasValue()) {
        for (const auto &user : msg.mentions.get())
            if (quint64(user.id.get()) == accountId)
                c.mentionsMe = true;
    }
    if (withJson)
        c.rawJson = messageToJson(msg);
    return c;
}

QString messageToJson(const Message &msg)
{
    QJsonObject o;
    o.insert("id", id(msg.id.get()));
    o.insert("channel_id", id(msg.channelId.get()));
    if (msg.nonce.hasValue())
        o.insert("nonce", msg.nonce.get());
    if (msg.author.hasValue()) {
        const auto &a = msg.author.get();
        QJsonObject author;
        author.insert("id", id(a.id.get()));
        author.insert("username", a.username.get());
        author.insert("global_name", a.globalName.isNull() ? QJsonValue() : QJsonValue(a.globalName.get()));
        author.insert("avatar", a.avatar.isNull() ? QJsonValue() : QJsonValue(a.avatar.get()));
        if (a.bot.hasValue())
            author.insert("bot", a.bot.get());
        o.insert("author", author);
    }
    o.insert("content", msg.content.hasValue() ? msg.content.get() : QString());
    if (msg.timestamp.hasValue())
        o.insert("timestamp", msg.timestamp.get().toString(Qt::ISODateWithMs));
    o.insert("edited_timestamp", msg.editedTimestamp.hasValue() && !msg.editedTimestamp.isNull()
                                         ? QJsonValue(msg.editedTimestamp.get().toString(Qt::ISODateWithMs))
                                         : QJsonValue());
    if (msg.type.hasValue())
        o.insert("type", int(msg.type.get()));
    if (msg.flags.hasValue())
        o.insert("flags", qint64(msg.flags.get()));
    if (msg.attachments.hasValue()) {
        QJsonArray list;
        for (const auto &att : msg.attachments.get()) {
            QJsonObject a;
            a.insert("id", id(att.id.get()));
            a.insert("filename", att.filename.get());
            a.insert("size", qint64(att.size.get()));
            a.insert("url", att.url.get());
            a.insert("proxy_url", att.proxyUrl.get());
            if (att.contentType.hasValue())
                a.insert("content_type", att.contentType.get());
            if (att.width.hasValue())
                a.insert("width", att.width.get());
            if (att.height.hasValue())
                a.insert("height", att.height.get());
            list.append(a);
        }
        o.insert("attachments", list);
    }
    if (msg.mentions.hasValue()) {
        QJsonArray list;
        for (const auto &u : msg.mentions.get())
            list.append(id(u.id.get()));
        o.insert("mentions", list);
    }
    if (msg.mentionRoles.hasValue()) {
        QJsonArray list;
        for (const auto &r : msg.mentionRoles.get())
            list.append(id(r));
        o.insert("mention_roles", list);
    }
    if (msg.mentionEveryone.hasValue())
        o.insert("mention_everyone", msg.mentionEveryone.get());
    if (!msg.embedsJson.isEmpty())
        o.insert("embeds", parsedJson(msg.embedsJson));
    if (!msg.reactionsJson.isEmpty())
        o.insert("reactions", parsedJson(msg.reactionsJson));
    if (!msg.stickersJson.isEmpty())
        o.insert("sticker_items", parsedJson(msg.stickersJson));
    if (msg.messageReference.hasValue()) {
        const auto &r = msg.messageReference.get();
        QJsonObject ref;
        if (r.messageId.hasValue())
            ref.insert("message_id", id(r.messageId.get()));
        if (r.channelId.hasValue())
            ref.insert("channel_id", id(r.channelId.get()));
        if (r.guildId.hasValue())
            ref.insert("guild_id", id(r.guildId.get()));
        if (r.type.hasValue())
            ref.insert("type", int(r.type.get()));
        o.insert("message_reference", ref);
    }
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Indented));
}

} // namespace Plugins
} // namespace Qord
