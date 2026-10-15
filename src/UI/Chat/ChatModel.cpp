#include "ChatModel.hpp"
#include "Core/Media/InputPolicy.hpp"

#include "Plugins/Client/MessageContexts.hpp"
#include "Plugins/PluginHost.hpp"

#include <QImageReader>

#include <algorithm>
#include <cmath>

#include "Core/Markdown/Parser.hpp"
#include "Core/MessageManager.hpp"
#include "Core/ImageManager.hpp"
#include "Core/Theme/Manager.hpp"
#include "Core/Media/Player.hpp"
#include "Discord/CdnUrls.hpp"
#include "Discord/Enums.hpp"

namespace Acheron {
namespace UI {

static QUrl reactionEmojiUrl(const Discord::Emoji &emoji)
{
    return emoji.getImageUrl(emoji.isAnimated());
}

static bool isSystemMessageType(Discord::MessageType type)
{
    switch (type) {
    case Discord::MessageType::DEFAULT:
    case Discord::MessageType::REPLY:
    case Discord::MessageType::CHAT_INPUT_COMMAND:
    case Discord::MessageType::CONTEXT_MENU_COMMAND:
        return false;
    default:
        return true;
    }
}

static EmbedType embedTypeFromString(const QString &typeStr)
{
    if (typeStr.isEmpty() || typeStr == "rich")
        return EmbedType::Rich;
    else if (typeStr == "age_verification_system_notification")
        return EmbedType::AgeVerificationSystemNotification;
    else if (typeStr == "application_news")
        return EmbedType::ApplicationNews;
    else if (typeStr == "article")
        return EmbedType::Article;
    else if (typeStr == "auto_moderation_message")
        return EmbedType::AutoModerationMessage;
    else if (typeStr == "auto_moderation_notification")
        return EmbedType::AutoModerationNotification;
    else if (typeStr == "gift")
        return EmbedType::Gift;
    else if (typeStr == "gifv")
        return EmbedType::Gifv;
    else if (typeStr == "image")
        return EmbedType::Image;
    else if (typeStr == "link")
        return EmbedType::Link;
    else if (typeStr == "poll_result")
        return EmbedType::PollResult;
    else if (typeStr == "post_preview")
        return EmbedType::PostPreview;
    else if (typeStr == "rich")
        return EmbedType::Rich;
    else if (typeStr == "safety_policy_notice")
        return EmbedType::SafetyPolicyNotice;
    else if (typeStr == "safety_system_notification")
        return EmbedType::SafetySystemNotification;
    else if (typeStr == "video")
        return EmbedType::Video;
    return EmbedType::Rich;
}

ChatModel::ChatModel(Core::ImageManager *imageManager, QObject *parent)
    : QAbstractListModel(parent), imageManager(imageManager)
{
    connect(imageManager, &Core::ImageManager::imageUnavailable, this, [this](const QUrl &url) {
        if (Discord::Cdn::isStickerUrl(url))
            refreshRowsShowingSticker(url);
    });

    connect(imageManager, &Core::ImageManager::imageFetched, this,
            [this](const QUrl &url, const QSize &size, const QPixmap &pixmap) {
                // avatar pending requests
                avatarTracker.notify(url, [this](const QModelIndex &index) {
                    if (index.isValid())
                        emit dataChanged(index, index, { Qt::DecorationRole });
                });

                // Qord: reaction users' avatars (WhoReacted)
                if (reactionAvatarUrls.remove(url)) {
                    for (int row = 0; row < messages.size(); ++row)
                        if (messages[row].reactions.hasValue() && !messages[row].reactions->isEmpty())
                            emit dataChanged(index(row, 0), index(row, 0), { ReactionsRole });
                }

                if (Discord::Cdn::isEmojiUrl(url)) {
                    const QString srcPrefix = url.toString(QUrl::RemoveQuery);
                    const bool documentSize = size == QSize(Core::Markdown::InlineEmojiPx, Core::Markdown::InlineEmojiPx) ||
                                              size == QSize(Core::Markdown::JumboEmojiPx, Core::Markdown::JumboEmojiPx);
                    for (int row = 0; row < messages.size(); ++row) {
                        const auto &msg = messages[row];
                        bool found = documentSize && msg.contentMessage().parsedContentCached.contains(srcPrefix);
                        if (documentSize && !found && embedCache.contains(msg.id)) {
                            for (const auto &embed : embedCache.value(msg.id)) {
                                if (embed.titleParsed.contains(srcPrefix) ||
                                    embed.descriptionParsed.contains(srcPrefix)) {
                                    found = true;
                                    break;
                                }
                                for (const auto &field : embed.fields) {
                                    if (field.nameParsed.contains(srcPrefix) ||
                                        field.valueParsed.contains(srcPrefix)) {
                                        found = true;
                                        break;
                                    }
                                }
                                if (found)
                                    break;
                            }
                        }

                        bool reactionFound = false;
                        if (!found && msg.reactions.hasValue()) {
                            for (const auto &reaction : *msg.reactions) {
                                if (!reaction.emoji->isUnicode() && reactionEmojiUrl(*reaction.emoji) == url) {
                                    reactionFound = true;
                                    break;
                                }
                            }
                        }

                        if (found) {
                            QModelIndex idx = index(row, 0);
                            emit dataChanged(idx, idx, { HtmlRole });
                        } else if (reactionFound) {
                            sizeCache.remove(msg.id);
                            QModelIndex idx = index(row, 0);
                            emit dataChanged(idx, idx, { ReactionsRole, CachedSizeRole });
                        }
                    }
                    return;
                }

                if (Discord::Cdn::isStickerUrl(url)) {
                    refreshRowsShowingSticker(url);
                    return;
                }

                // attachment and embed images
                for (int row = 0; row < messages.size(); ++row) {
                    const auto &msg = messages[row];
                    const auto &visible = msg.contentMessage();
                    bool found = false;

                    if (visible.attachments.hasValue()) {
                        for (const auto &att : *visible.attachments) {
                            if (QUrl(*att.proxyUrl) == url) {
                                found = true;
                                break;
                            }
                        }
                    }

                    if (!found && visible.embeds.hasValue()) {
                        for (const auto &embed : *visible.embeds) {
                            if (embed.author.hasValue() && embed.author->proxyIconUrl.hasValue() &&
                                QUrl(*embed.author->proxyIconUrl) == url) {
                                found = true;
                                break;
                            }
                            if (embed.footer.hasValue() && embed.footer->proxyIconUrl.hasValue() &&
                                QUrl(*embed.footer->proxyIconUrl) == url) {
                                found = true;
                                break;
                            }
                            if (embed.thumbnail.hasValue() &&
                                embed.thumbnail->proxyUrl.hasValue() &&
                                QUrl(*embed.thumbnail->proxyUrl) == url) {
                                found = true;
                                break;
                            }

                            if (embed.image.hasValue() && embed.image->proxyUrl.hasValue() &&
                                QUrl(*embed.image->proxyUrl) == url) {
                                found = true;
                                break;
                            }

                            if (embed.video.hasValue() && embed.video->proxyUrl.hasValue() &&
                                QUrl(*embed.video->proxyUrl) == url) {
                                found = true;
                                break;
                            }
                        }
                    }

                    if (found) {
                        embedCache.remove(msg.id);
                        invalidateDocCacheForMessage(msg.id);
                        QModelIndex idx = index(row, 0);
                        emit dataChanged(idx, idx, { AttachmentsRole, EmbedsRole, CachedSizeRole });
                    }
                }

                for (int row = 0; row < messages.size(); ++row) {
                    auto it = forwardOriginCache.constFind(messages[row].id);
                    if (it != forwardOriginCache.constEnd() && it->iconUrl.isValid() &&
                        it->iconUrl == url) {
                        QModelIndex idx = index(row, 0);
                        emit dataChanged(idx, idx, { ForwardOriginRole });
                    }
                }
            });
}

void ChatModel::refreshRowsShowingSticker(const QUrl &stickerUrl)
{
    const Snowflake stickerId = Discord::Cdn::stickerIdFromUrl(stickerUrl);
    for (int row = 0; row < messages.size(); ++row) {
        const auto &stickers = messages[row].contentMessage().stickerItems;
        if (!stickers.hasValue())
            continue;
        const bool shown = std::any_of(stickers->cbegin(), stickers->cend(), [stickerId](const Discord::StickerItem &sticker) {
            return sticker.id.get() == stickerId;
        });
        if (shown) {
            const QModelIndex idx = index(row, 0);
            emit dataChanged(idx, idx, { StickersRole });
        }
    }
}

bool ChatModel::isTextFile(const QString &filename, const QString &contentType)
{
    static const QSet<QString> extensions = {
        QStringLiteral("txt"),  QStringLiteral("md"),   QStringLiteral("log"),  QStringLiteral("json"),
        QStringLiteral("xml"),  QStringLiteral("yml"),  QStringLiteral("yaml"), QStringLiteral("toml"),
        QStringLiteral("ini"),  QStringLiteral("cfg"),  QStringLiteral("conf"), QStringLiteral("csv"),
        QStringLiteral("tsv"),  QStringLiteral("js"),   QStringLiteral("mjs"),  QStringLiteral("ts"),
        QStringLiteral("jsx"),  QStringLiteral("tsx"),  QStringLiteral("py"),   QStringLiteral("c"),
        QStringLiteral("h"),    QStringLiteral("cpp"),  QStringLiteral("hpp"),  QStringLiteral("cc"),
        QStringLiteral("cs"),   QStringLiteral("java"), QStringLiteral("kt"),   QStringLiteral("go"),
        QStringLiteral("rs"),   QStringLiteral("rb"),   QStringLiteral("php"),  QStringLiteral("sh"),
        QStringLiteral("bash"), QStringLiteral("ps1"),  QStringLiteral("bat"),  QStringLiteral("lua"),
        QStringLiteral("sql"),  QStringLiteral("html"), QStringLiteral("css"),  QStringLiteral("scss"),
        QStringLiteral("diff"), QStringLiteral("patch"), QStringLiteral("swift"), QStringLiteral("dart"),
        QStringLiteral("vue"),  QStringLiteral("cmake"), QStringLiteral("gradle"), QStringLiteral("properties"),
    };
    const QString type = contentType.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
    if (type == QLatin1String("image/svg+xml"))
        return false;
    if (type.startsWith(QLatin1String("text/")) || type == QLatin1String("application/json") ||
        type == QLatin1String("application/xml") || type == QLatin1String("application/javascript"))
        return true;
    const int dot = filename.lastIndexOf(QLatin1Char('.'));
    return dot > 0 && extensions.contains(filename.mid(dot + 1).toLower());
}

void ChatModel::loadTextPreview(Snowflake messageId, AttachmentData &data) const
{
    auto it = textPreviews.constFind(data.id);
    if (it == textPreviews.constEnd()) {
        if (!textLoader || suppressImageFetch)
            return;
        textPreviews.insert(data.id, {}); // loading
        const Snowflake attachmentId = data.id;
        const qint64 size = data.fileSizeBytes;
        auto *self = const_cast<ChatModel *>(this);
        textLoader(data.originalUrl, self, [self, messageId, attachmentId, size](bool ok, const QByteArray &bytes) {
            TextPreview preview;
            preview.loaded = ok;
            if (ok) {
                const QByteArray start = bytes.left(TextPreviewBytes);
                preview.text = QString::fromUtf8(start);
                preview.bytesLeft = qMax<qint64>(0, size - start.size());
                // A character cut in two at the end of what was loaded
                if (preview.bytesLeft > 0 && preview.text.endsWith(QChar::ReplacementCharacter))
                    preview.text.chop(1);
            }
            self->textPreviews.insert(attachmentId, preview);
            self->refreshAttachments(messageId);
        });
        return;
    }
    if (!it->loaded)
        return;
    data.textLoaded = true;
    data.textPreview = it->text;
    data.textBytesLeft = it->bytesLeft;
    data.textExpanded = expandedTexts.contains(data.id);
    if (auto *host = Qord::Plugins::PluginHost::instance(); host && host->hasHook(Qord::Plugins::Hook::AttachmentButtons))
        for (const auto &entry : host->dispatchAttachmentButtons(attachmentContext(messageId, data)))
            data.textButtons.append({ entry.pluginId, entry.button.id, entry.button.tooltip, entry.button.svg });
}

QList<Qord::Plugins::ChatMessageInfo> ChatModel::messageInfos() const
{
    auto *host = Qord::Plugins::PluginHost::instance();
    const bool annotations = host && host->hasHook(Qord::Plugins::Hook::MessageHistory);
    QList<Qord::Plugins::ChatMessageInfo> out;
    for (const Discord::Message &msg : messages) {
        Qord::Plugins::ChatMessageInfo info;
        info.id = quint64(msg.id.get());
        info.authorId = msg.author.hasValue() ? quint64(msg.author->id.get()) : 0;
        info.type = msg.type.hasValue() ? int(msg.type.get()) : 0;
        info.flags = msg.flags.hasValue() ? quint64(msg.flags.get().toInt()) : 0;
        if (annotations)
            if (const auto annotation = host->dispatchAnnotateMessage(Qord::Plugins::toMessageContext(
                        msg, quint64(currentAccountId), Qord::Plugins::pluginId(currentGuildId))))
                info.deleted = annotation->deleted;
        out.append(info);
    }
    return out;
}

Qord::Plugins::AttachmentContext ChatModel::attachmentContext(Snowflake messageId, const AttachmentData &attachment) const
{
    Qord::Plugins::AttachmentContext context;
    context.accountId = quint64(currentAccountId);
    context.channelId = Qord::Plugins::pluginId(getActiveChannelId());
    context.messageId = quint64(messageId);
    context.attachmentId = quint64(attachment.id);
    context.filename = attachment.filename;
    context.contentType = attachment.contentType;
    context.size = attachment.fileSizeBytes;
    context.text = attachment.textPreview;
    context.bytesLeft = attachment.textBytesLeft;
    return context;
}

void ChatModel::toggleTextExpanded(Snowflake messageId, Snowflake attachmentId)
{
    if (!expandedTexts.remove(attachmentId))
        expandedTexts.insert(attachmentId);
    refreshAttachments(messageId);
}

void ChatModel::refreshAttachments(Snowflake messageId)
{
    for (int row = 0; row < messages.size(); ++row) {
        if (messages[row].id.get() == messageId) {
            sizeCache.remove(messageId);
            emit dataChanged(index(row, 0), index(row, 0), { AttachmentsRole, CachedSizeRole });
            return;
        }
    }
}

void ChatModel::refreshReactions(Snowflake messageId)
{
    for (int row = 0; row < messages.size(); ++row) {
        if (messages[row].id.get() == messageId) {
            sizeCache.remove(messageId);
            emit dataChanged(index(row, 0), index(row, 0), { ReactionsRole, CachedSizeRole });
            return;
        }
    }
}

void ChatModel::setAvatarUrlResolver(AvatarUrlResolver resolver)
{
    avatarUrlResolver = std::move(resolver);
}

void ChatModel::setDisplayNameResolver(DisplayNameResolver resolver)
{
    displayNameResolver = std::move(resolver);
}

void ChatModel::setRoleColorResolver(RoleColorResolver resolver)
{
    roleColorResolver = std::move(resolver);
}

void ChatModel::setChannelNameResolver(ChannelNameResolver resolver)
{
    channelNameResolver = std::move(resolver);
}

void ChatModel::setGuildInfoResolver(GuildInfoResolver resolver)
{
    guildInfoResolver = std::move(resolver);
}

QString ChatModel::resolveAuthorName(const Discord::User &author) const
{
    if (displayNameResolver) {
        QString name = displayNameResolver(author, currentGuildId);
        if (!name.isEmpty())
            return name;
    }
    return author.getDisplayName();
}

QColor ChatModel::resolveAuthorColor(const Discord::User &author) const
{
    if (!roleColorResolver || currentGuildId == Snowflake::Invalid)
        return {};
    return roleColorResolver(author.id.get(), currentGuildId);
}

ForwardOriginData ChatModel::forwardOrigin(const Discord::Message &msg) const
{
    const Discord::MessageReference &ref = *msg.messageReference;

    ForwardOriginData origin;
    origin.channelId = ref.channelId.get();

    QString label;
    bool crossGuild = ref.guildId.hasValue() && ref.guildId.get() != currentGuildId;
    if (crossGuild && guildInfoResolver) {
        auto [guildName, guildIcon] = guildInfoResolver(ref.guildId.get());
        if (!guildName.isEmpty()) {
            label = guildName;
            origin.iconUrl = guildIcon;
        }
    }
    if (label.isEmpty() && channelNameResolver) {
        QString channelName = channelNameResolver(origin.channelId);
        if (!channelName.isEmpty())
            label = QStringLiteral("#%1").arg(channelName);
    }
    if (label.isEmpty())
        return origin;

    origin.text = label;
    if (msg.snapshotMessage && msg.snapshotMessage->timestamp.hasValue()) {
        QDateTime sent = msg.snapshotMessage->timestamp->toLocalTime();
        QString sentText = sent.date() == QDate::currentDate()
                                   ? sent.toString(QStringLiteral("hh:mm"))
                                   : sent.toString(QStringLiteral("MMM d, yyyy h:mm AP"));
        origin.text += QStringLiteral("  •  %1").arg(sentText);
    }
    origin.text += QStringLiteral("  ›");
    return origin;
}

int ChatModel::rowCount(const QModelIndex &parent) const
{
    return messages.size();
}

QVariant ChatModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    const auto &msg = messages[index.row()];
    switch (role) {
    case Qt::DisplayRole:
        [[fallthrough]];
    case ContentRole:
        return msg.contentMessage().content;
    case UsernameRole:
        return resolveAuthorName(msg.author.get());
    case AvatarRole: {
        const QSize desiredSize(40, 40); // Qord: matches ChatLayout::avatarSize()

        if (!avatarUrlResolver)
            return imageManager->placeholder(desiredSize);

        QUrl url = avatarUrlResolver(msg.author.get(), currentGuildId);
        return avatarTracker.fetch(imageManager, url, desiredSize, index, currentAccountId, Core::PinGroup::ChatView);
    }
    case TimestampRole:
        return msg.timestamp;
    case EditedTimestampRole:
        return msg.editedTimestamp.hasValue() ? QVariant(*msg.editedTimestamp) : QVariant();
    case UserIdRole:
        return msg.author->id;
    case CachedSizeRole: {
        if (sizeCache.contains(msg.id))
            return sizeCache.value(msg.id);
        return {};
    }
    case ShowHeaderRole: {
        if (isSystemMessageType(msg.type))
            return false;

        // replies always show a header (Qord: so do slash command answers)
        if (msg.type == Discord::MessageType::REPLY || msg.type == Discord::MessageType::CHAT_INPUT_COMMAND)
            return true;

        if (isFirstUnread(index.row())) // Qord: a new group starts after the NEW divider
            return true;

        // Qord: the message before, skipping ones hidden by plugins
        int prev = index.row() - 1;
        while (prev >= 0 && hiddenRowCheck && hiddenRowCheck(prev))
            --prev;
        if (prev < 0)
            return true;

        const auto &prevMsg = messages[prev];

        if (prevMsg.author->id != msg.author->id)
            return true;

        if (prevMsg.timestamp->toLocalTime().date() != msg.timestamp->toLocalTime().date())
            return true;

        return false;
    }
    case NewMarkerRole:
        return isFirstUnread(index.row());
    case DateSeparatorRole: {
        if (index.row() == 0 || isFirstUnread(index.row()))
            return true;

        const auto &prevMsg = messages[index.row() - 1];

        if (prevMsg.timestamp->toLocalTime().date() != msg.timestamp->toLocalTime().date())
            return true;

        return false;
    }
    case HtmlRole: {
        if (msg.type.get() == Discord::MessageType::THREAD_CREATED) {
            QString threadName = msg.content.hasValue() ? msg.content.get().toHtmlEscaped() : QString();
            Core::Snowflake threadId = Core::Snowflake::Invalid;
            if (msg.messageReference.hasValue() && msg.messageReference->channelId.hasValue())
                threadId = msg.messageReference->channelId.get();
            else if (msg.flags.hasValue() && msg.flags->testFlag(Discord::MessageFlag::HAS_THREAD))
                threadId = msg.id.get();

            QString authorName = resolveAuthorName(msg.author.get()).toHtmlEscaped();
            QColor authorColor = resolveAuthorColor(msg.author.get());
            QString authorHtml = authorColor.isValid()
                                         ? QStringLiteral("<span style=\"color:%1;font-weight:600\">%2</span>")
                                                   .arg(authorColor.name(), authorName)
                                         : QStringLiteral("<b>%1</b>").arg(authorName);

            QString label = threadName.isEmpty() ? tr("a thread") : threadName;
            QString threadHtml = threadId.isValid()
                                         ? QStringLiteral("<a href=\"acheron://channel/%1\">%2</a>")
                                                   .arg(QString::number(static_cast<quint64>(threadId)), label)
                                         : QStringLiteral("<b>%1</b>").arg(label);

            return authorHtml + tr(" started a thread: ") + threadHtml;
        }

        if (msg.type.get() == Discord::MessageType::THREAD_STARTER_MESSAGE) {
            if (msg.referencedMessage) {
                if (!msg.referencedMessage->parsedContentCached.isEmpty())
                    return msg.referencedMessage->parsedContentCached;
                if (msg.referencedMessage->content.hasValue())
                    return msg.referencedMessage->content.get().toHtmlEscaped();
                return QString();
            }
            return tr("Sorry, we couldn't load the first message in this thread.");
        }

        const Discord::Message &visible = msg.contentMessage();

        QString html = visible.parsedContentCached;

        // for image embeds, suppress text if content is just the embed url
        if (visible.embeds.hasValue() && visible.embeds->size() == 1) {
            const auto &embed = visible.embeds->first();
            QString embedType = embed.type.hasValue() ? *embed.type : QString();
            if (embedType == "image") {
                QString embedUrl = embed.url.hasValue() ? *embed.url : QString();
                if (!embedUrl.isEmpty() && visible.content == embedUrl)
                    html.clear();
            }
        }

        if (msg.flags.hasValue() && msg.flags->testFlag(Discord::MessageFlag::HAS_THREAD)) {
            QString sep = html.isEmpty() ? QString() : QStringLiteral("<br>");
            html += sep +
                    QStringLiteral("<a href=\"acheron://channel/%1\">"
                                   "<img src=\"acheron-icon:view-thread\" width=\"14\" height=\"14\""
                                   " style=\"vertical-align: middle\">"
                                   " %2</a>")
                            .arg(QString::number(static_cast<quint64>(msg.id.get())),
                                 // Qord: the thread's name and size, as Discord shows them
                                 msg.threadName.isEmpty() ? tr("View Thread")
                                 : msg.threadMessageCount >= 0
                                         ? tr("%1 · %n Messages", nullptr, msg.threadMessageCount)
                                                   .arg(msg.threadName.toHtmlEscaped())
                                         : msg.threadName.toHtmlEscaped());
        }

        if (revealedSpoilerMessages.contains(msg.id.get()))
            html.replace(QLatin1String(Core::Markdown::SpoilerHiddenStyle), QLatin1String(Core::Markdown::SpoilerShownStyle));

        if (msg.isForwarded()) {
            QString mutedColor = Core::Theme::Manager::instance()
                                         .color(Core::Theme::Token::PlaceholderText)
                                         .name();
            QString header = QStringLiteral("<img src=\"acheron-icon:forwarded\" width=\"12\" height=\"12\""
                                            " style=\"vertical-align: middle\">"
                                            " <span style=\"color: %1; font-size: small\"><i>%2</i></span>")
                                     .arg(mutedColor, tr("Forwarded"));
            html = html.isEmpty() ? header : header + QStringLiteral("<br>") + html;
        }

        return html;
    }
    case AttachmentsRole: {
        const Discord::Message &visible = msg.contentMessage();
        if (!visible.attachments.hasValue() || visible.attachments->isEmpty())
            return QVariant();

        const QVector<QPair<qint64, qint64>> *progress = nullptr;
        if (msg.nonce.hasValue()) {
            auto it = uploadProgress.constFind(msg.nonce.get());
            if (it != uploadProgress.constEnd())
                progress = &it.value();
        }

        QList<AttachmentData> result;
        int attachmentIndex = -1;
        for (const auto &att : *visible.attachments) {
            // Qord: plugins may show an attachment as something else (FakeNitro stickers)
            if (++attachmentIndex, &visible == &msg && msg.qordHiddenAttachments.contains(attachmentIndex))
                continue;
            AttachmentData data;
            data.id = att.id;
            data.proxyUrl = QUrl(*att.proxyUrl);
            data.originalUrl = QUrl(*att.url);
            data.isLocalPreview = visible.isPendingOutbound;
            const MediaFlags media = mediaFlagsFor(att, visible);
            data.isImage = media.isImage;
            data.contentType = media.contentType;
            data.isVideo = media.isVideo;
            data.isVoiceMessage = media.isVoiceMessage;
            data.isAudio = media.isAudio;
            data.durationMs = media.durationMs;
            data.filename = att.filename.hasValue() ? *att.filename : "unknown";
            data.fileSizeBytes = att.size.hasValue() ? *att.size : 0;
            data.isSpoiler = att.isSpoiler();

            int attIndex = result.size();
            if (progress && attIndex < progress->size()) {
                data.uploadSent = (*progress)[attIndex].first;
                data.uploadTotal = (*progress)[attIndex].second;
            }

            if (data.isMedia()) {
                QSize original;
                if (att.width.hasValue() && att.height.hasValue())
                    original = QSize(*att.width, *att.height);
                if (!original.isValid() && data.isVideo) {
                    const QSize decoded = videoNativeSizes.value(data.id);
                    original = decoded.isValid() ? decoded : QSize(1280, 720);
                }

                data.displaySize = Core::ImageManager::calculateDisplaySize(original);
                if (!att.localPreview.isNull()) {
                    // pending paste preview: pixels live in memory, not on disk
                    data.pixmap = previewPixmap(att.id, att.localPreview, data.displaySize);
                    data.isLoading = data.pixmap.isNull();
                } else if (data.isLocalPreview && data.proxyUrl.isLocalFile() &&
                           Core::Media::allowedChatMediaUrl(data.proxyUrl, true)) {
                    // pending dropped-file preview: decode from disk
                    data.pixmap = localPixmap(data.proxyUrl, data.displaySize);
                    data.isLoading = data.pixmap.isNull();
                } else {
                    data.pixmap = suppressImageFetch
                                          ? imageManager->getIfCached(data.proxyUrl, data.displaySize)
                                          : imageManager->get(data.proxyUrl, data.displaySize, currentAccountId);
                    data.isLoading = !imageManager->isCached(data.proxyUrl, data.displaySize);
                }
            } else {
                data.displaySize = QSize();
                data.isLoading = false;
                // Qord: Discord's text file preview
                if (!data.isAudio && data.fileSizeBytes > 0 && !data.originalUrl.isLocalFile() &&
                    isTextFile(data.filename, data.contentType)) {
                    data.isText = true;
                    loadTextPreview(msg.id.get(), data);
                }
            }

            result.append(data);
        }

        return QVariant::fromValue(result);
    }
    case MentionsMeRole: {
        if (msg.qordHighlighted) // Qord: a plugin's highlight (KeywordNotify)
            return true;
        const Discord::Message &visible = msg.contentMessage();
        if (visible.mentionEveryone.hasValue() && visible.mentionEveryone.get())
            return true;
        if (!visible.mentions.hasValue())
            return false;
        for (const auto &user : visible.mentions.get()) {
            if (user.id.get() == currentAccountId)
                return true;
        }
        return false;
    }
    case StickersRole: {
        const Discord::Message &visible = msg.contentMessage();
        // Qord: plus stickers display plugins add (FakeNitro)
        QList<Discord::StickerItem> stickers = visible.stickerItems.hasValue() ? *visible.stickerItems
                                                                               : QList<Discord::StickerItem>();
        if (&visible == &msg)
            stickers += msg.qordAddedStickers;
        if (stickers.isEmpty())
            return QVariant();

        const QSize displaySize(StickerData::DisplayPx, StickerData::DisplayPx);
        const int assetPx = Discord::Cdn::stickerAssetPx(StickerData::DisplayPx, qApp->devicePixelRatio());
        QList<StickerData> result;
        for (const auto &sticker : std::as_const(stickers)) {
            StickerData data;
            data.id = sticker.id;
            data.name = sticker.name;
            data.stillUrl = Discord::Cdn::stickerStill(sticker.id.get(), sticker.formatType.get(), assetPx);
            data.animatedUrl = Discord::Cdn::stickerAnimated(sticker.id.get(), sticker.formatType.get(), assetPx);
            if (data.hasRasterStill())
                data.still = pixmapOrNullWhileLoading(data.stillUrl, displaySize);
            data.unavailable = imageManager->isUnavailable({ data.hasRasterStill() ? data.stillUrl : data.animatedUrl, displaySize });
            result.append(data);
        }
        return QVariant::fromValue(result);
    }
    case EmbedsRole: {
        const Discord::Message &visible = msg.contentMessage();
        // Qord: plus the embeds plugins add (MessageLinkEmbeds)
        QList<Discord::Embed> allEmbeds = visible.embeds.hasValue() ? *visible.embeds : QList<Discord::Embed>();
        allEmbeds += msg.qordAddedEmbeds;
        if (allEmbeds.isEmpty())
            return QVariant();

        if (embedCache.contains(msg.id))
            return QVariant::fromValue(embedCache.value(msg.id));

        QList<EmbedData> result;
        // for handling the url-based embed image merging
        QMap<QString, int> urlToEmbedIndex;

        int embedIndex = -1;
        for (const auto &embed : allEmbeds) {
            // Qord: plugins may hide an embed (FakeNitro's emoji and sticker links)
            if (++embedIndex, &visible == &msg && msg.qordHiddenEmbeds.contains(embedIndex))
                continue;
            QString embedUrl = embed.url.hasValue() ? *embed.url : QString();

            bool hasImage = embed.image.hasValue() && embed.image->proxyUrl.hasValue() &&
                            embed.image->contentType.hasValue() &&
                            embed.image->contentType->startsWith("image/");

            bool shouldMerge = false;
            int parentIndex = -1;

            if (!embedUrl.isEmpty() && hasImage && urlToEmbedIndex.contains(embedUrl)) {
                parentIndex = urlToEmbedIndex[embedUrl];
                // excess ignored
                if (result[parentIndex].images.size() < 4)
                    shouldMerge = true;
            }

            if (shouldMerge) {
                EmbedImageData imageData;
                imageData.url = QUrl(*embed.image->proxyUrl);
                imageData.originalUrl = QUrl(*embed.image->url);
                QSize origSize;
                if (embed.image->width.hasValue() && embed.image->height.hasValue())
                    origSize = QSize(*embed.image->width, *embed.image->height);
                imageData.displaySize = Core::ImageManager::calculateDisplaySize(origSize);
                imageData.pixmap =
                        suppressImageFetch
                                ? imageManager->getIfCached(imageData.url, imageData.displaySize)
                                : imageManager->get(imageData.url, imageData.displaySize, currentAccountId);

                result[parentIndex].images.append(imageData);
            } else if (!shouldMerge && hasImage && !embedUrl.isEmpty() &&
                       urlToEmbedIndex.contains(embedUrl)) {
                continue;
            } else {
                EmbedData data;

                bool hasAnything = embed.title.hasValue() || embed.description.hasValue() ||
                                   embed.timestamp.hasValue() || embed.color.hasValue() ||
                                   embed.author.hasValue() || embed.footer.hasValue() || hasImage;

                data.type = embedTypeFromString(embed.type.hasValue() ? *embed.type : QString());
                data.title = embed.title.hasValue() ? *embed.title : QString();
                data.description = embed.description.hasValue() ? *embed.description : QString();
                data.url = embedUrl;
                data.timestamp = embed.timestamp.hasValue() ? *embed.timestamp : QDateTime();
                data.color = embed.color.hasValue()
                                     ? QColor::fromRgb(*embed.color)
                                     : Core::Theme::Manager::instance().color(Core::Theme::Token::EmbedDefault);

                static Core::Markdown::Parser parser;
                Core::Markdown::ParseState titleState;
                titleState.isInline = true;
                titleState.excludedRules.insert("link");
                if (!data.title.isEmpty()) {
                    auto ast = parser.parse(data.title, titleState);
                    data.titleParsed = parser.toHtml(ast);
                }

                Core::Markdown::ParseState descriptionState;
                descriptionState.isInline = true;
                if (!data.description.isEmpty()) {
                    auto ast = parser.parse(data.description, descriptionState);
                    data.descriptionParsed = parser.toHtml(ast);
                }

                if (embed.author.hasValue()) {
                    data.authorName =
                            embed.author->name.hasValue() ? *embed.author->name : QString();
                    data.authorUrl = embed.author->proxyIconUrl.hasValue()
                                             ? *embed.author->proxyIconUrl
                                             : QString();
                    if (embed.author->iconUrl.hasValue()) {
                        data.authorIconUrl = QUrl(*embed.author->proxyIconUrl);
                        data.authorIcon =
                                suppressImageFetch
                                        ? imageManager->getIfCached(data.authorIconUrl,
                                                                    QSize(24, 24))
                                        : imageManager->get(data.authorIconUrl, QSize(24, 24), currentAccountId);
                    }
                }

                if (embed.footer.hasValue()) {
                    data.footerText =
                            embed.footer->text.hasValue() ? *embed.footer->text : QString();
                    if (embed.footer->proxyIconUrl.hasValue()) {
                        data.footerIconUrl = QUrl(*embed.footer->proxyIconUrl);
                        data.footerIcon =
                                suppressImageFetch
                                        ? imageManager->getIfCached(data.footerIconUrl,
                                                                    QSize(20, 20))
                                        : imageManager->get(data.footerIconUrl, QSize(20, 20), currentAccountId);
                    }
                }

                if (embed.provider.hasValue()) {
                    data.providerName =
                            embed.provider->name.hasValue() ? *embed.provider->name : QString();
                    data.providerUrl =
                            embed.provider->url.hasValue() ? *embed.provider->url : QString();
                }

                // observed png thumbnail with width/height but no content type
                if (embed.thumbnail.hasValue() && embed.thumbnail->proxyUrl.hasValue() &&
                    embed.thumbnail->width > 0) {
                    hasAnything = true;
                    data.thumbnailUrl = QUrl(*embed.thumbnail->proxyUrl);
                    data.thumbnailOriginalUrl = QUrl(*embed.thumbnail->url);
                    QSize origSize;
                    if (embed.thumbnail->width.hasValue() && embed.thumbnail->height.hasValue())
                        origSize = QSize(*embed.thumbnail->width, *embed.thumbnail->height);

                    if (data.type == EmbedType::Gifv || data.type == EmbedType::Image)
                        data.thumbnailSize = Core::ImageManager::calculateDisplaySize(origSize);
                    else
                        data.thumbnailSize = origSize.isValid()
                                                     ? origSize.scaled(80, 80, Qt::KeepAspectRatio)
                                                     : QSize(80, 80);
                    data.thumbnail =
                            suppressImageFetch
                                    ? imageManager->getIfCached(data.thumbnailUrl,
                                                                data.thumbnailSize)
                                    : imageManager->get(data.thumbnailUrl, data.thumbnailSize, currentAccountId);
                }

                if (hasImage) {
                    EmbedImageData imageData;
                    imageData.url = QUrl(*embed.image->proxyUrl);
                    imageData.originalUrl = QUrl(*embed.image->url);
                    QSize origSize;
                    if (embed.image->width.hasValue() && embed.image->height.hasValue())
                        origSize = QSize(*embed.image->width, *embed.image->height);
                    imageData.displaySize = Core::ImageManager::calculateDisplaySize(origSize);
                    imageData.pixmap =
                            suppressImageFetch
                                    ? imageManager->getIfCached(imageData.url,
                                                                imageData.displaySize)
                                    : imageManager->get(imageData.url, imageData.displaySize, currentAccountId);
                    data.images.append(imageData);
                }

                if (embed.video.hasValue()) {
                    QUrl mediaUrl;
                    if (embed.video->proxyUrl.hasValue())
                        mediaUrl = QUrl(*embed.video->proxyUrl);

                    const QString videoType = embed.video->contentType.hasValue()
                                                      ? *embed.video->contentType
                                                      : QString();
                    if (!mediaUrl.isEmpty() && Core::Media::canPlay(videoType, mediaUrl)) {
                        data.videoUrl = mediaUrl;
                        data.videoPlayable = true;
                        hasAnything = true;
                    }

                    QUrl posterUrl;
                    QSize posterSize;
                    if (embed.thumbnail.hasValue() && embed.thumbnail->proxyUrl.hasValue() &&
                        embed.thumbnail->proxyUrl->startsWith("https://")) {
                        posterUrl = QUrl(*embed.thumbnail->proxyUrl);
                        if (embed.thumbnail->width.hasValue() && embed.thumbnail->height.hasValue())
                            posterSize = QSize(*embed.thumbnail->width, *embed.thumbnail->height);
                    } else if (data.videoPlayable && embed.video->proxyUrl.hasValue()) {
                        posterUrl = QUrl(*embed.video->proxyUrl);
                        if (embed.video->width.hasValue() && embed.video->height.hasValue())
                            posterSize = QSize(*embed.video->width, *embed.video->height);
                    }

                    if (!posterUrl.isEmpty()) {
                        hasAnything = true;
                        data.videoThumbnailUrl = posterUrl;
                        data.videoThumbnailSize = Core::ImageManager::calculateDisplaySize(posterSize);
                        data.videoThumbnail = suppressImageFetch
                                                      ? imageManager->getIfCached(data.videoThumbnailUrl,
                                                                                  data.videoThumbnailSize)
                                                      : imageManager->get(data.videoThumbnailUrl,
                                                                          data.videoThumbnailSize,
                                                                          currentAccountId);
                    }

                    if (data.videoPlayable && !data.videoThumbnailSize.isValid()) {
                        QSize videoSize;
                        if (embed.video->width.hasValue() && embed.video->height.hasValue())
                            videoSize = QSize(*embed.video->width, *embed.video->height);
                        data.videoThumbnailSize = Core::ImageManager::calculateDisplaySize(videoSize);
                    }
                }

                if (embed.fields.hasValue()) {
                    if (!embed.fields->empty())
                        hasAnything = true;
                    for (const auto &field : *embed.fields) {
                        EmbedFieldData fieldData;
                        fieldData.name = field.name.hasValue() ? *field.name : QString();
                        fieldData.value = field.value.hasValue() ? *field.value : QString();
                        fieldData.isInline = field.isInline.hasValue() ? *field.isInline : false;

                        Core::Markdown::ParseState nameState;
                        nameState.isInline = true;
                        nameState.excludedRules.insert("link");
                        if (!fieldData.name.isEmpty()) {
                            auto ast = parser.parse(fieldData.name, nameState);
                            fieldData.nameParsed = parser.toHtml(ast);
                        }

                        Core::Markdown::ParseState valueState;
                        valueState.isInline = true;
                        if (!fieldData.value.isEmpty()) {
                            auto ast = parser.parse(fieldData.value, valueState);
                            fieldData.valueParsed = parser.toHtml(ast);
                        }

                        data.fields.append(fieldData);
                    }
                }

                if (!embedUrl.isEmpty())
                    urlToEmbedIndex[embedUrl] = result.size();

                if (hasAnything)
                    result.append(data);
            }
        }

        if (!suppressImageFetch)
            embedCache[msg.id] = result;
        return QVariant::fromValue(result);
    }
    case IsPendingRole:
        return msg.nonce.hasValue() && pendingNonces.contains(msg.nonce.get());
    case IsErroredRole:
        return msg.nonce.hasValue() && erroredNonces.contains(msg.nonce.get());
    case UsernameColorRole:
        return resolveAuthorColor(msg.author.get());
    case MessageIdRole:
        return msg.id;
    case ReactionsRole: {
        if (!msg.reactions.hasValue() || msg.reactions->isEmpty())
            return QVariant();

        QList<ReactionData> result;
        for (const auto &reaction : *msg.reactions) {
            QPixmap emojiPixmap;
            bool isLoading = false;
            Core::Snowflake emojiId;
            QUrl emojiUrl;
            if (!reaction.emoji->isUnicode()) {
                emojiId = reaction.emoji->id;
                emojiUrl = reactionEmojiUrl(*reaction.emoji);
                QSize emojiSize(16, 16);
                emojiPixmap = imageManager->get(emojiUrl, emojiSize, currentAccountId);
                isLoading = !imageManager->isCached(emojiUrl, emojiSize);
            }

            int normalCount = reaction.countDetails.hasValue() ? *reaction.countDetails->normal : *reaction.count;
            int burstCount = reaction.countDetails.hasValue() ? *reaction.countDetails->burst : 0;

            if (burstCount > 0) {
                ReactionData data;
                data.emojiName = reaction.emoji->name;
                data.emojiId = emojiId;
                data.emojiUrl = emojiUrl;
                data.emojiAnimated = reaction.emoji->isAnimated();
                data.count = burstCount;
                data.me = reaction.meBurst.hasValue() && *reaction.meBurst;
                data.isBurst = true;
                data.emojiPixmap = emojiPixmap;
                data.isLoading = isLoading;
                data.burstTintColor = reaction.getBrightestBurstColor();
                result.append(data);
            }

            if (normalCount > 0) {
                ReactionData data;
                data.emojiName = reaction.emoji->name;
                data.emojiId = emojiId;
                data.emojiUrl = emojiUrl;
                data.emojiAnimated = reaction.emoji->isAnimated();
                data.count = normalCount;
                data.me = reaction.me;
                data.isBurst = false;
                data.emojiPixmap = emojiPixmap;
                data.isLoading = isLoading;
                result.append(data);
            }
        }

        // Qord: who reacted, from a plugin (WhoReacted); users that message
        // filters hide (NoBlockedMessages) are left out, as upstream does
        auto *plugins = Qord::Plugins::PluginHost::instance();
        if (plugins && plugins->hasHook(Qord::Plugins::Hook::ReactionUsers)) {
            const bool filtering = plugins->hasHook(Qord::Plugins::Hook::MessageFilter);
            for (auto &data : result) {
                Qord::Plugins::ReactionContext ctx;
                ctx.accountId = quint64(currentAccountId);
                ctx.guildId = quint64(currentGuildId);
                ctx.channelId = msg.channelId.hasValue() ? quint64(msg.channelId.get()) : 0;
                ctx.messageId = quint64(msg.id.get());
                ctx.emojiName = data.emojiName;
                ctx.emojiId = data.emojiId.isValid() ? quint64(data.emojiId) : 0; // 0: Unicode
                ctx.type = data.isBurst ? 1 : 0;
                ctx.reactionsOnMessage = int(msg.reactions->size());
                const auto users = plugins->dispatchReactionUsers(ctx);
                if (!users)
                    continue;
                data.usersClickable = users->clickable;
                for (const auto id : users->users) {
                    if (filtering) {
                        Qord::Plugins::MessageContext author;
                        author.accountId = ctx.accountId;
                        author.channelId = ctx.channelId;
                        author.guildId = ctx.guildId;
                        author.authorId = id;
                        if (plugins->dispatchShouldHideMessage(author))
                            continue;
                    }
                    data.userIds.append(Core::Snowflake(id));
                }
                data.moreUsers = qMax(0, int(data.userIds.size()) - ReactionAvatarMax);
                for (int i = 0; i < qMin(int(data.userIds.size()), ReactionAvatarMax); ++i) {
                    const QUrl url = userAvatarResolver ? userAvatarResolver(data.userIds[i], currentGuildId) : QUrl();
                    const QSize size(32, 32);
                    if (url.isValid() && !imageManager->isCached(url, size))
                        reactionAvatarUrls.insert(url);
                    data.userAvatars.append(url.isValid() ? imageManager->get(url, size, currentAccountId) : QPixmap());
                }
            }
        }

        return QVariant::fromValue(result);
    }
    case IsSystemMessageRole:
        return isSystemMessageType(msg.type);
    case MessageTypeRole:
        return static_cast<int>(msg.type.get());
    case IsForwardedRole:
        return msg.isForwarded();
    case ForwardOriginRole: {
        if (!msg.isForwarded() || !msg.messageReference->channelId.hasValue())
            return {};

        auto cached = forwardOriginCache.constFind(msg.id);
        if (cached == forwardOriginCache.constEnd())
            cached = forwardOriginCache.insert(msg.id, forwardOrigin(msg));

        ForwardOriginData origin = cached.value();
        if (origin.text.isEmpty())
            return {};
        if (origin.iconUrl.isValid())
            origin.icon = imageManager->get(origin.iconUrl, QSize(16, 16), currentAccountId);
        return QVariant::fromValue(origin);
    }
    case ReplyDataRole: {
        ReplyData reply;

        // Qord: "<user> used /command" above a slash command's answer, like Discord
        if (msg.type == Discord::MessageType::CHAT_INPUT_COMMAND && !msg.interactionName.isEmpty()) {
            reply.state = ReplyData::State::Present;
            reply.authorId = msg.interactionUserId;
            reply.authorName = msg.interactionUserName;
            reply.authorColor = msg.interactionUserId.isValid() && roleColorResolver
                                        ? roleColorResolver(msg.interactionUserId, currentGuildId)
                                        : QColor();
            reply.contentSnippet = tr("used /%1").arg(msg.interactionName);
            return QVariant::fromValue(reply);
        }
        if (msg.type != Discord::MessageType::REPLY) {
            reply.state = ReplyData::State::None;
            return QVariant::fromValue(reply);
        }

        if (!msg.referencedMessage) {
            if (msg.referencedMessageNull) {
                reply.state = ReplyData::State::Deleted;
            } else {
                reply.state = ReplyData::State::Unknown;
            }
            if (msg.messageReference.hasValue() && msg.messageReference->messageId.hasValue())
                reply.referencedMessageId = *msg.messageReference->messageId;
            return QVariant::fromValue(reply);
        }

        const auto &ref = msg.referencedMessage;
        reply.state = ReplyData::State::Present;
        reply.referencedMessageId = ref->id;
        reply.authorId = ref->author->id;
        const Discord::Message &refVisible = ref->contentMessage();
        reply.contentSnippet = refVisible.content;
        if (reply.contentSnippet.isEmpty() && refVisible.stickerItems.hasValue() && !refVisible.stickerItems->isEmpty())
            reply.contentSnippet = tr("Click to see sticker");

        reply.authorColor = resolveAuthorColor(ref->author.get());
        reply.authorName = replyNameResolver ? replyNameResolver(ref->author.get(), currentGuildId) : QString();
        if (reply.authorName.isEmpty())
            reply.authorName = resolveAuthorName(ref->author.get());

        return QVariant::fromValue(reply);
    }
    default:
        return {};
    }
}

bool ChatModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid())
        return false;

    if (role == CachedSizeRole) {
        auto &msg = messages[index.row()];
        sizeCache[msg.id] = value.toSize();
        return true;
    }

    return false;
}

Snowflake ChatModel::getOldestMessageId() const
{
    if (messages.isEmpty())
        return Snowflake::Invalid;
    return messages.first().id;
}

Snowflake ChatModel::getNewestMessageId() const
{
    if (messages.isEmpty())
        return Snowflake::Invalid;
    return messages.last().id;
}

int ChatModel::rowForMessage(Snowflake messageId) const
{
    for (int row = 0; row < messages.size(); ++row) {
        if (messages[row].id == messageId)
            return row;
    }
    return -1;
}

Snowflake ChatModel::getActiveChannelId() const
{
    return currentChannelId;
}

Snowflake ChatModel::getActiveGuildId() const
{
    return currentGuildId;
}

void ChatModel::setAtLatest(bool value)
{
    if (atLatest == value)
        return;
    atLatest = value;
    emit atLatestChanged(value);
}

void ChatModel::resetMessages(const QVector<Discord::Message> &sortedIncoming)
{
    beginResetModel();
    sizeCache.clear();
    embedCache.clear();
    forwardOriginCache.clear();
    docCache.clear();
    pendingNonces.clear();
    erroredNonces.clear();
    uploadProgress.clear();
    localPixmapCache.clear();
    previewPixmapCache.clear();
    mediaFlagsCache.clear();
    messages = sortedIncoming;
    endResetModel();
}

void ChatModel::appendMessages(const QVector<Discord::Message> &sortedIncoming)
{
    if (sortedIncoming.isEmpty())
        return;

    beginInsertRows({}, messages.size(), messages.size() + sortedIncoming.size() - 1);
    messages += sortedIncoming;
    endInsertRows();
}

void ChatModel::handleIncomingMessages(const Core::MessageRequestResult &result)
{
    if (!result.success)
        return;

    if (result.channelId != currentChannelId)
        return;

    QVector<Discord::Message> incomingMessages{ result.messages.cbegin(), result.messages.cend() };
    std::sort(incomingMessages.begin(), incomingMessages.end(),
              [](const Discord::Message &a, const Discord::Message &b) {
                  return a.id.get() < b.id.get();
              });

    switch (result.type) {
    case Discord::Client::MessageLoadType::Latest:
        resetMessages(incomingMessages);
        setAtLatest(true);
        break;
    case Discord::Client::MessageLoadType::Jump:
        if (incomingMessages.isEmpty())
            break;
        resetMessages(incomingMessages);
        setAtLatest(result.reachedLatest);
        break;
    case Discord::Client::MessageLoadType::Future:
        if (result.anchorId != getNewestMessageId())
            break;
        appendMessages(incomingMessages);
        if (result.reachedLatest)
            setAtLatest(true);
        break;
    case Discord::Client::MessageLoadType::History: {
        if (incomingMessages.isEmpty() || result.anchorId != getOldestMessageId())
            break;

        int numNew = incomingMessages.size();

        const Snowflake oldAnchorId = messages.first().id;

        beginInsertRows({}, 0, numNew - 1);
        messages = incomingMessages + messages;
        endInsertRows();

        // invalidate cached size cuz header and/or separator might have moved
        sizeCache.remove(oldAnchorId);
        QModelIndex oldAnchorIdx = index(numNew, 0);
        emit dataChanged(oldAnchorIdx, oldAnchorIdx,
                         { CachedSizeRole, ShowHeaderRole, DateSeparatorRole });

        break;
    }
    case Discord::Client::MessageLoadType::Created: {
        if (incomingMessages.isEmpty())
            break;

        bool isUpdate = false;
        for (const auto &incomingMsg : incomingMessages) {
            for (int i = 0; i < messages.size(); i++) {
                // update
                if (messages[i].id == incomingMsg.id) {
                    messages[i] = incomingMsg;

                    sizeCache.remove(incomingMsg.id);
                    embedCache.remove(incomingMsg.id);
                    invalidateDocCacheForMessage(incomingMsg.id);

                    QModelIndex idx = index(i, 0);
                    emit dataChanged(idx, idx);
                    isUpdate = true;
                    break;
                }
            }
        }

        if (isUpdate)
            break;

        // replace sent message by nonce
        bool replacedPreview = false;
        for (const auto &incomingMsg : incomingMessages) {
            if (incomingMsg.nonce.hasValue()) {
                QString nonce = incomingMsg.nonce.get();

                // todo qhash? probably doesnt matter at all
                for (int i = 0; i < messages.size(); i++) {
                    if (messages[i].nonce.hasValue() && messages[i].nonce.get() == nonce) {
                        prunePreviewCaches(messages[i]); // drop the pending preview's pixmaps
                        messages[i] = incomingMsg;
                        pendingNonces.remove(nonce);
                        uploadProgress.remove(nonce);
                        QModelIndex idx = index(i, 0);
                        emit dataChanged(idx, idx);
                        replacedPreview = true;
                        break;
                    }
                }
            }
        }

        if (!replacedPreview && atLatest) {
            beginInsertRows({}, messages.size(), messages.size() + incomingMessages.size() - 1);

            for (const auto &msg : incomingMessages) {
                if (msg.isPendingOutbound && msg.nonce.hasValue()) {
                    pendingNonces.insert(msg.nonce.get());
                }
            }

            messages = messages + incomingMessages;
            endInsertRows();
        }
        break;
    }
    default:
        break;
    }
}

void ChatModel::handleMessageDeleted(Snowflake channelId, Snowflake messageId)
{
    if (channelId != currentChannelId)
        return;

    for (int i = 0; i < messages.size(); i++) {
        if (messages[i].id == messageId) {
            if (messages[i].nonce.hasValue()) {
                pendingNonces.remove(messages[i].nonce.get());
                uploadProgress.remove(messages[i].nonce.get());
            }
            prunePreviewCaches(messages[i]); // cancelled/deleted preview won't render again
            beginRemoveRows({}, i, i);
            sizeCache.remove(messageId);
            embedCache.remove(messageId);
            invalidateDocCacheForMessage(messageId);
            messages.remove(i);
            endRemoveRows();

            // invalidate what came afterwards
            if (i < messages.size()) {
                const auto &nextMessage = messages[i];

                sizeCache.remove(nextMessage.id);
                embedCache.remove(nextMessage.id);
                invalidateDocCacheForMessage(nextMessage.id);

                QModelIndex idx = index(i, 0);
                emit dataChanged(idx, idx, { CachedSizeRole, ShowHeaderRole, DateSeparatorRole });
            }
            break;
        }
    }
}

void ChatModel::handleMessageErrored(const QString &nonce)
{
    for (int i = 0; i < messages.size(); i++) {
        if (messages[i].nonce.hasValue() && messages[i].nonce.get() == nonce) {
            pendingNonces.remove(nonce);
            erroredNonces.insert(nonce);
            uploadProgress.remove(nonce);
            QModelIndex idx = index(i, 0);
            emit dataChanged(idx, idx);
            break;
        }
    }
}

void ChatModel::handleUploadProgress(const QString &nonce, int fileIndex, qint64 sent, qint64 total)
{
    if (fileIndex < 0 || !pendingNonces.contains(nonce))
        return;

    auto &progress = uploadProgress[nonce];
    while (progress.size() <= fileIndex)
        progress.append({ -1, -1 });
    progress[fileIndex] = { sent, total };

    for (int i = 0; i < messages.size(); i++) {
        if (messages[i].nonce.hasValue() && messages[i].nonce.get() == nonce) {
            QModelIndex idx = index(i, 0);
            emit dataChanged(idx, idx, { AttachmentsRole });
            break;
        }
    }
}

QPixmap ChatModel::localPixmap(const QUrl &url, const QSize &displaySize) const
{
    auto it = localPixmapCache.constFind(url);
    if (it != localPixmapCache.constEnd())
        return it.value();

    QImageReader reader(url.toLocalFile());
    reader.setAutoTransform(true);
    QSize original = reader.size();
    if (original.isValid() && displaySize.isValid()) {
        QSize scaled = original.scaled(displaySize * qApp->devicePixelRatio(),
                                       Qt::KeepAspectRatio);
        if (scaled.width() < original.width())
            reader.setScaledSize(scaled);
    }

    QPixmap pixmap = QPixmap::fromImage(reader.read());
    if (!pixmap.isNull())
        pixmap.setDevicePixelRatio(qApp->devicePixelRatio());
    localPixmapCache.insert(url, pixmap);
    return pixmap;
}

QPixmap ChatModel::pixmapOrNullWhileLoading(const QUrl &url, const QSize &displaySize) const
{
    const QPixmap pixmap = suppressImageFetch ? imageManager->getIfCached(url, displaySize)
                                              : imageManager->get(url, displaySize, currentAccountId);
    return imageManager->isCached(url, displaySize) ? pixmap : QPixmap();
}

QPixmap ChatModel::previewPixmap(Snowflake attachmentId, const QImage &image,
                                 const QSize &displaySize) const
{
    auto it = previewPixmapCache.constFind(attachmentId);
    if (it != previewPixmapCache.constEnd())
        return it.value();

    qreal dpr = qApp->devicePixelRatio();
    QImage scaled = displaySize.isValid()
                            ? image.scaled(displaySize * dpr, Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation)
                            : image;
    QPixmap pixmap = QPixmap::fromImage(scaled);
    if (!pixmap.isNull())
        pixmap.setDevicePixelRatio(dpr);
    previewPixmapCache.insert(attachmentId, pixmap);
    return pixmap;
}

void ChatModel::prunePreviewCaches(const Discord::Message &msg)
{
    if (!msg.attachments.hasValue())
        return;
    for (const auto &att : *msg.attachments) {
        previewPixmapCache.remove(*att.id);
        if (att.proxyUrl.hasValue())
            localPixmapCache.remove(QUrl(*att.proxyUrl));
    }
}

void ChatModel::setAccount(Snowflake accountId)
{
    currentAccountId = accountId;
}

void ChatModel::setActiveChannel(Snowflake channelId, Snowflake guildId)
{
    if (currentChannelId == channelId)
        return;

    imageManager->unpinGroup(Core::PinGroup::ChatView);

    currentChannelId = channelId;
    currentGuildId = guildId;
    setAtLatest(true);

    beginResetModel();
    messages.clear();
    sizeCache.clear();
    embedCache.clear();
    forwardOriginCache.clear();
    docCache.clear();
    pendingNonces.clear();
    erroredNonces.clear();
    uploadProgress.clear();
    localPixmapCache.clear();
    previewPixmapCache.clear();
    mediaFlagsCache.clear();
    revealedSpoilers.clear();
    endResetModel();
}

void ChatModel::refreshUsersInView(const QList<Snowflake> &userIds)
{
    bool refreshAll = userIds.isEmpty();

    for (int row = 0; row < messages.size(); ++row) {
        const auto &msg = messages[row];
        if (!msg.author.hasValue())
            continue;

        Snowflake authorId = msg.author->id.get();

        if (refreshAll || userIds.contains(authorId)) {
            QModelIndex idx = index(row, 0);
            emit dataChanged(idx, idx, { UsernameRole, UsernameColorRole, AvatarRole });
        }
    }
}

void ChatModel::revealSpoiler(Snowflake attachmentId)
{
    if (revealedSpoilers.contains(attachmentId))
        return;

    revealedSpoilers.insert(attachmentId);

    for (int row = 0; row < messages.size(); ++row) {
        const auto &visible = messages[row].contentMessage();
        if (visible.attachments.hasValue()) {
            for (const auto &att : *visible.attachments) {
                if (*att.id == attachmentId) {
                    QModelIndex idx = index(row, 0);
                    emit dataChanged(idx, idx, { AttachmentsRole, CachedSizeRole });
                    return;
                }
            }
        }
    }
}

bool ChatModel::isSpoilerRevealed(Snowflake attachmentId) const
{
    return revealedSpoilers.contains(attachmentId);
}

ChatModel::MediaFlags ChatModel::mediaFlagsFor(const Discord::Attachment &att, const Discord::Message &msg) const
{
    auto cached = mediaFlagsCache.constFind(*att.id);
    if (cached != mediaFlagsCache.constEnd())
        return *cached;

    MediaFlags flags;
    flags.isImage = att.isImage();
    flags.contentType = att.contentType.hasValue() ? *att.contentType : QString();

    const QUrl url(att.url.hasValue() ? *att.url : QString());
    const bool voiceFlagged = msg.flags.hasValue() && msg.flags->testFlag(Discord::MessageFlag::IS_VOICE_MESSAGE);

    flags.isVideo = !flags.isImage && Core::Media::canPlay(flags.contentType, url);
    flags.isVoiceMessage = !msg.isPendingOutbound && Core::Media::isSupported() &&
                           !flags.isImage && !flags.isVideo && voiceFlagged &&
                           (flags.contentType.isEmpty() ||
                            flags.contentType.startsWith(QLatin1String("audio/")));
    flags.isAudio = flags.isVoiceMessage ||
                    (!msg.isPendingOutbound && !flags.isImage && !flags.isVideo &&
                     Core::Media::canPlayAudio(flags.contentType, url));

    if (att.durationSecs.hasValue() && std::isfinite(*att.durationSecs) && *att.durationSecs > 0.0)
        flags.durationMs = static_cast<qint64>(qMin(*att.durationSecs, 86400.0) * 1000.0);

    if (!msg.isPendingOutbound)
        mediaFlagsCache.insert(*att.id, flags);

    return flags;
}

void ChatModel::setVideoNativeSize(Snowflake attachmentId, const QSize &size)
{
    if (size.isEmpty())
        return;
    if (videoNativeSizes.value(attachmentId) == size)
        return;

    videoNativeSizes.insert(attachmentId, size);

    for (int row = 0; row < messages.size(); ++row) {
        const auto &msg = messages[row];
        const auto &visible = msg.contentMessage();
        if (!visible.attachments.hasValue())
            continue;

        for (const auto &att : *visible.attachments) {
            if (*att.id != attachmentId)
                continue;

            sizeCache.remove(msg.id);
            QModelIndex idx = index(row, 0);
            emit dataChanged(idx, idx, { AttachmentsRole, CachedSizeRole });
            return;
        }
    }
}

QTextDocument *ChatModel::getCachedDocument(const DocCacheKey &key) const
{
    return docCache.object(key);
}

void ChatModel::cacheDocument(const DocCacheKey &key, QTextDocument *doc) const
{
    docCache.insert(key, doc);
}

void ChatModel::invalidateDocCache()
{
    docCache.clear();
    docCacheWidth = 0;
}

void ChatModel::invalidateLayout()
{
    invalidateDocCache();
    sizeCache.clear();
}

void ChatModel::revealSpoilers(Snowflake messageId)
{
    if (revealedSpoilerMessages.contains(messageId))
        return;
    revealedSpoilerMessages.insert(messageId);
    invalidateDocCacheForMessage(messageId);
    for (int row = 0; row < rowCount(); ++row) {
        const QModelIndex idx = index(row, 0);
        if (Snowflake(idx.data(MessageIdRole).toULongLong()) == messageId) {
            emit dataChanged(idx, idx);
            break;
        }
    }
}

void ChatModel::invalidateDocCacheForMessage(Snowflake messageId)
{
    docCache.remove(bodyDocKey(messageId));

    // brute remove all possible entries
    for (int ei = 0; ei < 10; ++ei) {
        docCache.remove(embedTitleDocKey(messageId, ei));
        docCache.remove(embedDescDocKey(messageId, ei));
        for (int fi = 0; fi < 25; ++fi) {
            docCache.remove(embedFieldNameDocKey(messageId, ei, fi));
            docCache.remove(embedFieldValueDocKey(messageId, ei, fi));
        }
    }
}

void ChatModel::rowVisibilityChanged(int row)
{
    for (int next = row + 1; next < int(messages.size()); ++next) {
        if (hiddenRowCheck && hiddenRowCheck(next))
            continue;
        sizeCache.remove(messages[next].id);
        const QModelIndex idx = index(next, 0);
        emit dataChanged(idx, idx, { CachedSizeRole, ShowHeaderRole });
        break;
    }
}

bool ChatModel::isFirstUnread(int row) const
{
    if (!unreadMarkerAfter.isValid() || row <= 0 || row >= messages.size())
        return false;
    // Qord: the divider goes above the first newer message from someone else;
    // your own (a preview, a message from another device) are never new
    const auto isOwn = [this](int r) {
        return messages[r].author.hasValue() && messages[r].author->id.get() == currentAccountId;
    };
    if (quint64(messages[row].id.get()) <= quint64(unreadMarkerAfter) || isOwn(row))
        return false;
    for (int prev = row - 1; prev >= 0; --prev) {
        if (quint64(messages[prev].id.get()) <= quint64(unreadMarkerAfter))
            return true;
        if (!isOwn(prev))
            return false;
    }
    return false;
}

void ChatModel::setUnreadMarker(Snowflake lastReadId)
{
    if (unreadMarkerAfter == lastReadId)
        return;
    unreadMarkerAfter = lastReadId;
    invalidateLayout();
    if (!messages.isEmpty())
        emit dataChanged(index(0, 0), index(int(messages.size()) - 1, 0),
                         { CachedSizeRole, ShowHeaderRole, DateSeparatorRole, NewMarkerRole });
}

} // namespace UI
} // namespace Acheron
