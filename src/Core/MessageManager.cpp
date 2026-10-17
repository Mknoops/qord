#include "MessageManager.hpp"
#include "RoleColors.hpp"

#include <QBuffer>
#include <QDateTime>
#include <QPainter>
#include <QSvgRenderer>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QPointer>
#include <QUrl>

#include <functional>
#include <utility>

#include "Discord/Client.hpp"
#include "Emoji/EmojiManager.hpp"
#include "Markdown/Parser.hpp"
#include "Logging.hpp"
#include "UserManager.hpp"

#include "Plugins/Client/MessageContexts.hpp"
#include "Plugins/PluginHost.hpp"

namespace Acheron {
namespace Core {

static QString resolveUserJoinMessage(const Discord::Message &msg, const QString &author)
{
    qint64 ms = msg.timestamp->toMSecsSinceEpoch();
    switch (ms % 13) {
    case 0:
        return author + QStringLiteral(" joined the party.");
    case 1:
        return author + QStringLiteral(" is here.");
    case 2:
        return QStringLiteral("Welcome, ") + author + QStringLiteral(". We hope you brought pizza.");
    case 3:
        return QStringLiteral("A wild ") + author + QStringLiteral(" appeared.");
    case 4:
        return author + QStringLiteral(" just landed.");
    case 5:
        return author + QStringLiteral(" just slid into the server.");
    case 6:
        return author + QStringLiteral(" just showed up!");
    case 7:
        return QStringLiteral("Welcome ") + author + QStringLiteral(". Say hi!");
    case 8:
        return author + QStringLiteral(" hopped into the server.");
    case 9:
        return QStringLiteral("Everyone welcome ") + author + QStringLiteral("!");
    case 10:
        return QStringLiteral("Glad you're here, ") + author + QStringLiteral(".");
    case 11:
        return QStringLiteral("Good to see you, ") + author + QStringLiteral(".");
    case 12:
        return QStringLiteral("Yay you made it, ") + author + QStringLiteral("!");
    default:
        return author + QStringLiteral(" joined the party.");
    }
}

static QString resolveSystemMessageContent(const Discord::Message &msg, const std::function<QString()> &authorName)
{
    switch (static_cast<Discord::MessageType>(msg.type.get())) {
    case Discord::MessageType::CALL:
        return authorName() + QStringLiteral(" started a call.");
    case Discord::MessageType::USER_JOIN:
        return resolveUserJoinMessage(msg, authorName());
    default:
        return msg.content;
    }
}

// Attachments for a message shown before (or instead of) uploading: pasted
// images from memory, picked images from disk, other files by name and size
static QList<Discord::Attachment> localAttachments(const QList<PendingAttachment> &attachments, Snowflake baseId)
{
    QList<Discord::Attachment> out;
    for (int i = 0; i < attachments.size(); i++) {
        const auto &att = attachments[i];
        Discord::Attachment a;
        a.id = Snowflake(baseId + i + 1);
        a.filename = att.filename;
        a.size = att.size;
        a.contentType = att.mimeType;
        if (!att.image.isNull()) {
            a.localPreview = att.image;
            a.width = att.image.width();
            a.height = att.image.height();
        } else if (att.mimeType.startsWith("image/") && !att.filePath.isEmpty()) {
            QString localUrl = QUrl::fromLocalFile(att.filePath).toString();
            a.url = localUrl;
            a.proxyUrl = localUrl;
            QSize dims = QImageReader(att.filePath).size();
            if (dims.isValid()) {
                a.width = dims.width();
                a.height = dims.height();
            }
        }
        if (att.isSpoiler)
            a.flags = Discord::AttachmentFlags(Discord::AttachmentFlag::IS_SPOILER);
        out.append(a);
    }
    return out;
}

MessageManager::MessageManager(Snowflake accountId, Discord::Client *client,
                               UserManager *userManager, QObject *parent)
    : QObject(parent), client(client), userManager(userManager), repo(accountId), parser(std::make_unique<Markdown::Parser>())
{
    messageCache.setMaxCost(1'000);

    parser->setUserResolver([this](Snowflake userId, Snowflake sourceChannelId) {
        // Qord: Discord's name for a user it doesn't know (ValidUser fixes these)
        if (!this->userManager->getUser(userId))
            return QStringLiteral("unknown-user");
        return this->userManager->getDisplayName(userId, this->client->getGuildIdForChannel(sourceChannelId));
    });

    // connect(client, &Discord::Client::messagesReceived, this, &MessageManager::onApiMessagesReceived);
    // connect(client, &Discord::Client::messagesFailed, this, &MessageManager::onMessagesFailed);
}

MessageManager::~MessageManager() {}

void MessageManager::parseMessageContent(Discord::Message &msg)
{
    auto authorName = [&] {
        return userManager->getAuthorDisplayName(msg.author.get(), client->getGuildIdForChannel(msg.channelId));
    };
    QString content = resolveSystemMessageContent(msg, authorName);
    QHash<Qord::Plugins::Id, QString> emojiNotes;
    msg.qordHiddenAttachments.clear();
    msg.qordHiddenEmbeds.clear();
    msg.qordAddedStickers.clear();
    msg.qordAddedStickerNotes.clear();
    msg.qordAddedEmbeds.clear();
    // Plugins may change how received messages look to this user (never the
    // stored message); synthesized system texts are left alone
    auto *plugins = Qord::Plugins::PluginHost::instance();
    if (plugins && plugins->hasHook(Qord::Plugins::Hook::TransformIncomingContent) && msg.content.hasValue() &&
        content == msg.content.get()) {
        Qord::Plugins::IncomingMessage in;
        in.accountId = quint64(client->getMe().id.get());
        in.channelId = Qord::Plugins::pluginId(msg.channelId.get());
        in.guildId = Qord::Plugins::pluginId(client->getGuildIdForChannel(msg.channelId));
        in.messageId = quint64(msg.id.get());
        in.authorId = msg.author.hasValue() ? quint64(msg.author.get().id.get()) : 0;
        in.isOwn = in.authorId && in.authorId == in.accountId;
        in.content = content;
        if (msg.attachments.hasValue())
            for (const auto &att : msg.attachments.get())
                in.attachments.append({ att.url.hasValue() ? att.url.get() : QString(),
                                        att.contentType.hasValue() ? att.contentType.get() : QString() });
        if (msg.embeds.hasValue())
            for (const auto &embed : msg.embeds.get()) {
                QString url = embed.url.hasValue() ? embed.url.get() : QString();
                if (url.isEmpty() && embed.image.hasValue() && embed.image->url.hasValue())
                    url = embed.image->url.get();
                in.embeds.append({ embed.type.hasValue() ? embed.type.get() : QString(), url });
            }
        plugins->dispatchTransformIncomingContent(in);
        content = in.content;
        msg.qordHiddenAttachments = in.hiddenAttachments;
        msg.qordHiddenEmbeds = in.hiddenEmbeds;
        emojiNotes = in.emojiNotes;
        for (const auto &sticker : in.addedStickers) {
            Discord::StickerItem item;
            item.id = Snowflake(sticker.id);
            item.name = sticker.name;
            item.formatType = Discord::StickerFormatType(sticker.formatType);
            msg.qordAddedStickers.append(item);
            msg.qordAddedStickerNotes.append(sticker.note);
        }
    }
    msg.parsedContentCached = inlineHtml(content, msg.channelId);
    for (auto note = emojiNotes.cbegin(); note != emojiNotes.cend(); ++note) {
        // The emoji's title (its :name:) gets the note after it
        const QRegularExpression title(QStringLiteral(R"((<img src="[^"]*/emojis/%1\.[^"]*" alt="[^"]*" title="[^"]*))")
                                               .arg(note.key()));
        msg.parsedContentCached.replace(title, QStringLiteral("\\1 ") + note.value().toHtmlEscaped());
    }

    // Plugins may annotate a message (MessageLogger: deleted, edit history)
    if (plugins && plugins->hasHook(Qord::Plugins::Hook::MessageHistory)) {
        if (auto annotation = plugins->dispatchAnnotateMessage(pluginContext(msg)))
            msg.parsedContentCached = annotatedHtml(*annotation, content, msg.channelId);
    }
    // Qord: shown under the text (Translate)
    if (plugins && plugins->hasHook(Qord::Plugins::Hook::MessageAccessories))
        for (const auto &entry : plugins->dispatchMessageAccessories(pluginContext(msg))) {
            const auto &accessory = entry.accessory;
            if (!accessory.content.isEmpty() || !accessory.footer.isEmpty() || !accessory.links.isEmpty())
                msg.parsedContentCached += accessoryHtml(entry.pluginId, accessory, msg.channelId);
            // Embeds as Discord's rich embeds; images of one embed share its url
            // so the chat groups them (MessageLinkEmbeds)
            int index = 0;
            for (const auto &source : accessory.embeds) {
                const QString groupUrl = QStringLiteral("qord-accessory://%1/%2/%3")
                                                 .arg(entry.pluginId, QString::number(quint64(msg.id.get())))
                                                 .arg(index++);
                Discord::Embed embed;
                embed.type = QStringLiteral("rich");
                embed.url = groupUrl;
                if (!source.description.isEmpty())
                    embed.description = source.description;
                if (source.timestamp.isValid())
                    embed.timestamp = source.timestamp;
                if (source.color)
                    embed.color = int(source.color);
                if (!source.authorName.isEmpty()) {
                    Discord::EmbedAuthor author;
                    author.name = source.authorName;
                    if (!source.authorIconUrl.isEmpty()) {
                        author.iconUrl = source.authorIconUrl;
                        author.proxyIconUrl = source.authorIconUrl;
                    }
                    embed.author = author;
                }
                if (!source.footerText.isEmpty()) {
                    Discord::EmbedFooter footer;
                    footer.text = source.footerText;
                    if (!source.footerIconUrl.isEmpty()) {
                        footer.iconUrl = source.footerIconUrl;
                        footer.proxyIconUrl = source.footerIconUrl;
                    }
                    embed.footer = footer;
                }
                auto image = [&](const auto &img) {
                    Discord::EmbedMedia media;
                    media.url = img.url;
                    media.proxyUrl = img.url;
                    media.width = img.width;
                    media.height = img.height;
                    media.contentType = QStringLiteral("image/png");
                    return media;
                };
                if (!source.images.isEmpty())
                    embed.image = image(source.images.first());
                msg.qordAddedEmbeds.append(embed);
                for (int i = 1; i < std::min<int>(source.images.size(), 4); ++i) {
                    Discord::Embed extra;
                    extra.type = QStringLiteral("image");
                    extra.url = groupUrl;
                    extra.image = image(source.images[i]);
                    msg.qordAddedEmbeds.append(extra);
                }
            }
        }

    // Qord: plugins may treat someone else's message as mentioning you
    msg.qordHighlighted = false;
    if (plugins && plugins->hasHook(Qord::Plugins::Hook::Highlights) && msg.author.hasValue() &&
        msg.author->id.get() != client->getMe().id.get())
        msg.qordHighlighted = plugins->dispatchHighlightMessage(pluginContext(msg, true));

    // Qord: Discord's footer for messages only you can see (slash command
    // answers, PreviewMessage); dismissing one removes it from the chat
    if (msg.flags.hasValue() && msg.flags->testFlag(Discord::MessageFlag::EPHEMERAL))
        msg.parsedContentCached += QStringLiteral("<div style=\"color: #949ba4; font-size: small;\">%1 · "
                                                 "<a href=\"%2\">%3</a></div>")
                                           .arg(tr("Only you can see this"), QLatin1String(DismissMessageHref),
                                                tr("Dismiss message"));

    if (msg.type.hasValue() && msg.type.get() == Discord::MessageType::THREAD_STARTER_MESSAGE &&
        msg.referencedMessage && msg.referencedMessage->content.hasValue() &&
        msg.referencedMessage->parsedContentCached.isEmpty())
        msg.referencedMessage->parsedContentCached =
                inlineHtml(msg.referencedMessage->content.get(), msg.referencedMessage->channelId);

    if (msg.snapshotMessage && msg.snapshotMessage->content.hasValue() &&
        msg.snapshotMessage->parsedContentCached.isEmpty())
        msg.snapshotMessage->parsedContentCached = inlineHtml(msg.snapshotMessage->content.get(), msg.channelId);
}

Qord::Plugins::MessageContext MessageManager::pluginContext(const Discord::Message &msg, bool withJson) const
{
    const Snowflake channelId = msg.channelId.get();
    const Snowflake parent = channelParentResolver ? channelParentResolver(channelId) : Snowflake::Invalid;
    return Qord::Plugins::toMessageContext(msg, quint64(client->getMe().id.get()),
                                           quint64(client->getGuildIdForChannel(channelId)), quint64(parent), withJson);
}

QString MessageManager::annotatedHtml(const Qord::Plugins::MessageAnnotation &annotation, const QString &content,
                                      Snowflake channelId) const
{
    using Kind = Qord::Plugins::DiffPart::Kind;
    auto diffHtml = [&](const QList<Qord::Plugins::DiffPart> &parts) {
        QString out;
        for (const auto &part : parts) {
            const QString html = inlineHtml(part.text, channelId);
            if (part.kind == Kind::Added)
                out += QStringLiteral("<span style=\"background-color: rgba(35, 165, 90, 0.25);\">") + html + QStringLiteral("</span>");
            else if (part.kind == Kind::Removed)
                out += QStringLiteral("<span style=\"background-color: rgba(242, 63, 67, 0.25); text-decoration: line-through;\">") + html +
                       QStringLiteral("</span>");
            else
                out += html;
        }
        return out;
    };

    QString html;
    for (const auto &edit : annotation.history) {
        html += QStringLiteral("<div style=\"color: #80848e;\">");
        html += edit.diff.isEmpty() ? inlineHtml(edit.content, channelId) : diffHtml(edit.diff);
        html += QStringLiteral(" <span style=\"font-size: small;\">(edited)</span></div>");
    }
    html += annotation.separatedDiffs && !annotation.currentDiff.isEmpty() ? diffHtml(annotation.currentDiff)
                                                                          : inlineHtml(content, channelId);
    if (annotation.deleted) {
        if (annotation.deletedStyle == Qord::Plugins::MessageAnnotation::DeletedStyle::Overlay)
            html = QStringLiteral("<div style=\"background-color: rgba(240, 71, 71, 0.15);\">") + html + QStringLiteral("</div>");
        else
            html = QStringLiteral("<div style=\"color: #f23f43;\">") + html + QStringLiteral("</div>");
    }
    return html;
}

QString MessageManager::accessoryHtml(const QString &pluginId, const Qord::Plugins::MessageAccessory &accessory,
                                     Snowflake channelId) const
{
    QString html = QStringLiteral("<div style=\"color: #b5bac1;\">");
    if (!accessory.iconSvg.isEmpty()) {
        // As a PNG in a data URL, which Qt's text layout draws inline
        QByteArray svg = accessory.iconSvg;
        svg.replace("currentColor", "#b5bac1");
        QSvgRenderer renderer(svg);
        if (renderer.isValid()) {
            constexpr int Px = 32; // drawn at 16, sharp at 2x
            QImage icon(Px, Px, QImage::Format_ARGB32_Premultiplied);
            icon.fill(Qt::transparent);
            QPainter painter(&icon);
            renderer.render(&painter);
            painter.end();
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            icon.save(&buffer, "PNG");
            html += QStringLiteral("<img src=\"data:image/png;base64,%1\" width=\"16\" height=\"16\" "
                                   "style=\"vertical-align: middle;\"> ")
                            .arg(QString::fromLatin1(png.toBase64()));
        }
    }
    if (!accessory.content.isEmpty())
        html += inlineHtml(accessory.content, channelId);
    QStringList links;
    for (const auto &link : accessory.links)
        links.append(QStringLiteral("<a href=\"%1%2/%3\">%4</a>")
                             .arg(QLatin1String(Qord::Plugins::MessageAccessoryHref), QString::fromLatin1(QUrl::toPercentEncoding(pluginId)),
                                  QString::fromLatin1(QUrl::toPercentEncoding(link.id)), link.label.toHtmlEscaped()));
    if (!accessory.footer.isEmpty() || !links.isEmpty()) {
        QString footer = accessory.footer.toHtmlEscaped();
        if (!links.isEmpty())
            footer += (footer.isEmpty() ? QString() : QStringLiteral(" - ")) + links.join(QStringLiteral(" - "));
        html += QStringLiteral("<br><span style=\"font-size: small;\">(") + footer + QStringLiteral(")</span>");
    }
    return html + QStringLiteral("</div>");
}

void MessageManager::setMentionColorResolver(std::function<QColor(Snowflake userId, Snowflake guildId)> resolver)
{
    parser->setUserColorResolver([this, resolver = std::move(resolver)](Snowflake userId, Snowflake sourceChannelId) {
        if (!roleColorOptions().mentions || !resolver)
            return QColor();
        const Snowflake guildId = client->getGuildIdForChannel(sourceChannelId);
        return guildId.isValid() ? resolver(userId, guildId) : QColor();
    });
}

void MessageManager::setChannelParentResolver(std::function<Snowflake(Snowflake)> resolver)
{
    channelParentResolver = std::move(resolver);
}

void MessageManager::refreshMessage(Snowflake channelId, Snowflake messageId)
{
    Discord::Message msg;
    if (auto *cached = messageCache.object(messageId))
        msg = *cached;
    else if (auto existing = repo.getMessage(messageId))
        msg = *existing;
    else
        return;
    parseMessageContent(msg);
    messageCache.insert(messageId, new Discord::Message(msg));
    emit messagesReceived({ true, Discord::Client::MessageLoadType::Created, channelId, { msg } });
}

std::optional<Discord::Message> MessageManager::findMessage(Snowflake messageId)
{
    if (auto *cached = messageCache.object(messageId))
        return *cached;
    if (const auto it = linkedMessages.constFind(messageId); it != linkedMessages.constEnd())
        return *it;
    return repo.getMessage(messageId);
}

void MessageManager::fetchLinkedMessage(Snowflake channelId, Snowflake messageId, std::function<void(bool)> done)
{
    QPointer<MessageManager> self(this);
    client->fetchMessagesAround(channelId, messageId, 1, [self, messageId, done](const Result<QList<Discord::Message>> &result) {
        if (!self)
            return;
        bool found = false;
        if (result.success()) {
            for (const auto &message : *result.value) {
                if (message.id.get() != messageId)
                    continue;
                if (message.author.hasValue())
                    self->userManager->saveUser(message.author.get());
                self->linkedMessages.insert(messageId, message);
                found = true;
            }
        }
        if (done)
            done(found);
    });
}

void MessageManager::addLocalMessage(Snowflake channelId, const QString &content,
                                     const QList<PendingAttachment> &attachments)
{
    Discord::Message msg;
    msg.id = Snowflake::generateNonce();
    msg.channelId = channelId;
    msg.content = content;
    msg.timestamp = QDateTime::currentDateTimeUtc();
    msg.author = client->getMe();
    msg.type = Discord::MessageType::DEFAULT;
    msg.flags = Discord::MessageFlags(Discord::MessageFlag::EPHEMERAL);
    msg.attachments = localAttachments(attachments, msg.id);
    msg.isDraftPreview = !attachments.isEmpty();
    msg.embeds = QList<Discord::Embed>();
    parseMessageContent(msg);
    messageCache.insert(msg.id, new Discord::Message(msg));
    emit messagesReceived({ true, Discord::Client::MessageLoadType::Created, channelId, { msg } });
}

void MessageManager::removeKeptMessage(Snowflake channelId, Snowflake messageId)
{
    messageCache.remove(messageId);
    if (segments.contains(channelId))
        segments[channelId].remove(messageId);
    emit messageDeleted(channelId, messageId);
}

static const QSet<QString> OfficialTopicExcludedRules = { QStringLiteral("inlineCode"), QStringLiteral("br") };
static const QSet<QString> OfficialBioExcludedRules = { QStringLiteral("br"), QStringLiteral("user"), QStringLiteral("link") };

static Markdown::ParseState inlineParseState(Snowflake channelId)
{
    Markdown::ParseState state;
    state.isInline = true;
    state.customState["channelId"] = quint64(channelId);
    return state;
}

QString MessageManager::inlineHtml(const QString &content, Snowflake channelId) const
{
    auto ast = parser->parse(content, inlineParseState(channelId));
    return parser->toHtml(ast, Markdown::Parser::isEmojiOnly(ast));
}

QString MessageManager::channelTopicHtml(const QString &topic, Snowflake channelId) const
{
    Markdown::ParseState state = inlineParseState(channelId);
    state.excludedRules = OfficialTopicExcludedRules;
    return parser->toHtml(parser->parse(topic, state));
}

QString MessageManager::profileBioHtml(const QString &bio) const
{
    Markdown::ParseState state = inlineParseState(Snowflake::Invalid);
    state.excludedRules = OfficialBioExcludedRules;
    return parser->toHtml(parser->parse(bio, state));
}

void MessageManager::setRoleResolver(Markdown::RoleResolverFn resolver)
{
    parser->setRoleResolver(std::move(resolver));
}

void MessageManager::setChannelLinkResolver(Markdown::ChannelLinkResolverFn resolver)
{
    parser->setChannelLinkResolver(std::move(resolver));
}

void MessageManager::setChannelResolver(std::function<QString(Snowflake)> resolver)
{
    parser->setChannelResolver([resolver](const QString &channelId) {
        Snowflake id(channelId.toULongLong());
        return resolver(id);
    });
}

void MessageManager::setEmojiManager(EmojiManager *manager)
{
    emojiManager = manager;
}

void MessageManager::requestLoadChannel(Snowflake channelId)
{
    if (pagesInFlight.contains({ channelId, LoadType::Latest }))
        return;

    if (fetchedChannels.contains(channelId)) {
        if (const auto *tail = segments[channelId].tail()) {
            int count = static_cast<int>(tail->ids.size());
            if (auto slice = cachedSlice(channelId, *tail, std::max(0, count - PageSize), count)) {
                emit messagesReceived({ .success = true,
                                        .type = LoadType::Latest,
                                        .channelId = channelId,
                                        .messages = *slice,
                                        .reachedLatest = true });
                return;
            }
        }
    }

    fetchPage(LoadType::Latest, channelId, Snowflake::Invalid, [this, channelId](Discord::Client::MessagesCallback done) {
        client->fetchLatestMessages(channelId, PageSize, std::move(done));
    });
}

void MessageManager::requestLoadHistory(Snowflake channelId, Snowflake beforeId)
{
    if (pagesInFlight.contains({ channelId, LoadType::History }))
        return;

    auto start = channelStartId.constFind(channelId);
    if (start != channelStartId.constEnd() && start.value() >= beforeId) {
        emit messagesReceived({ .success = true,
                                .type = LoadType::History,
                                .channelId = channelId,
                                .anchorId = beforeId });
        return;
    }

    if (auto pos = segments[channelId].find(beforeId); pos.run && pos.index > 0) {
        if (auto slice = cachedSlice(channelId, *pos.run, std::max(0, pos.index - PageSize), pos.index)) {
            emit messagesReceived({ .success = true,
                                    .type = LoadType::History,
                                    .channelId = channelId,
                                    .messages = *slice,
                                    .anchorId = beforeId });
            return;
        }
    }

    fetchPage(LoadType::History, channelId, beforeId, [this, channelId, beforeId](Discord::Client::MessagesCallback done) {
        client->fetchHistory(channelId, beforeId, PageSize, std::move(done));
    });
}

void MessageManager::requestLoadFuture(Snowflake channelId, Snowflake afterId)
{
    if (pagesInFlight.contains({ channelId, LoadType::Future }))
        return;

    if (auto pos = segments[channelId].find(afterId); pos.run) {
        int count = static_cast<int>(pos.run->ids.size());
        bool knowsWhatFollows = pos.index + 1 < count || pos.run->isTail;
        if (knowsWhatFollows) {
            int end = std::min(count, pos.index + 1 + PageSize);
            if (auto slice = cachedSlice(channelId, *pos.run, pos.index + 1, end)) {
                emit messagesReceived({ .success = true,
                                        .type = LoadType::Future,
                                        .channelId = channelId,
                                        .messages = *slice,
                                        .reachedLatest = pos.run->isTail && end == count,
                                        .anchorId = afterId });
                return;
            }
        }
    }

    fetchPage(LoadType::Future, channelId, afterId, [this, channelId, afterId](Discord::Client::MessagesCallback done) {
        client->fetchMessagesAfter(channelId, afterId, PageSize, std::move(done));
    });
}

void MessageManager::requestLoadAround(Snowflake channelId, Snowflake messageId)
{
    if (pagesInFlight.contains({ channelId, LoadType::Jump }))
        return;

    if (auto pos = segments[channelId].find(messageId); pos.run) {
        int count = static_cast<int>(pos.run->ids.size());
        int from = std::max(0, pos.index - JumpWindow / 2);
        int to = std::min(count, from + JumpWindow);
        if (auto slice = cachedSlice(channelId, *pos.run, from, to)) {
            emit messagesReceived({ .success = true,
                                    .type = LoadType::Jump,
                                    .channelId = channelId,
                                    .messages = *slice,
                                    .reachedLatest = pos.run->isTail && to == count });
            return;
        }
    }

    fetchPage(LoadType::Jump, channelId, messageId, [this, channelId, messageId](Discord::Client::MessagesCallback done) {
        client->fetchMessagesAround(channelId, messageId, JumpWindow, std::move(done));
    });
}

void MessageManager::fetchPage(LoadType type, Snowflake channelId, Snowflake anchorId, const PageFetcher &fetch)
{
    pagesInFlight.insert({ channelId, type });

    QPointer<MessageManager> guard = this;
    fetch([this, guard, type, channelId, anchorId](const Result<QList<Discord::Message>> &result) {
        if (!guard)
            return;

        pagesInFlight.remove({ channelId, type });

        if (!result.success()) {
            qCWarning(LogCore) << "Failed to load" << type << "page in" << channelId << result.error;
            emit messagesReceived({ .success = false,
                                    .type = type,
                                    .channelId = channelId,
                                    .anchorId = anchorId });
            return;
        }
        onApiMessagesReceived(result.value.value(), type, channelId, anchorId);
    });
}

std::optional<QList<Discord::Message>> MessageManager::cachedSlice(Snowflake channelId,
                                                                   const MessageSegments::Run &run,
                                                                   int from, int to)
{
    QList<Discord::Message> result;
    if (from >= to)
        return result;

    result.reserve(to - from);
    for (int i = from; i < to; i++) {
        auto *msg = messageCache.object(run.ids[i]);
        if (!msg) {
            result.clear();
            break;
        }
        result.append(*msg);
    }
    if (!result.isEmpty())
        return result;

    QList<Discord::Message> fromDisk = repo.getMessagesInRange(channelId, run.ids[from], run.ids[to - 1]);
    if (fromDisk.isEmpty())
        return std::nullopt;

    for (auto &msg : fromDisk)
        parseMessageContent(msg);
    return fromDisk;
}

static QList<Snowflake> idsOf(const QList<Discord::Message> &messages)
{
    QList<Snowflake> ids;
    ids.reserve(messages.size());
    for (const auto &msg : messages)
        ids.append(msg.id.get());
    return ids;
}

static bool sameNickAvatarAndRoles(const Discord::Member &a, const Discord::Member &b)
{
    return a.nick.valueOr() == b.nick.valueOr() &&
           a.avatar.valueOr() == b.avatar.valueOr() &&
           a.roles.valueOr() == b.roles.valueOr();
}

void MessageManager::cacheGatewayMembers(const Discord::Message &msg)
{
    if (!msg.guildId.hasValue())
        return;
    Snowflake guildId = msg.guildId.get();
    QList<Snowflake> changedUserIds;

    auto cache = [&](const Discord::User &user, Discord::Member member) {
        Snowflake userId = user.id.get();
        auto known = userManager->getMember(guildId, userId);
        if (known && sameNickAvatarAndRoles(*known, member))
            return;
        member.user = user;
        userManager->saveMemberWithUser(guildId, member);
        changedUserIds.append(userId);
    };

    if (msg.member.hasValue() && msg.author.hasValue())
        cache(msg.author.get(), msg.member.get());

    if (msg.mentions.hasValue()) {
        for (const auto &mentioned : msg.mentions.get())
            if (mentioned.member.hasValue())
                cache(mentioned, mentioned.member.get());
    }

    if (!changedUserIds.isEmpty())
        emit membersLearnedFromMessage(guildId, changedUserIds);
}

void MessageManager::onMessageCreated(const Discord::Message &message)
{
    if (message.guildId.hasValue())
        client->registerChannelGuild(message.channelId, message.guildId.get());

    cacheGatewayMembers(message);
    onApiMessagesReceived({ message }, Discord::Client::MessageLoadType::Created,
                          message.channelId);
}

void MessageManager::onMessageUpdated(const Discord::Message &message)
{
    cacheGatewayMembers(message);

    Discord::Message merged;
    bool haveBaseline = false;

    if (auto *cached = messageCache.object(message.id)) {
        merged = *cached;
        haveBaseline = true;
    } else if (auto existing = repo.getMessage(message.id)) {
        merged = *existing;
        haveBaseline = true;
    }

    if (haveBaseline) {
        const Discord::Message before = merged;
        merged.applyUpdate(message);
        // Plugins see the edit before the new version is rendered
        auto *plugins = Qord::Plugins::PluginHost::instance();
        if (plugins && plugins->hasHook(Qord::Plugins::Hook::MessageHistory))
            plugins->dispatchMessageEdited(pluginContext(before), pluginContext(merged));
    } else {
        if (!message.presentKeys.contains(QStringLiteral("content"))) {
            return;
        }
        merged = message;
    }

    parseMessageContent(merged);

    messageCache.insert(merged.id, new Discord::Message(merged));
    repo.updateMessageContent(merged);

    if (message.presentKeys.contains(QStringLiteral("reactions")))
        repo.updateReactionsJson(merged.id, merged.reactionsJson);

    emit messagesReceived({ true, Discord::Client::MessageLoadType::Created, merged.channelId, { merged } });
}

void MessageManager::onMessageDeleted(const Discord::MessageDelete &event)
{
    Snowflake channelId = event.channelId.get();
    Snowflake messageId = event.id.get();

    // Plugins may keep the message on screen, marked deleted
    auto *plugins = Qord::Plugins::PluginHost::instance();
    if (plugins && plugins->hasHook(Qord::Plugins::Hook::MessageHistory)) {
        std::optional<Discord::Message> existing;
        if (auto *cached = messageCache.object(messageId))
            existing = *cached;
        else if (auto stored = repo.getMessage(messageId))
            existing = *stored;
        if (existing && existing->qordDeleted)
            return;
        if (existing && plugins->dispatchMessageDeleted(pluginContext(*existing))) {
            existing->qordDeleted = true;
            // Still recorded as deleted, so it is gone after a restart
            repo.markMessageDeleted(messageId);
            parseMessageContent(*existing);
            messageCache.insert(messageId, new Discord::Message(*existing));
            emit messagesReceived({ true, Discord::Client::MessageLoadType::Created, channelId, { *existing } });
            return;
        }
    }

    messageCache.remove(messageId);

    if (segments.contains(channelId))
        segments[channelId].remove(messageId);

    repo.markMessageDeleted(messageId);

    emit messageDeleted(channelId, messageId);
}

void MessageManager::onMessageSendFailed(const QString &nonce, const QString &error)
{
    qCWarning(LogCore) << "Message send failed for nonce" << nonce << ":" << error;

    pendingFollowUps.remove(nonce); // the rest of a split message is not sent
    emit messageErrored(nonce);
}

// Qord: the next part of a message a plugin split, once the one before it was sent
void MessageManager::onMessageSent(const QString &nonce)
{
    const auto found = pendingFollowUps.constFind(nonce);
    if (found == pendingFollowUps.cend())
        return;
    const Snowflake channelId = found->first;
    QStringList rest = found->second;
    pendingFollowUps.erase(found);
    if (rest.isEmpty())
        return;
    const QString next = rest.takeFirst();
    carriedFollowUps = rest;
    (void)sendMessage(channelId, next, Snowflake::Invalid, {}, true, false);
    carriedFollowUps.clear();
}

void MessageManager::addReaction(Snowflake channelId, Snowflake messageId, const QString &emoji, bool isBurst)
{
    if (emojiManager)
        emojiManager->trackReaction(emoji);
    client->addReaction(channelId, messageId, emoji, isBurst);
}

MessageManager::SendResult MessageManager::sendMessage(Snowflake channelId, const QString &originalContent,
                                                       Snowflake replyToMessageId,
                                                       const QList<PendingAttachment> &attachments,
                                                       bool mentionRepliedUser, bool runPlugins,
                                                       std::function<void(bool sent)> deferredDone,
                                                       const QList<Snowflake> &originalStickerIds)
{
    QString content = originalContent;
    QList<Snowflake> stickerIds = originalStickerIds;
    QStringList followUps = std::exchange(carriedFollowUps, {});
    auto *plugins = Qord::Plugins::PluginHost::instance();
    if (runPlugins && plugins && plugins->hasHook(Qord::Plugins::Hook::BeforeSend)) {
        Qord::Plugins::OutgoingMessage out;
        out.accountId = quint64(client->getMe().id.get());
        out.channelId = Qord::Plugins::pluginId(channelId);
        out.guildId = Qord::Plugins::pluginId(client->getGuildIdForChannel(channelId));
        out.replyToMessageId = quint64(replyToMessageId);
        out.content = content;
        for (Snowflake id : std::as_const(stickerIds))
            out.stickerIds.append(quint64(id));
        plugins->dispatchBeforeSend(out);
        if (out.cancel)
            return SendResult::Cancelled;
        content = out.content;
        followUps = out.followUps;
        stickerIds.clear();
        for (Qord::Plugins::Id id : std::as_const(out.stickerIds))
            stickerIds.append(Snowflake(id));
        if (out.deferred) {
            // Sent once the plugin has the final text (auto-translate)
            QPointer<MessageManager> self(this);
            out.deferred(content, [self, channelId, replyToMessageId, attachments, mentionRepliedUser, followUps,
                                   deferredDone, stickerIds](std::optional<QString> finalContent,
                                                 QList<Qord::Plugins::AttachedFile> files) {
                QList<PendingAttachment> all = attachments;
                for (const auto &file : files) {
                    PendingAttachment added;
                    added.data = file.data;
                    added.filename = file.fileName;
                    added.mimeType = file.mimeType;
                    added.size = file.data.size();
                    if (file.mimeType.startsWith(QLatin1String("image/")))
                        added.image = QImage::fromData(file.data); // the preview
                    all.append(added);
                }
                bool sent = false;
                if (self && finalContent && finalContent->trimmed().isEmpty() && all.isEmpty() && stickerIds.isEmpty())
                    sent = true;
                else if (self && finalContent) {
                    self->carriedFollowUps = followUps;
                    sent = self->sendMessage(channelId, *finalContent, replyToMessageId, all, mentionRepliedUser,
                                             false, {}, stickerIds) == SendResult::Sent;
                    self->carriedFollowUps.clear();
                }
                if (deferredDone)
                    deferredDone(sent);
            });
            return SendResult::Deferred;
        }
        // Nothing left to send (e.g. a rule replaced everything)
        if (content.trimmed().isEmpty() && attachments.isEmpty() && stickerIds.isEmpty())
            return SendResult::Sent;
    }

    // Like Discord's client: a leading "@silent " sends without notifying
    int flags = 0;
    static const QString SilentPrefix = QStringLiteral("@silent ");
    if (content.startsWith(SilentPrefix)) {
        content = content.mid(SilentPrefix.size());
        flags |= int(Discord::MessageFlag::SUPPRESS_NOTIFICATIONS);
    }

    if (emojiManager)
        emojiManager->trackMessageEmojis(content);

    Snowflake nonceId = Snowflake::generateNonce();
    QString nonce = QString::number(nonceId);

    auto outgoing = attachments;

    Discord::Message preview;
    preview.id = nonceId; // temporary id, will be overwritten
    preview.nonce = nonce;
    preview.channelId = channelId;
    preview.content = content;
    preview.timestamp = QDateTime::currentDateTimeUtc();
    preview.author = client->getMe();
    preview.flags = Discord::MessageFlags(flags);
    preview.isPendingOutbound = true;
    if (!stickerIds.isEmpty()) { // Qord: the stickers going with it, where known
        QList<Discord::StickerItem> items;
        for (Snowflake id : std::as_const(stickerIds)) {
            Discord::StickerItem item;
            item.id = id;
            std::optional<Discord::Sticker> known = emojiManager ? emojiManager->stickerById(id) : std::nullopt;
            if (!known)
                for (const auto &pack : client->cachedStickerPacks())
                    for (const Discord::Sticker &sticker : pack.stickers)
                        if (sticker.id.get() == id)
                            known = sticker;
            if (!known)
                continue;
            item.name = known->name.get();
            item.formatType = known->formatType.get();
            items.append(item);
        }
        preview.stickerItems = items;
    }

    if (!outgoing.isEmpty())
        preview.attachments = localAttachments(outgoing, nonceId);

    if (replyToMessageId.isValid()) {
        preview.type = Discord::MessageType::REPLY;
        Discord::MessageReference ref;
        ref.messageId = replyToMessageId;
        ref.channelId = channelId;
        preview.messageReference = ref;
    } else {
        preview.type = Discord::MessageType::DEFAULT;
    }

    auto ast = parser->parse(content, inlineParseState(channelId));
    bool jumbo = Markdown::Parser::isEmojiOnly(ast);
    preview.parsedContentCached = parser->toHtml(ast, jumbo);

    // get our fake preview in
    emit messagesReceived(
            { true, Discord::Client::MessageLoadType::Created, channelId, { preview } });

    for (const auto &att : outgoing) {
        if (att.data.isEmpty() && att.filePath.isEmpty()) {
            emit messageErrored(nonce);
            return SendResult::Sent;
        }
    }

    if (!followUps.isEmpty())
        pendingFollowUps.insert(nonce, { channelId, followUps });
    client->sendMessage(channelId, content, nonce, replyToMessageId, outgoing, mentionRepliedUser, flags, stickerIds);
    return SendResult::Sent;
}

void MessageManager::cancelSend(Snowflake channelId, const QString &nonce)
{
    pendingFollowUps.remove(nonce);
    if (!client->cancelMessageSend(nonce))
        return;

    Snowflake nonceId(nonce.toULongLong());
    messageCache.remove(nonceId);
    if (segments.contains(channelId))
        segments[channelId].remove(nonceId);
    emit messageDeleted(channelId, nonceId);
}

static bool emojisMatch(const Discord::Emoji &a, const Discord::Emoji &b)
{
    if (!a.isUnicode() && !b.isUnicode())
        return a.id.get() == b.id.get();
    if (a.isUnicode() && b.isUnicode())
        return a.name.get() == b.name.get();
    return false;
}

// todo i dont like this here
static QString reactionsToJson(const QList<Discord::Reaction> &reactions)
{
    if (reactions.isEmpty())
        return {};

    QJsonArray arr;
    for (const auto &r : reactions) {
        QJsonObject emojiObj;
        if (!r.emoji->isUnicode())
            emojiObj["id"] = r.emoji->id->toString();
        else
            emojiObj["id"] = QJsonValue::Null;
        emojiObj["name"] = *r.emoji->name;
        if (r.emoji->animated.hasValue())
            emojiObj["animated"] = *r.emoji->animated;

        QJsonObject obj;
        obj["emoji"] = emojiObj;
        obj["count"] = *r.count;
        obj["me"] = *r.me;

        if (r.countDetails.hasValue()) {
            QJsonObject details;
            details["burst"] = *r.countDetails->burst;
            details["normal"] = *r.countDetails->normal;
            obj["count_details"] = details;
        }

        if (r.meBurst.hasValue())
            obj["me_burst"] = *r.meBurst;
        if (r.burstCount.hasValue())
            obj["burst_count"] = *r.burstCount;

        if (r.burstColors.hasValue()) {
            QJsonArray colors;
            for (const auto &c : *r.burstColors)
                colors.append(c);
            obj["burst_colors"] = colors;
        }

        arr.append(obj);
    }

    QJsonDocument doc(arr);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

static void rebuildReactionsJson(Discord::Message &msg)
{
    if (!msg.reactions.hasValue()) {
        msg.reactionsJson.clear();
        return;
    }
    msg.reactionsJson = reactionsToJson(*msg.reactions);
}

static QList<Discord::Reaction> reactionsFromJson(const QString &json)
{
    QList<Discord::Reaction> reactions;
    if (json.isEmpty())
        return reactions;

    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (doc.isArray()) {
        for (const QJsonValue &val : doc.array())
            reactions.append(Discord::Reaction::fromJson(val.toObject()));
    }
    return reactions;
}

void MessageManager::emitReactionUpdate(Discord::Message &msg)
{
    rebuildReactionsJson(msg);
    messageCache.insert(msg.id, new Discord::Message(msg));
    repo.saveMessages({ msg });
    emit messagesReceived({ true, Discord::Client::MessageLoadType::Created, msg.channelId, { msg } });
}

static void applyReactionAdd(QList<Discord::Reaction> &reactions,
                             const Discord::Emoji &emoji, bool isBurst, bool isMe,
                             const QList<QString> &burstColors = {})
{
    bool found = false;
    for (auto &r : reactions) {
        if (emojisMatch(r.emoji, emoji)) {
            r.count = *r.count + 1;
            if (r.countDetails.hasValue()) {
                if (isBurst)
                    r.countDetails->burst = *r.countDetails->burst + 1;
                else
                    r.countDetails->normal = *r.countDetails->normal + 1;
            }
            if (isMe) {
                if (isBurst)
                    r.meBurst = true;
                else
                    r.me = true;
            }
            if (isBurst && !burstColors.isEmpty())
                r.burstColors = burstColors;
            found = true;
            break;
        }
    }

    if (!found) {
        Discord::Reaction newReaction;
        newReaction.emoji = emoji;
        newReaction.count = 1;
        newReaction.me = !isBurst && isMe;
        newReaction.meBurst = isBurst && isMe;
        newReaction.burstCount = isBurst ? 1 : 0;

        Discord::ReactionCountDetails details;
        details.burst = isBurst ? 1 : 0;
        details.normal = isBurst ? 0 : 1;
        newReaction.countDetails = details;

        if (isBurst && !burstColors.isEmpty())
            newReaction.burstColors = burstColors;

        reactions.append(newReaction);
    }
}

void MessageManager::onReactionAdd(const Discord::MessageReactionAdd &event)
{
    bool isBurst = event.type.hasValue() && *event.type == 1;
    bool isMe = event.userId.get() == client->getMe().id.get();

    auto *cached = messageCache.object(event.messageId);
    if (!cached) {
        QList<Discord::Reaction> reactions =
                reactionsFromJson(repo.getReactionsJson(event.messageId));
        QList<QString> colors = event.burstColors.hasValue() ? *event.burstColors : QList<QString>{};
        applyReactionAdd(reactions, event.emoji, isBurst, isMe, colors);
        repo.updateReactionsJson(event.messageId, reactionsToJson(reactions));
        return;
    }

    Discord::Message msg = *cached;

    if (!msg.reactions.hasValue())
        msg.reactions = QList<Discord::Reaction>();

    QList<QString> colors = event.burstColors.hasValue() ? *event.burstColors : QList<QString>{};
    applyReactionAdd(*msg.reactions, event.emoji, isBurst, isMe, colors);

    emitReactionUpdate(msg);
}

static void applyReactionAddMany(QList<Discord::Reaction> &reactions,
                                 const Discord::MessageReactionAddMany &event, Snowflake myId)
{
    for (const auto &debounced : *event.reactions) {
        bool isMe = false;
        for (const auto &uid : *debounced.users) {
            if (uid == myId) {
                isMe = true;
                break;
            }
        }

        int addCount = debounced.users->size();
        bool found = false;
        for (auto &r : reactions) {
            if (emojisMatch(r.emoji, debounced.emoji)) {
                r.count = *r.count + addCount;
                if (r.countDetails.hasValue())
                    r.countDetails->normal = *r.countDetails->normal + addCount;
                if (isMe)
                    r.me = true;
                found = true;
                break;
            }
        }

        if (!found) {
            Discord::Reaction newReaction;
            newReaction.emoji = debounced.emoji;
            newReaction.count = addCount;
            newReaction.me = isMe;
            newReaction.meBurst = false;
            newReaction.burstCount = 0;

            Discord::ReactionCountDetails details;
            details.burst = 0;
            details.normal = addCount;
            newReaction.countDetails = details;

            reactions.append(newReaction);
        }
    }
}

void MessageManager::onReactionAddMany(const Discord::MessageReactionAddMany &event)
{
    Snowflake myId = client->getMe().id;

    auto *cached = messageCache.object(event.messageId);
    if (!cached) {
        QList<Discord::Reaction> reactions =
                reactionsFromJson(repo.getReactionsJson(event.messageId));
        applyReactionAddMany(reactions, event, myId);
        repo.updateReactionsJson(event.messageId, reactionsToJson(reactions));
        return;
    }

    Discord::Message msg = *cached;

    if (!msg.reactions.hasValue())
        msg.reactions = QList<Discord::Reaction>();

    applyReactionAddMany(*msg.reactions, event, myId);

    emitReactionUpdate(msg);
}

static void applyReactionRemove(QList<Discord::Reaction> &reactions,
                                const Discord::Emoji &emoji, bool isBurst, bool isMe)
{
    for (int i = 0; i < reactions.size(); ++i) {
        auto &r = reactions[i];
        if (emojisMatch(r.emoji, emoji)) {
            r.count = *r.count - 1;
            if (r.countDetails.hasValue()) {
                if (isBurst)
                    r.countDetails->burst = qMax(0, *r.countDetails->burst - 1);
                else
                    r.countDetails->normal = qMax(0, *r.countDetails->normal - 1);
            }
            if (isMe) {
                if (isBurst)
                    r.meBurst = false;
                else
                    r.me = false;
            }

            if (*r.count <= 0)
                reactions.removeAt(i);

            break;
        }
    }
}

void MessageManager::onReactionRemove(const Discord::MessageReactionRemove &event)
{
    bool isBurst = event.type.hasValue() && *event.type == 1;
    bool isMe = event.userId.get() == client->getMe().id.get();

    auto *cached = messageCache.object(event.messageId);
    if (!cached) {
        QList<Discord::Reaction> reactions =
                reactionsFromJson(repo.getReactionsJson(event.messageId));
        applyReactionRemove(reactions, event.emoji, isBurst, isMe);
        repo.updateReactionsJson(event.messageId, reactionsToJson(reactions));
        return;
    }

    Discord::Message msg = *cached;

    if (!msg.reactions.hasValue())
        return;

    applyReactionRemove(*msg.reactions, event.emoji, isBurst, isMe);

    emitReactionUpdate(msg);
}

void MessageManager::onReactionRemoveAll(const Discord::MessageReactionRemoveAll &event)
{
    auto *cached = messageCache.object(event.messageId);
    if (!cached) {
        repo.updateReactionsJson(event.messageId, {});
        return;
    }

    Discord::Message msg = *cached;
    msg.reactions = QList<Discord::Reaction>();

    emitReactionUpdate(msg);
}

void MessageManager::onReactionRemoveEmoji(const Discord::MessageReactionRemoveEmoji &event)
{
    auto *cached = messageCache.object(event.messageId);
    if (!cached) {
        QList<Discord::Reaction> reactions =
                reactionsFromJson(repo.getReactionsJson(event.messageId));
        for (int i = 0; i < reactions.size(); ++i) {
            if (emojisMatch(reactions[i].emoji, event.emoji)) {
                reactions.removeAt(i);
                break;
            }
        }
        repo.updateReactionsJson(event.messageId, reactionsToJson(reactions));
        return;
    }

    Discord::Message msg = *cached;

    if (!msg.reactions.hasValue())
        return;

    for (int i = 0; i < msg.reactions->size(); ++i) {
        if (emojisMatch((*msg.reactions)[i].emoji, event.emoji)) {
            msg.reactions->removeAt(i);
            break;
        }
    }

    emitReactionUpdate(msg);
}

void MessageManager::onApiMessagesReceived(const QList<Discord::Message> &messages, LoadType type,
                                           Snowflake channelId, Snowflake anchorId)
{
    repo.saveMessages(messages);

    auto sortedMessages = messages;
    std::sort(sortedMessages.begin(), sortedMessages.end(),
              [](const auto &a, const auto &b) { return a.id.get() < b.id.get(); });

    bool shortPage = sortedMessages.size() < PageSize;
    if (type == LoadType::History && shortPage)
        channelStartId[channelId] = sortedMessages.isEmpty() ? anchorId : sortedMessages.first().id.get();
    else if (type == LoadType::Latest && shortPage && !sortedMessages.isEmpty())
        channelStartId[channelId] = sortedMessages.first().id.get();

    for (auto &msg : sortedMessages)
        parseMessageContent(msg);

    for (const auto &msg : sortedMessages) {
        // cache owns its own copy
        Discord::Message *toCache = new Discord::Message(msg);
        messageCache.insert(toCache->id, toCache); // should remove old copies
    }

    auto &segs = segments[channelId];
    QList<Snowflake> ids = idsOf(sortedMessages);
    bool reachedLatest = false;

    switch (type) {
    case LoadType::Latest:
        fetchedChannels.insert(channelId);
        segs.setTail(ids);
        reachedLatest = true;
        break;
    case LoadType::Created:
        segs.merge(ids, Snowflake::Invalid, true);
        break;
    case LoadType::History:
        segs.merge(ids, anchorId, false);
        break;
    case LoadType::Future:
    case LoadType::Jump: {
        bool coversPresent = false;
        if (type == LoadType::Future) {
            coversPresent = sortedMessages.size() < PageSize;
        } else {
            int atOrAfterTarget = std::count_if(ids.cbegin(), ids.cend(),
                                                [&](Snowflake id) { return id >= anchorId; });
            coversPresent = atOrAfterTarget < JumpWindow / 2;
        }
        segs.merge(ids, anchorId, coversPresent);

        Snowflake newestLoaded = ids.isEmpty() ? anchorId : ids.last();
        auto pos = segs.find(newestLoaded);

        reachedLatest = pos.run ? pos.run->isTail : coversPresent;
        qCDebug(LogCore) << "Loaded" << ids.size() << "messages around/after" << anchorId << "in"
                         << channelId << "coversPresent" << coversPresent << "reachedLatest" << reachedLatest;

        if (pos.run && reachedLatest) {
            for (int i = pos.index + 1; i < static_cast<int>(pos.run->ids.size()); ++i) {
                auto *cached = messageCache.object(pos.run->ids[i]);
                if (!cached)
                    break;
                sortedMessages.append(*cached);
            }
        }
        break;
    }
    }

    emit messagesReceived({ .success = true,
                            .type = type,
                            .channelId = channelId,
                            .messages = sortedMessages,
                            .reachedLatest = reachedLatest,
                            .anchorId = anchorId });
}

} // namespace Core
} // namespace Acheron
