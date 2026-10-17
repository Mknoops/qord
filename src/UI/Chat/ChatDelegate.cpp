#include "ChatDelegate.hpp"
#include "Qord/Preferences.hpp"
#include "Core/RoleColors.hpp"

#include <QLocale>
#include <QPainterPath>
#include <QSvgRenderer>

#include "ChatModel.hpp"
#include "ChatLayout.hpp"
#include "ChatView.hpp"
#include "Core/LottieDecoder.hpp"
#include "Core/Theme/Icons.hpp"
#include "Core/Theme/Manager.hpp"
#include "Core/Media/Player.hpp"
#include "Core/Media/PlayerPool.hpp"
#include "UI/Chat/FrameAnimator.hpp"
#include "UI/Chat/EmojiTextObject.hpp"
#include "UI/Chat/InlineVideoController.hpp"
#include "UI/Chat/MediaTarget.hpp"
#include "UI/Chat/VideoControls.hpp"
#include "UI/Qord/NameDecorations.hpp"
#include "Core/RoleStyle.hpp"

#include <QCache>

#include <algorithm>

namespace Acheron {
namespace UI {

ChatDelegate::ChatDelegate(Core::ImageManager *imageManager, ChatView *view)
    : QStyledItemDelegate(view),
      emojiHandler(new EmojiTextObject(imageManager, view->frameAnimator(), this))
{
}

static void drawHighlightFlash(QPainter *painter, const QStyleOptionViewItem &option, const ChatView *view, int row, const QRect &rect)
{
    if (!view || view->highlightedRow() != row)
        return;
    QColor flash = option.palette.highlight().color();
    flash.setAlphaF(0.3 * view->highlightOpacity());
    painter->fillRect(rect, flash);
}

static void drawHoverHighlight(QPainter *painter, const QStyleOptionViewItem &option, const ChatView *view, int row, const QRect &rect)
{
    if (!view || view->hoveredMessageRowAtPaint() != row)
        return;
    QColor tint = option.palette.text().color();
    tint.setAlpha(14);
    painter->fillRect(rect, tint);
}

static QColor bodyTextColor(const QStyleOptionViewItem &option, const QModelIndex &index)
{
    if (index.data(ChatModel::IsErroredRole).toBool())
        return Core::Theme::Manager::instance().color(Core::Theme::Token::ChatError);
    if (index.data(ChatModel::IsPendingRole).toBool())
        return option.palette.text().color().lighter(50);
    if (index.data(ChatModel::IsSystemMessageRole).toBool()) {
        QColor color = option.palette.text().color();
        color.setAlpha(140);
        return color;
    }
    const QColor text = (option.state & QStyle::State_Selected) ? option.palette.highlightedText().color()
                                                                 : option.palette.text().color();
    // Qord: RoleColorEverywhere tints the text with the author's role color
    // (not for failed sends, which would no longer stand out)
    const auto &roleColors = Core::roleColorOptions();
    if (roleColors.messages && roleColors.messageSaturation > 0) {
        const QColor author = index.data(ChatModel::UsernameColorRole).value<QColor>();
        if (author.isValid())
            return Core::mixOklab(author, roleColors.messageSaturation, text);
    }
    return text;
}

static void applyTextSelection(QAbstractTextDocumentLayout::PaintContext &paintCtx, const ChatView *view,
                               QTextDocument *doc, const QModelIndex &index, const QPalette &palette)
{
    if (!view || !view->hasTextSelection())
        return;

    const auto start = view->selectionStart();
    const auto end = view->selectionEnd();
    const int r = index.row();
    if (r < start.row || r > end.row)
        return;

    int startChar = 0;
    int endChar = -1;
    if (r == start.row)
        startChar = start.index;
    if (r == end.row)
        endChar = end.index;

    QTextCursor cursor(doc);
    cursor.setPosition(startChar);
    if (endChar == -1)
        cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    else
        cursor.setPosition(endChar, QTextCursor::KeepAnchor);

    QAbstractTextDocumentLayout::Selection sel;
    sel.cursor = cursor;
    sel.format.setBackground(palette.highlight());
    sel.format.setForeground(palette.highlightedText());
    paintCtx.selections.append(sel);
}

static void drawBodyDocument(QPainter *painter, QTextDocument *doc, const QRect &textRect, const QRectF &clip,
                             const QStyleOptionViewItem &option, const QModelIndex &index, const ChatView *view)
{
    painter->save();
    painter->translate(textRect.topLeft());

    QAbstractTextDocumentLayout::PaintContext paintCtx;
    paintCtx.clip = clip;
    paintCtx.palette.setColor(QPalette::Text, bodyTextColor(option, index));
    applyTextSelection(paintCtx, view, doc, index, option.palette);
    doc->documentLayout()->draw(painter, paintCtx);

    painter->restore();
}

bool ChatDelegate::paintBodyTextOnly(QPainter *painter, const QStyleOptionViewItem &option,
                                     const QModelIndex &index, const ChatModel *chatModel,
                                     const ChatView *chatView, const QRect &textRect, const QRect &damage) const
{
    const Snowflake msgId = index.data(ChatModel::MessageIdRole).toULongLong();
    QTextDocument *doc = chatModel->getCachedDocument(bodyDocKey(msgId));
    if (!doc)
        return false;

    drawHoverHighlight(painter, option, chatView, index.row(), damage);
    drawHighlightFlash(painter, option, chatView, index.row(), damage);
    drawBodyDocument(painter, doc, textRect, damage.translated(-textRect.topLeft()), option, index, chatView);
    return true;
}

static void drawSticker(QPainter *painter, const QRect &rect, const QPixmap &pixmap)
{
    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->drawPixmap(VideoControls::fitRect(pixmap.size(), rect), pixmap);
    painter->restore();
}

static void drawUnplayableSticker(QPainter *painter, const QStyleOptionViewItem &option, const QRect &rect, const QString &name)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(option.palette.mid().color(), 1));
    painter->setBrush(option.palette.alternateBase());
    painter->drawRoundedRect(rect, 8, 8);

    painter->setFont(option.font);
    painter->setPen(option.palette.placeholderText().color());
    const QRect textRect = rect.adjusted(8, 8, -8, -8);
    painter->drawText(textRect, Qt::AlignCenter | Qt::TextWordWrap, ChatDelegate::tr("Sticker: %1").arg(name));
    painter->restore();
}

struct ResolvedSticker
{
    QPixmap pixmap;
    bool unplayable = false;
};

static ResolvedSticker resolveSticker(const StickerData &sticker, const QRect &rect, FrameAnimator *animator, Snowflake accountId)
{
    if (sticker.unavailable || (!sticker.hasRasterStill() && !Core::Lottie::isSupported()))
        return { {}, true };

    QPixmap frame;
    if (animator && sticker.isAnimated())
        frame = animator->frame(sticker.animatedUrl, rect.size(), accountId, rect, AnimatedKind::Sticker);
    if (!frame.isNull())
        return { frame };
    if (sticker.hasRasterStill())
        return { sticker.still };
    if (!animator)
        return {};

    const Core::AnimatedFramesPtr decoded = animator->framesOnceDecoded(sticker.animatedUrl, rect.size(), accountId);
    if (!decoded)
        return {};
    if (decoded->frames.isEmpty())
        return { {}, true };
    return { decoded->frames.first() };
}

static void drawSystemMessageIcon(QPainter *painter, const QStyleOptionViewItem &option, Discord::MessageType type, const QRect &rect)
{
    using Discord::MessageType;

    QColor color = option.palette.text().color();
    qreal opacity = 0.55;
    if (type == MessageType::USER_JOIN || type == MessageType::RECIPIENT_ADD) {
        color = QColor(0x3b, 0xa5, 0x5c);
        opacity = 1.0;
    } else if (type == MessageType::RECIPIENT_REMOVE) {
        color = Core::Theme::Manager::instance().color(Core::Theme::Token::ChatError);
        opacity = 1.0;
    }

    const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    const QPixmap icon = Core::Theme::Icons::pixmap(ChatLayout::systemMessageIcon(type), rect.width(), color, dpr);

    painter->save();
    painter->setOpacity(opacity);
    painter->drawPixmap(rect, icon);
    painter->restore();
}

static void drawUploadProgress(QPainter *painter, const QRect &barRect, qint64 sent, qint64 total, const QPalette &palette)
{
    qreal fraction = total > 0 ? qBound(0.0, qreal(sent) / qreal(total), 1.0) : 0.0;

    if (barRect.width() < 20)
        return;

    int radius = barRect.height() / 2;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 160));
    painter->drawRoundedRect(barRect, radius, radius);
    if (fraction > 0.0) {
        QRect fillRect = barRect;
        fillRect.setWidth(qMax(barRect.height(), qRound(fraction * barRect.width())));
        painter->setBrush(palette.highlight());
        painter->drawRoundedRect(fillRect, radius, radius);
    }
    painter->restore();
}

static QPixmap cachedBlur(const QPixmap &source)
{
    static constexpr int MaxBlurCacheKb = 16 * 1024;
    static QCache<qint64, QPixmap> cache(MaxBlurCacheKb);

    const qint64 key = source.cacheKey();
    if (const QPixmap *cached = cache.object(key))
        return *cached;

    const QPixmap blurred = ChatLayout::createBlurredPixmap(source, 60);
    const int costKb = qMax(1, static_cast<int>(blurred.width()) * blurred.height() * blurred.depth() / 8 / 1024);
    cache.insert(key, new QPixmap(blurred), costKb);

    return blurred;
}

static void paintSpoilerOverlay(QPainter *painter, const QRect &rect, const QFont &baseFont)
{
    painter->fillRect(rect, QColor(0, 0, 0, 100));

    QFont spoilerFont = baseFont;
    spoilerFont.setBold(true);
    spoilerFont.setPointSize(spoilerFont.pointSize() + 2);
    painter->setFont(spoilerFont);
    painter->setPen(Qt::white);
    painter->drawText(rect, Qt::AlignCenter, ChatDelegate::tr("SPOILER"));
}

static void paintMediaTarget(QPainter *painter,
                             const QStyleOptionViewItem &option,
                             const MediaTarget &target,
                             const InlineVideoController *video)
{
    if (target.rect.isEmpty())
        return;

    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (target.spoilered) {
        if (target.isAudio()) {
            VideoControls::paintAudioBase(painter, target.rect);
        } else {
            painter->fillRect(target.rect, Qt::black);
            if (!target.poster.isNull())
                ChatLayout::drawCroppedPixmap(painter, target.rect, cachedBlur(target.poster));
        }
        paintSpoilerOverlay(painter, target.rect, option.font);
        painter->restore();
        return;
    }

    Core::Media::Player *player = video ? video->playerFor(target.key) : nullptr;

    if (!target.isAudio()) {
        painter->fillRect(target.rect, Qt::black);

        const QImage frame = player ? player->currentFrame() : QImage();
        if (!frame.isNull())
            painter->drawImage(VideoControls::fitRect(frame.size(), target.rect), frame);
        else if (!target.poster.isNull())
            ChatLayout::drawCroppedPixmap(painter, target.rect, target.poster);
    }

    const auto session = VideoControls::sessionFor(player,
                                                   target.rect,
                                                   target.info(),
                                                   video && video->volumeExpandedFor(target.key));

    VideoControls::paintPlaybackStatus(painter, target.rect, session.state);

    const bool hovered = video && video->isHovered(target.key);
    if (target.isAudio() || hovered || !session.state.playing)
        VideoControls::paint(painter, session.layout, session.state);

    if (target.uploadSent >= 0) {
        const QRect barRect(target.rect.left() + 8,
                            target.rect.bottom() - 13,
                            target.rect.width() - 16,
                            6);
        drawUploadProgress(painter, barRect, target.uploadSent, target.uploadTotal, option.palette);
    }

    painter->restore();
}

void ChatDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                         const QModelIndex &index) const
{
    painter->save();

    const auto *chatModel = qobject_cast<const ChatModel *>(index.model());
    if (chatModel)
        chatModel->suppressImageFetch = false;

    const auto *chatView = qobject_cast<const ChatView *>(option.widget);
    const InlineVideoController *video = chatView ? chatView->videoController() : nullptr;
    FrameAnimator *animator = chatView ? chatView->frameAnimator() : nullptr;

    if (animator && !animator->intersectsPaintDamage(option.rect)) {
        painter->restore();
        return;
    }

    if (video) {
        if (const auto surface = video->surfaceCoveringDamage(index); surface && !surface->isAudio()) {
            paintMediaTarget(painter, option, *surface, video);
            painter->restore();
            return;
        }
    }

    if (chatModel)
        emojiHandler->setAccountId(chatModel->getAccountId());

    if (animator && chatModel) {
        const auto repaint = animator->bodyOnlyRepaint(index.row(), option.rect);
        if (repaint && paintBodyTextOnly(painter, option, index, chatModel, chatView, repaint->textRect, repaint->damage)) {
            painter->restore();
            return;
        }
    }

    if (animator) {
        if (const auto stickers = animator->stickerOnlyRepaint(index.row(), option.rect)) {
            for (const auto &sticker : *stickers) {
                drawHoverHighlight(painter, option, chatView, index.row(), sticker.rect);
                drawHighlightFlash(painter, option, chatView, index.row(), sticker.rect);
                drawSticker(painter, sticker.rect, sticker.frame);
            }
            painter->restore();
            return;
        }
    }

    ChatLayout::LayoutContext ctx = ChatLayout::buildContext(index, option.font, option.rect, option.palette);
    ChatLayout::MessageLayout layout = ChatLayout::calculateMessageLayout(ctx);

    if (animator)
        animator->beginRow(index.row(), option.rect, layout.textRect);

    QRect messageRect = option.rect;
    if (layout.hasSeparator)
        messageRect.setTop(layout.separatorRect.bottom() + 1);
    if (index.data(ChatModel::MentionsMeRole).toBool() && Qord::Preferences::flag(Qord::Pref::HighlightMentions)) {
        // Qord: Discord's gold highlight and bar on messages that mention you
        const auto &theme = Core::Theme::Manager::instance();
        painter->fillRect(messageRect, theme.color(Core::Theme::Token::MentionedMessageBg));
        painter->fillRect(QRect(messageRect.left(), messageRect.top(), 2, messageRect.height()),
                          theme.color(Core::Theme::Token::MentionedMessageBar));
    }
    drawHoverHighlight(painter, option, chatView, index.row(), messageRect);
    drawHighlightFlash(painter, option, chatView, index.row(), option.rect);

    const QString username = index.data(ChatModel::UsernameRole).toString();
    const QPixmap avatar = qvariant_cast<QPixmap>(index.data(ChatModel::AvatarRole));
    const QDateTime timestamp = index.data(ChatModel::TimestampRole).toDateTime().toLocalTime();

// debug paint
#if 0
    painter->setPen(Qt::red);
    painter->drawRect(layout.rowRect);
    if (layout.hasSeparator) {
        painter->setPen(Qt::yellow);
        painter->drawRect(layout.separatorRect);
    }
    if (layout.showHeader) {
        painter->setPen(Qt::green);
        painter->drawRect(layout.avatarRect);
        painter->setPen(Qt::green);
        painter->drawRect(layout.headerRect);
    }
    painter->setPen(Qt::white);
    painter->drawRect(layout.textRect);
#endif

    if (layout.hasSeparator) {
        // Qord: Discord's dividers. A date gets a thin line with the date in the
        // middle; the first unread message gets a red line with a NEW tag.
        const auto &theme = Core::Theme::Manager::instance();
        const bool isNew = index.data(ChatModel::NewMarkerRole).toBool();
        const QDate previousDate = index.row() == 0 ? QDate()
                                                    : index.model()
                                                              ->index(index.row() - 1, 0)
                                                              .data(ChatModel::TimestampRole)
                                                              .toDateTime()
                                                              .toLocalTime()
                                                              .date();
        const bool dateChanged = !previousDate.isValid() || previousDate != timestamp.date();
        const QColor newRed(0xf2, 0x3f, 0x43);
        const QColor lineColor = isNew ? newRed : theme.color(Core::Theme::Token::MutedText).darker(220);
        const int midY = layout.separatorRect.center().y();
        const int left = layout.separatorRect.left() + 16;
        int right = layout.separatorRect.right() - 14;

        QFont tagFont = option.font;
        tagFont.setPixelSize(10);
        tagFont.setBold(true);
        const QFontMetrics tagFm(tagFont);
        QRect newTag;
        if (isNew) {
            const int tagWidth = tagFm.horizontalAdvance(QStringLiteral("NEW")) + 8;
            newTag = QRect(right - tagWidth, midY - 7, tagWidth, 14);
            right = newTag.left();
        }
        painter->setPen(QPen(lineColor, 1));
        painter->drawLine(left, midY, right, midY);

        if (dateChanged) {
            QFont dateFont = option.font;
            dateFont.setPixelSize(12);
            dateFont.setWeight(QFont::DemiBold);
            const QString dateText = QLocale().toString(timestamp.date(), QLocale::LongFormat);
            const QFontMetrics dateFm(dateFont);
            const int textWidth = dateFm.horizontalAdvance(dateText) + 12;
            const QRect textBgRect(layout.separatorRect.center().x() - textWidth / 2, midY - dateFm.height() / 2,
                                   textWidth, dateFm.height());
            painter->fillRect(textBgRect, option.palette.base());
            painter->setFont(dateFont);
            painter->setPen(isNew ? newRed : theme.color(Core::Theme::Token::MutedText));
            painter->drawText(textBgRect, Qt::AlignCenter, dateText);
        }
        if (isNew) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(newRed);
            painter->drawRoundedRect(newTag, 3, 3);
            painter->setFont(tagFont);
            painter->setPen(Qt::white);
            painter->drawText(newTag, Qt::AlignCenter, QStringLiteral("NEW"));
            painter->restore();
        }
    }

    if (layout.hasReply && !layout.replyRect.isNull()) {
        ReplyData replyData = ctx.replyData;

        // Compute reply text font/metrics first so the connector aligns with the text
        QFont replyFont = option.font;
        replyFont.setPointSizeF(replyFont.pointSizeF() * 0.85);
        QFontMetrics replyFm(replyFont);

        int textX = layout.replyRect.left() + 4;
        int textY = layout.replyRect.top();
        int availWidth = layout.replyRect.width() - 4;

        // The vertical center of the reply text line
        int textMidY = textY + replyFm.height() / 2;

        // Draw the reply connector line (L-shaped)
        QColor lineColor = option.palette.text().color();
        lineColor.setAlpha(80);
        QPen replyPen(lineColor, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter->setPen(replyPen);

        int lineX = layout.avatarRect.center().x();
        int lineBottom = layout.avatarRect.top();
        int cornerRadius = 5;

        QPainterPath replyPath;
        replyPath.moveTo(lineX, lineBottom);
        replyPath.lineTo(lineX, textMidY + cornerRadius);
        replyPath.quadTo(lineX, textMidY, lineX + cornerRadius, textMidY);
        replyPath.lineTo(layout.replyRect.left(), textMidY);
        painter->drawPath(replyPath);

        // Draw reply text
        painter->setFont(replyFont);

        QColor replyTextColor = option.palette.text().color();
        bool replyHovered = chatView && chatView->hoveredRowAtPaint() == index.row() && chatView->replyBarHoveredAtPaint();
        replyTextColor.setAlpha(replyHovered ? 255 : 180);

        if (replyData.state == ReplyData::State::Present) {
            // Author name in bold, with role color if available
            QFont authorFont = replyFont;
            authorFont.setBold(true);
            painter->setFont(authorFont);
            QFontMetrics authorFm(authorFont);
            QColor authorColor = replyData.authorColor.isValid() ? replyData.authorColor : replyTextColor;
            painter->setPen(authorColor);
            QString authorName = replyData.authorName;
            int authorWidth = authorFm.horizontalAdvance(authorName);
            painter->drawText(textX, textY + authorFm.ascent(), authorName);

            // Content snippet
            painter->setFont(replyFont);
            painter->setPen(replyTextColor);
            int snippetX = textX + authorWidth + 6;
            int snippetWidth = availWidth - authorWidth - 6;
            if (snippetWidth > 0) {
                QString snippet = replyData.contentSnippet;
                snippet.replace('\n', ' ');
                QString elidedSnippet = replyFm.elidedText(snippet, Qt::ElideRight, snippetWidth);
                painter->drawText(snippetX, textY + replyFm.ascent(), elidedSnippet);
            }
        } else if (replyData.state == ReplyData::State::Deleted) {
            painter->setPen(replyTextColor);
            painter->drawText(textX, textY + replyFm.ascent(),
                              tr("Original message was deleted"));
        } else {
            painter->setPen(replyTextColor);
            painter->drawText(textX, textY + replyFm.ascent(),
                              tr("Unknown message"));
        }
    }

    if (ctx.isSystemMessage)
        drawSystemMessageIcon(painter, option, ctx.messageType, layout.systemIconRect);

    if (layout.showHeader) {
        if (!avatar.isNull()) {
            // Qord: round avatars, like Discord
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setRenderHint(QPainter::SmoothPixmapTransform);
            QPainterPath clip;
            clip.addEllipse(QRectF(layout.avatarRect));
            painter->setClipPath(clip);
            painter->drawPixmap(layout.avatarRect, avatar);
            painter->restore();
        }

        QFont headerFont = option.font;
        headerFont.setBold(true);
        painter->setFont(headerFont);
        QFontMetrics headerFm(headerFont);

        // Qord: the role color is on the name, in a dot after it, or both
        // (Discord's Role Colors setting, BetterRoleDot)
        const QColor roleColor = index.data(ChatModel::UsernameColorRole).value<QColor>();
        QColor headerColor;
        if (option.state & QStyle::State_Selected) {
            headerColor = option.palette.highlightedText().color();
        } else {
            headerColor = roleColor.isValid() && Core::roleColorOnName() ? roleColor : option.palette.text().color();
        }

        painter->setPen(headerColor);
        painter->drawText(layout.headerRect, Qt::AlignLeft | Qt::AlignTop, username);

        // Qord: after the name come the role dot, Discord's APP tag for bots,
        // plugin icons (PlatformIndicators), then "Today at 3:45 PM"
        QList<HeaderTip> tips;
        int afterName = layout.headerRect.left() + headerFm.horizontalAdvance(username);
        const int lineCenter = layout.headerRect.top() + headerFm.height() / 2;
        if (roleColor.isValid() && Core::roleDotAfterName()) {
            const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
            const QRect dotRect(afterName + 4, lineCenter - Core::RoleDotSize / 2, Core::RoleDotSize,
                                Core::RoleDotSize);
            painter->drawPixmap(dotRect, Core::roleDotPixmap(roleColor, dpr));
            if (Core::roleDotOptions().enabled)
                tips.append({ dotRect, QString(), QString(), QString(), 0, roleColor.name() });
            afterName = dotRect.right() + 1;
        }
        const Snowflake headerMessageId = index.data(ChatModel::MessageIdRole).toULongLong();
        const Discord::Message *headerMessage = chatModel ? chatModel->messageAt(index.row()) : nullptr;
        const bool isSystemUser = headerMessage && headerMessage->author.hasValue() && headerMessage->author->isSystem();
        const bool isBot = isSystemUser || (headerMessage && headerMessage->author.hasValue() &&
                                            headerMessage->author->bot.hasValue() && headerMessage->author->bot.get());
        if (isBot) {
            QFont tagFont = option.font;
            tagFont.setPixelSize(10);
            tagFont.setBold(true);
            // Discord's own account says SYSTEM, with the verified check
            const QString tag = isSystemUser ? QStringLiteral("\u2713 SYSTEM") : QStringLiteral("APP");
            const QFontMetrics tagFm(tagFont);
            const QRect tagRect(afterName + 4, lineCenter - 8, tagFm.horizontalAdvance(tag) + 8, 15);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(Core::Theme::Manager::instance().color(Core::Theme::Token::Highlight));
            painter->drawRoundedRect(tagRect, 3, 3);
            painter->setPen(Qt::white);
            painter->setFont(tagFont);
            painter->drawText(tagRect, Qt::AlignCenter, tag);
            painter->restore();
            afterName = tagRect.right() + 1;
        }
        if (auto *provider = NameDecorations::instance(); provider && headerMessage && headerMessage->author.hasValue()) {
            const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
            const auto decorations =
                    provider->forUser(chatModel->getAccountId(), chatModel->getActiveGuildId(),
                                      headerMessage->author->id.get(), NameDecorations::Place::Message, dpr);
            const QList<QRect> rects = NameDecorations::paint(*painter, decorations, afterName, lineCenter);
            for (int i = 0; i < rects.size(); ++i)
                tips.append({ rects[i], decorations[i].tooltip, decorations[i].pluginId, decorations[i].clickId,
                              quint64(headerMessage->author->id.get()) });
            afterName += NameDecorations::width(decorations);
        }

        QFont timestampFont = option.font;
        timestampFont.setPixelSize(12);
        painter->setFont(timestampFont);
        const QFontMetrics timestampFm(timestampFont);

        const QString timestampText = headerTimestamp(timestamp);
        const int timestampX = afterName + 8;
        const QRect timestampRect(timestampX, layout.headerRect.top(), timestampFm.horizontalAdvance(timestampText),
                                  headerFm.height());
        painter->setPen(Core::Theme::Manager::instance().color(Core::Theme::Token::MutedText));
        painter->drawText(timestampRect, Qt::AlignLeft | Qt::AlignVCenter, timestampText);
        tips.append({ timestampRect, QLocale().toString(timestamp.date(), QLocale::LongFormat) + QLatin1Char(' ') +
                                             Qord::Preferences::timeText(timestamp.time()) });
        if (lastHeaderTips.size() > 512)
            lastHeaderTips.clear();
        lastHeaderTips.insert(quint64(headerMessageId), tips);
    }

    Snowflake msgId = index.data(ChatModel::MessageIdRole).toULongLong();

    QFont bodyFont = option.font;
    if (ctx.isSystemMessage)
        bodyFont.setItalic(true);

    DocCacheKey bodyKey = bodyDocKey(msgId);
    QTextDocument *doc = chatModel->getCachedDocument(bodyKey);
    if (!doc) {
        doc = new QTextDocument;
        emojiHandler->install(*doc);
        ChatLayout::setupDocument(*doc, ctx.htmlContent, bodyFont, layout.textRect.width());
        chatModel->cacheDocument(bodyKey, doc);
    } else if (int(doc->textWidth()) != layout.textRect.width()) {
        doc->setTextWidth(layout.textRect.width());
    }

    drawBodyDocument(painter, doc, layout.textRect, QRectF(), option, index, chatView);

    painter->restore();
    painter->save();

    const bool isPending = index.data(ChatModel::IsPendingRole).toBool();
    QList<AttachmentData> attachments = ctx.attachments;

    for (const auto &imgLayout : layout.imageLayouts) {
        if (imgLayout.index >= attachments.size())
            continue;

        const auto &att = attachments[imgLayout.index];
        bool isSingleImage =
                (layout.imageLayouts.size() == 1 &&
                 std::count_if(attachments.begin(), attachments.end(),
                               [](const AttachmentData &a) { return a.isMedia(); }) == 1);

        bool showBlurred = att.isSpoiler;
        if (showBlurred && chatModel->isSpoilerRevealed(att.id))
            showBlurred = false;

        if (att.isVideo) {
            if (auto target = MediaTargets::forAttachment(att, imgLayout.rect, chatModel))
                paintMediaTarget(painter, option, *target, video);
            continue;
        }

        if (!att.pixmap.isNull()) {
            QPixmap displayPixmap = att.pixmap;

            if (showBlurred)
                displayPixmap = cachedBlur(att.pixmap);

            if (isSingleImage)
                painter->drawPixmap(imgLayout.rect, displayPixmap);
            else
                ChatLayout::drawCroppedPixmap(painter, imgLayout.rect, displayPixmap);

            if (showBlurred)
                paintSpoilerOverlay(painter, imgLayout.rect, option.font);
        } else {
            painter->fillRect(imgLayout.rect, QColor(60, 60, 60));
            painter->setPen(option.palette.text().color());
            painter->drawText(imgLayout.rect, Qt::AlignCenter, tr("Loading..."));
        }

        if (isPending && att.uploadSent >= 0) {
            QRect barRect(imgLayout.rect.left() + 8, imgLayout.rect.bottom() - 13,
                          imgLayout.rect.width() - 16, 6);
            drawUploadProgress(painter, barRect, att.uploadSent, att.uploadTotal, option.palette);
        }
    }

    constexpr int fileAttachmentPadding = 8;

    for (const auto &fileLayout : layout.fileLayouts) {
        if (fileLayout.index >= attachments.size())
            continue;

        const auto &att = attachments[fileLayout.index];
        QRect fileRect = fileLayout.rect;

        if (att.isVoiceMessage) {
            if (auto target = MediaTargets::forAttachment(att, ChatLayout::audioBarRect(fileRect, true), chatModel))
                paintMediaTarget(painter, option, *target, video);
            continue;
        }

        // Qord: Discord's text file preview
        if (att.isText && att.textLoaded) {
            paintTextPreview(painter, option, fileRect, att);
            continue;
        }

        QColor bgColor = option.palette.alternateBase().color();
        painter->fillRect(fileRect, bgColor);

        painter->setPen(QPen(option.palette.mid().color(), 1));
        painter->drawRect(fileRect);

        QRect iconRect(fileRect.left() + fileAttachmentPadding,
                       fileRect.top() + fileAttachmentPadding, 32, 32);
        painter->fillRect(iconRect, option.palette.mid());
        const qreal fileIconDpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        const QPixmap fileIcon = Core::Theme::Icons::pixmap(att.isAudio ? Core::Theme::Icons::Name::FileAudio
                                                                        : Core::Theme::Icons::Name::FileText,
                                                            20, option.palette.text().color(), fileIconDpr);
        QRect fileGlyphRect(0, 0, 20, 20);
        fileGlyphRect.moveCenter(iconRect.center());
        painter->drawPixmap(fileGlyphRect, fileIcon);

        int textLeft = iconRect.right() + fileAttachmentPadding;
        QRect textAreaRect(textLeft, fileRect.top() + fileAttachmentPadding,
                           fileRect.width() - (textLeft - fileRect.left()) - fileAttachmentPadding,
                           fileRect.height() - fileAttachmentPadding * 2);

        QFont filenameFont = option.font;
        painter->setFont(filenameFont);
        painter->setPen(option.palette.text().color());
        QFontMetrics filenameFm(filenameFont);
        QString elidedFilename =
                filenameFm.elidedText(att.filename, Qt::ElideMiddle, textAreaRect.width());
        painter->drawText(textAreaRect.left(), textAreaRect.top() + filenameFm.ascent(),
                          elidedFilename);

        bool uploading = isPending && att.uploadSent >= 0;

        QFont sizeFont = option.font;
        sizeFont.setPointSize(sizeFont.pointSize() - 1);
        painter->setFont(sizeFont);
        painter->setPen(option.palette.placeholderText().color());
        QString sizeText = ChatLayout::formatFileSize(att.fileSizeBytes);
        if (uploading && att.uploadTotal > 0)
            sizeText = ChatLayout::formatFileSize(att.uploadSent) + " / " +
                       ChatLayout::formatFileSize(att.uploadTotal);
        QFontMetrics sizeFm(sizeFont);
        painter->drawText(textAreaRect.left(),
                          textAreaRect.top() + filenameFm.height() + sizeFm.ascent(), sizeText);

        if (att.isAudio) {
            if (auto target = MediaTargets::forAttachment(att, ChatLayout::audioBarRect(fileRect, false), chatModel))
                paintMediaTarget(painter, option, *target, video);
        }

        if (uploading) {
            QRect barRect(fileRect.left() + 2, fileRect.bottom() - 5, fileRect.width() - 4, 4);
            drawUploadProgress(painter, barRect, att.uploadSent, att.uploadTotal, option.palette);
        }
    }

    QList<EmbedData> embeds = ctx.embeds;

    for (int embedIdx = 0; embedIdx < layout.embedLayouts.size() && embedIdx < embeds.size();
         ++embedIdx) {
        const auto &embedLayout = layout.embedLayouts[embedIdx];
        const auto &embed = embeds[embedIdx];

        if (embed.type == EmbedType::Gifv) {
            if (!embedLayout.imagesRect.isNull() && !embed.thumbnail.isNull()) {
                QPixmap scaledThumb = embed.thumbnail.scaled(
                        embed.thumbnailSize * embed.thumbnail.devicePixelRatio(),
                        Qt::KeepAspectRatio, Qt::SmoothTransformation);
                painter->drawPixmap(embedLayout.imagesRect.topLeft(), scaledThumb);
            }

            QFont gifFont = option.font;
            gifFont.setPointSize(gifFont.pointSize() - 2);
            painter->setFont(gifFont);
            painter->setPen(option.palette.placeholderText().color());
            QFontMetrics gifFm(gifFont);
            int gifLabelTop = embedLayout.imagesRect.isNull() ? embedLayout.embedRect.top()
                                                              : embedLayout.imagesRect.bottom();
            painter->drawText(embedLayout.embedRect.left(), gifLabelTop + gifFm.ascent() + 2,
                              "GIF");
            continue;
        }

        if (embed.type == EmbedType::Image) {
            if (!embedLayout.imagesRect.isNull() && !embed.thumbnail.isNull()) {
                QPixmap scaledThumb = embed.thumbnail.scaled(
                        embed.thumbnailSize * embed.thumbnail.devicePixelRatio(),
                        Qt::KeepAspectRatio, Qt::SmoothTransformation);
                painter->drawPixmap(embedLayout.imagesRect.topLeft(), scaledThumb);
            }
            continue;
        }

        if (ChatLayout::embedIsBareVideo(embed)) {
            if (auto target = MediaTargets::forEmbed(embed, ctx.messageId, embedIdx, embedLayout.imagesRect))
                paintMediaTarget(painter, option, *target, video);
            continue;
        }

        painter->fillRect(embedLayout.embedRect, option.palette.base().color().darker(110));

        QRect borderRect(embedLayout.embedRect.left(), embedLayout.embedRect.top(),
                         ChatLayout::embedBorderWidth(), embedLayout.embedRect.height());
        painter->fillRect(borderRect, embed.color);

        if (embedLayout.hasThumbnail && !embedLayout.thumbnailRect.isNull()) {
            QPixmap thumb = !embed.thumbnail.isNull() ? embed.thumbnail : embed.videoThumbnail;
            if (!thumb.isNull()) {
                QPixmap scaledThumb = thumb.scaled(embedLayout.thumbnailRect.size(),
                                                   Qt::KeepAspectRatio, Qt::SmoothTransformation);
                painter->drawPixmap(embedLayout.thumbnailRect.topLeft(), scaledThumb);
            }
        }

        if (!embed.providerName.isEmpty() && !embedLayout.providerRect.isNull()) {
            QFont providerFont = option.font;
            providerFont.setPointSize(providerFont.pointSize() - 2);
            painter->setFont(providerFont);
            painter->setPen(option.palette.placeholderText().color());
            QFontMetrics providerFm(providerFont);
            painter->drawText(embedLayout.providerRect.left(),
                              embedLayout.providerRect.top() + providerFm.ascent(),
                              embed.providerName);
        }

        if (!embed.authorName.isEmpty() && !embedLayout.authorRect.isNull()) {
            int authorX = embedLayout.authorRect.left();
            int authorY = embedLayout.authorRect.top();
            if (!embed.authorIcon.isNull()) {
                QRect iconRect(authorX, authorY, ChatLayout::authorIconSize(),
                               ChatLayout::authorIconSize());
                painter->drawPixmap(iconRect, embed.authorIcon);
                authorX += ChatLayout::authorIconSize() + 6;
            }
            QFont authorFont = option.font;
            authorFont.setPointSize(authorFont.pointSize() - 1);
            authorFont.setBold(true);
            painter->setFont(authorFont);
            painter->setPen(option.palette.text().color());
            QFontMetrics authorFm(authorFont);
            int textY = authorY + (ChatLayout::authorIconSize() - authorFm.height()) / 2 +
                        authorFm.ascent();
            painter->drawText(authorX, textY, embed.authorName);
        }

        if (!embed.title.isEmpty() && !embedLayout.titleRect.isNull()) {
            QFont titleFont = option.font;
            titleFont.setBold(true);
            painter->setFont(titleFont);
            QColor titleColor = !embed.url.isEmpty() ? option.palette.link().color()
                                                     : option.palette.text().color();
            painter->setPen(titleColor);

            QString titleHtml = !embed.titleParsed.isEmpty() ? embed.titleParsed : embed.title;
            DocCacheKey titleKey = embedTitleDocKey(msgId, embedIdx);
            QTextDocument *titleDoc = chatModel->getCachedDocument(titleKey);
            if (!titleDoc) {
                titleDoc = new QTextDocument;
                titleDoc->setDefaultFont(titleFont);
                titleDoc->setTextWidth(embedLayout.titleRect.width());
                emojiHandler->install(*titleDoc);
                titleDoc->setHtml(titleHtml);
                chatModel->cacheDocument(titleKey, titleDoc);
            } else if (int(titleDoc->textWidth()) != embedLayout.titleRect.width()) {
                titleDoc->setTextWidth(embedLayout.titleRect.width());
            }

            painter->save();
            painter->translate(embedLayout.titleRect.topLeft());
            QAbstractTextDocumentLayout::PaintContext titleCtx;
            titleCtx.palette.setColor(QPalette::Text, titleColor);
            titleDoc->documentLayout()->draw(painter, titleCtx);
            painter->restore();
        }

        if (!embed.description.isEmpty() && !embedLayout.descriptionRect.isNull()) {
            QFont descFont = option.font;
            painter->setFont(descFont);
            painter->setPen(option.palette.text().color());

            QString descHtml = !embed.descriptionParsed.isEmpty() ? embed.descriptionParsed
                                                                  : embed.description;
            DocCacheKey descKey = embedDescDocKey(msgId, embedIdx);
            QTextDocument *descDoc = chatModel->getCachedDocument(descKey);
            if (!descDoc) {
                descDoc = new QTextDocument;
                descDoc->setDefaultFont(descFont);
                descDoc->setTextWidth(embedLayout.descriptionRect.width());
                emojiHandler->install(*descDoc);
                descDoc->setHtml(descHtml);
                chatModel->cacheDocument(descKey, descDoc);
            } else if (int(descDoc->textWidth()) != embedLayout.descriptionRect.width()) {
                descDoc->setTextWidth(embedLayout.descriptionRect.width());
            }

            painter->save();
            painter->translate(embedLayout.descriptionRect.topLeft());
            QAbstractTextDocumentLayout::PaintContext descCtx;
            descCtx.palette.setColor(QPalette::Text, option.palette.text().color());
            descDoc->documentLayout()->draw(painter, descCtx);
            painter->restore();
        }

        QFont fieldNameFont = option.font;
        fieldNameFont.setBold(true);
        QFontMetrics fieldNameFm(fieldNameFont);

        for (const auto &fieldLayout : embedLayout.fieldLayouts) {
            if (fieldLayout.fieldIndex >= embed.fields.size())
                continue;

            const auto &field = embed.fields[fieldLayout.fieldIndex];
            int fi = fieldLayout.fieldIndex;

            QString nameHtml = !field.nameParsed.isEmpty() ? field.nameParsed : field.name;
            DocCacheKey nameKey = embedFieldNameDocKey(msgId, embedIdx, fi);
            QTextDocument *nameDoc = chatModel->getCachedDocument(nameKey);
            if (!nameDoc) {
                nameDoc = new QTextDocument;
                nameDoc->setDefaultFont(fieldNameFont);
                nameDoc->setTextWidth(fieldLayout.nameRect.width());
                emojiHandler->install(*nameDoc);
                nameDoc->setHtml(nameHtml);
                chatModel->cacheDocument(nameKey, nameDoc);
            } else if (int(nameDoc->textWidth()) != fieldLayout.nameRect.width()) {
                nameDoc->setTextWidth(fieldLayout.nameRect.width());
            }

            painter->save();
            painter->translate(fieldLayout.nameRect.topLeft());
            QAbstractTextDocumentLayout::PaintContext nameCtx;
            nameCtx.palette.setColor(QPalette::Text, option.palette.text().color());
            nameDoc->documentLayout()->draw(painter, nameCtx);
            painter->restore();

            QString valueHtml = !field.valueParsed.isEmpty() ? field.valueParsed : field.value;
            DocCacheKey valueKey = embedFieldValueDocKey(msgId, embedIdx, fi);
            QTextDocument *valueDoc = chatModel->getCachedDocument(valueKey);
            if (!valueDoc) {
                valueDoc = new QTextDocument;
                valueDoc->setDefaultFont(option.font);
                valueDoc->setTextWidth(fieldLayout.valueRect.width());
                emojiHandler->install(*valueDoc);
                valueDoc->setHtml(valueHtml);
                chatModel->cacheDocument(valueKey, valueDoc);
            } else if (int(valueDoc->textWidth()) != fieldLayout.valueRect.width()) {
                valueDoc->setTextWidth(fieldLayout.valueRect.width());
            }

            painter->save();
            painter->translate(fieldLayout.valueRect.topLeft());
            QAbstractTextDocumentLayout::PaintContext valueCtx;
            valueCtx.palette.setColor(QPalette::Text, option.palette.text().color().darker(110));
            valueDoc->documentLayout()->draw(painter, valueCtx);
            painter->restore();
        }

        if (!embed.images.isEmpty()) {
            for (const auto &imgLayout : embedLayout.imageLayouts) {
                if (imgLayout.imageIndex >= embed.images.size())
                    continue;

                const auto &img = embed.images[imgLayout.imageIndex];
                bool isSingleImage = (embed.images.size() == 1);

                if (!img.pixmap.isNull()) {
                    if (isSingleImage) {
                        QPixmap scaledImage =
                                img.pixmap.scaled(img.displaySize * img.pixmap.devicePixelRatio(),
                                                  Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        painter->drawPixmap(imgLayout.rect.topLeft(), scaledImage);
                    } else {
                        ChatLayout::drawCroppedPixmap(painter, imgLayout.rect, img.pixmap);
                    }
                } else {
                    painter->fillRect(imgLayout.rect, QColor(60, 60, 60));
                    painter->setPen(option.palette.text().color());
                    painter->drawText(imgLayout.rect, Qt::AlignCenter, "Loading...");
                }
            }
        } else if (auto target = MediaTargets::forEmbed(embed, ctx.messageId, embedIdx, embedLayout.imagesRect)) {
            paintMediaTarget(painter, option, *target, video);
        } else if (!embed.videoThumbnail.isNull() && embed.thumbnail.isNull() &&
                   !embedLayout.imagesRect.isNull()) {
            QPixmap scaledVideo = embed.videoThumbnail.scaled(embedLayout.imagesRect.size(),
                                                              Qt::KeepAspectRatio, Qt::SmoothTransformation);
            painter->drawPixmap(embedLayout.imagesRect.topLeft(), scaledVideo);
            VideoControls::paintPlayBadge(painter, embedLayout.imagesRect);
        }

        if (!embed.footerText.isEmpty() && !embedLayout.footerRect.isNull()) {
            int footerX = embedLayout.footerRect.left();
            int footerY = embedLayout.footerRect.top();
            if (!embed.footerIcon.isNull()) {
                QRect iconRect(footerX, footerY, ChatLayout::footerIconSize(),
                               ChatLayout::footerIconSize());
                painter->drawPixmap(iconRect, embed.footerIcon);
                footerX += ChatLayout::footerIconSize() + 6;
            }
            QFont footerFont = option.font;
            footerFont.setPointSize(footerFont.pointSize() - 2);
            painter->setFont(footerFont);
            painter->setPen(option.palette.placeholderText().color());
            QFontMetrics footerFm(footerFont);

            QString footerText = embed.footerText;
            if (embed.timestamp.isValid())
                footerText += " • " + embed.timestamp.toLocalTime().toString("MMM d, yyyy h:mm AP");

            int textY = footerY + (ChatLayout::footerIconSize() - footerFm.height()) / 2 +
                        footerFm.ascent();
            painter->drawText(footerX, textY, footerText);
        }
    }

    for (const auto &stickerLayout : layout.stickerLayouts) {
        const StickerData &sticker = ctx.stickers[stickerLayout.stickerIndex];
        const ResolvedSticker resolved = resolveSticker(sticker, stickerLayout.rect, animator, chatModel->getAccountId());
        if (resolved.unplayable)
            drawUnplayableSticker(painter, option, stickerLayout.rect, sticker.name);
        else if (!resolved.pixmap.isNull())
            drawSticker(painter, stickerLayout.rect, resolved.pixmap);
    }

    if (!layout.forwardOriginRect.isNull() && !ctx.forwardOrigin.text.isEmpty()) {
        QRect originRect = layout.forwardOriginRect;
        int textX = originRect.left();

        if (ctx.forwardOrigin.iconUrl.isValid()) {
            const int iconSize = ChatLayout::forwardOriginIconSize();
            if (!ctx.forwardOrigin.icon.isNull()) {
                QRect iconRect(textX,
                               originRect.top() + (originRect.height() - iconSize) / 2,
                               iconSize,
                               iconSize);
                painter->save();
                painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
                QPainterPath clip;
                clip.addEllipse(iconRect);
                painter->setClipPath(clip);
                ChatLayout::drawCroppedPixmap(painter, iconRect, ctx.forwardOrigin.icon);
                painter->restore();
            }
            textX += iconSize + ChatLayout::forwardOriginIconGap();
        }

        QFont originFont = ChatLayout::forwardOriginFont(option.font);
        painter->setFont(originFont);
        painter->setPen(Core::Theme::Manager::instance().color(Core::Theme::Token::PlaceholderText));
        QFontMetrics originFm(originFont);
        QRect textRect(textX,
                       originRect.top(),
                       originRect.right() - textX + 1,
                       originRect.height());
        painter->drawText(textRect,
                          Qt::AlignLeft | Qt::AlignVCenter,
                          originFm.elidedText(ctx.forwardOrigin.text, Qt::ElideRight, textRect.width()));
    }

    QList<ReactionData> reactions = ctx.reactions;

    for (const auto &reactionLayout : layout.reactionLayouts) {
        if (reactionLayout.reactionIndex >= reactions.size())
            continue;

        const auto &reaction = reactions[reactionLayout.reactionIndex];

        QColor pillBg;
        if (reaction.isBurst && reaction.burstTintColor.isValid()) {
            pillBg = reaction.burstTintColor;
            pillBg.setAlpha(40);
        } else {
            pillBg = option.palette.alternateBase().color();
        }

        QColor borderColor;
        int borderWidth;
        if (reaction.me) {
            borderColor = option.palette.highlight().color();
            borderWidth = 1;
        } else {
            borderColor = option.palette.mid().color();
            borderWidth = 1;
        }

        painter->setPen(QPen(borderColor, borderWidth));
        painter->setBrush(pillBg);
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->drawRoundedRect(reactionLayout.pillRect, 6, 6);
        painter->setRenderHint(QPainter::Antialiasing, false);

        if (reaction.emojiId.isValid()) {
            QPixmap emojiPixmap;
            if (animator && reaction.emojiAnimated)
                emojiPixmap = animator->frame(reaction.emojiUrl, reactionLayout.emojiRect.size(),
                                              chatModel->getAccountId(), reactionLayout.emojiRect);
            if (emojiPixmap.isNull())
                emojiPixmap = reaction.emojiPixmap;
            if (!emojiPixmap.isNull())
                painter->drawPixmap(reactionLayout.emojiRect, emojiPixmap);
        } else {
            // render smaller than the rect to fit within pill
            QFont emojiFont = option.font;
            emojiFont.setPixelSize(ChatLayout::reactionEmojiSize() - 4);
            painter->setFont(emojiFont);
            painter->setPen(option.palette.text().color());
            painter->drawText(reactionLayout.emojiRect, Qt::AlignCenter, reaction.emojiName);
        }

        QFont countFont = option.font;
        countFont.setPointSizeF(countFont.pointSizeF() * 0.85);
        painter->setFont(countFont);

        QColor countColor;
        if (reaction.me)
            countColor = option.palette.highlight().color();
        else
            countColor = option.palette.text().color();

        painter->setPen(countColor);
        painter->drawText(reactionLayout.countRect, Qt::AlignLeft | Qt::AlignVCenter,
                          QString::number(reaction.count));

        // Qord: who reacted (WhoReacted): round avatars, each ringed in the
        // pill's colour so the overlaps stay apart, then "+N"
        if (!reactionLayout.avatarRects.isEmpty()) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
            const QColor ring = option.palette.alternateBase().color();
            for (int a = 0; a < reactionLayout.avatarRects.size() && a < reaction.userAvatars.size(); ++a) {
                const QRectF r = reactionLayout.avatarRects[a];
                painter->setPen(Qt::NoPen);
                painter->setBrush(ring);
                painter->drawEllipse(r.adjusted(-1.5, -1.5, 1.5, 1.5));
                QPainterPath clip;
                clip.addEllipse(r);
                painter->setClipPath(clip);
                const QPixmap &avatar = reaction.userAvatars[a];
                if (avatar.isNull())
                    painter->fillRect(r, option.palette.mid());
                else
                    painter->drawPixmap(r, avatar, QRectF(avatar.rect()));
                painter->setClipping(false);
            }
            if (reactionLayout.moreRect.isValid()) {
                painter->setFont(countFont);
                painter->setPen(option.palette.text().color());
                painter->drawText(reactionLayout.moreRect, Qt::AlignLeft | Qt::AlignVCenter,
                                  QStringLiteral("+%1").arg(reaction.moreUsers));
            }
            painter->restore();
        }
    }

    if (animator)
        animator->endRow();

    painter->restore();
}

QSize ChatDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    int viewportWidth = 400;
    const ChatView *chatView = nullptr;
    if (option.widget) {
        if (auto view = qobject_cast<const QAbstractItemView *>(option.widget)) {
            viewportWidth = view->viewport()->width();
            chatView = qobject_cast<const ChatView *>(view);
        } else {
            viewportWidth = option.widget->width();
        }
    }

    bool isEditing = chatView && chatView->editingRow() == index.row();

    QSize cached = index.data(ChatModel::CachedSizeRole).toSize();
    if (cached.isValid() && cached.width() == viewportWidth && !isEditing)
        return cached;

    const auto *chatModel = qobject_cast<const ChatModel *>(index.model());
    if (chatModel)
        chatModel->suppressImageFetch = true;

    ChatLayout::LayoutContext ctx = ChatLayout::buildContext(index, option.font, option.rect, option.palette);

    if (chatModel)
        chatModel->suppressImageFetch = false;

    ctx.rowWidth = viewportWidth;
    ctx.rowTop = 0;

    ChatLayout::MessageLayout layout = ChatLayout::calculateMessageLayout(ctx);

    int height = layout.totalHeight;
    if (isEditing)
        height = qMax(height, height + ChatView::InlineEditMinHeight);

    QSize size(viewportWidth, height);

    if (!isEditing) {
        auto model = const_cast<QAbstractItemModel *>(index.model());
        const auto prevSize = index.data(ChatModel::CachedSizeRole).toSize();
        if (size != prevSize)
            model->setData(index, size, ChatModel::CachedSizeRole);
    }

    return size;
}

void ChatDelegate::paintTextPreview(QPainter *painter, const QStyleOptionViewItem &option, const QRect &box,
                                    const AttachmentData &att) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    QPainterPath shape;
    shape.addRoundedRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    painter->fillPath(shape, option.palette.alternateBase());
    painter->setPen(QPen(option.palette.mid().color(), 1));
    painter->drawPath(shape);

    const ChatLayout::TextPreviewButtons buttons = ChatLayout::textPreviewButtons(box, att);
    const QFont mono = ChatLayout::textPreviewFont();
    const QFontMetrics fm(mono);
    painter->setFont(mono);
    painter->setPen(option.palette.text().color());
    const int textLeft = box.left() + 10;
    const int textWidth = box.width() - 20;
    int y = buttons.body.top() + 8;
    for (const QString &line : ChatLayout::textPreviewLines(att)) {
        painter->drawText(textLeft, y + fm.ascent(), fm.elidedText(line, Qt::ElideRight, textWidth));
        y += fm.lineSpacing();
    }
    if (att.textExpanded && att.textBytesLeft > 0) {
        painter->setPen(option.palette.placeholderText().color());
        painter->drawText(textLeft, y + fm.ascent(),
                          tr("%1 bytes left").arg(QLocale().toString(att.textBytesLeft)));
    }

    // Footer: name and size, then the buttons
    painter->setPen(QPen(option.palette.mid().color(), 1));
    painter->drawLine(buttons.footer.topLeft(), buttons.footer.topRight());
    int buttonsLeft = buttons.download.left();
    for (const QRect &rect : { buttons.view, buttons.expand })
        if (!rect.isNull())
            buttonsLeft = qMin(buttonsLeft, rect.left());
    for (const QRect &rect : buttons.plugins)
        buttonsLeft = qMin(buttonsLeft, rect.left());
    QFont nameFont = option.font;
    nameFont.setWeight(QFont::DemiBold);
    painter->setFont(nameFont);
    const QFontMetrics nameFm(nameFont);
    QFont sizeFont = option.font;
    sizeFont.setPointSize(sizeFont.pointSize() - 1);
    const QFontMetrics sizeFm(sizeFont);
    const QString size = ChatLayout::formatFileSize(att.fileSizeBytes);
    const int nameWidth = qMax(0, buttonsLeft - textLeft - 12 - sizeFm.horizontalAdvance(size));
    const QString name = nameFm.elidedText(att.filename, Qt::ElideMiddle, nameWidth);
    const int baseline = buttons.footer.top() + (buttons.footer.height() + nameFm.ascent() - nameFm.descent()) / 2;
    painter->setPen(option.palette.text().color());
    painter->drawText(textLeft, baseline, name);
    painter->setFont(sizeFont);
    painter->setPen(option.palette.placeholderText().color());
    painter->drawText(textLeft + nameFm.horizontalAdvance(name) + 8, baseline, size);

    const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    const QColor iconColor = option.palette.text().color();
    auto drawIcon = [&](const QRect &rect, const QPixmap &icon) {
        QRect glyph(0, 0, 18, 18);
        glyph.moveCenter(rect.center());
        painter->drawPixmap(glyph, icon);
    };
    drawIcon(buttons.download, Core::Theme::Icons::pixmap(Core::Theme::Icons::Name::ArrowDown, 18, iconColor, dpr));
    drawIcon(buttons.view, Core::Theme::Icons::pixmap(Core::Theme::Icons::Name::Maximize, 18, iconColor, dpr));
    if (!buttons.expand.isNull()) {
        QPixmap chevron = Core::Theme::Icons::pixmap(Core::Theme::Icons::Name::ChevronDown, 18, iconColor, dpr);
        if (att.textExpanded)
            chevron = chevron.transformed(QTransform().scale(1, -1));
        drawIcon(buttons.expand, chevron);
    }
    for (int i = 0; i < buttons.plugins.size() && i < att.textButtons.size(); ++i) {
        QByteArray svg = att.textButtons[i].svg;
        svg.replace("currentColor", iconColor.name().toLatin1());
        QSvgRenderer renderer(svg);
        QPixmap icon(QSize(18, 18) * dpr);
        icon.setDevicePixelRatio(dpr);
        icon.fill(Qt::transparent);
        if (renderer.isValid()) {
            QPainter iconPainter(&icon);
            renderer.render(&iconPainter, QRectF(0, 0, 18, 18));
        }
        drawIcon(buttons.plugins[i], icon);
    }
    painter->restore();
}

QString ChatDelegate::headerTimestamp(const QDateTime &timestamp)
{
    const QLocale locale;
    const QString time = Qord::Preferences::timeText(timestamp.time());
    const qint64 days = timestamp.date().daysTo(QDate::currentDate());
    if (days == 0)
        return tr("Today at %1").arg(time);
    if (days == 1)
        return tr("Yesterday at %1").arg(time);
    return locale.toString(timestamp.date(), QLocale::ShortFormat) + QLatin1Char(' ') + time;
}

} // namespace UI
} // namespace Acheron
