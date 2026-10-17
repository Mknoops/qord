/*
 * Qord, a native Discord client
 * Port of Equicord's FakeNitro plugin (src/plugins/fakeNitro)
 * Original: Copyright (c) Vendicated and contributors, authors Arjix, D3SOX, Ven, fawn, captain, Nuckyz,
 * AutumnVN, sadan
 * Qord additions (long messages, image/GIF/video shrinking, fake reactions, uploads,
 * several stickers and more): Maarten
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "FakeNitro.hpp"

#include "ApngToGif.hpp"

#include "Plugins/JsCompat.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QBuffer>
#include <cmath>
#include <QImage>
#include <QImageReader>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

namespace Qord {
namespace Plugins {

namespace {

const QString NameParam = QStringLiteral("name");

// Upstream's regexes
const QRegularExpression &emojiRegex()
{
    static const QRegularExpression re(QStringLiteral(R"(/emojis/(\d+?)\.(png|webp|gif))"));
    return re;
}
const QRegularExpression &stickerRegex()
{
    static const QRegularExpression re(QStringLiteral(R"(/stickers/(\d+?)\.)"));
    return re;
}
const QRegularExpression &gifStickerRegex()
{
    static const QRegularExpression re(QStringLiteral(R"(/attachments/\d+?/\d+?/(\d+?)\.gif)"));
    return re;
}
const QRegularExpression &hyperLinkRegex()
{
    static const QRegularExpression re(QStringLiteral(R"(\[.+?\]\((https?://.+?)\))"));
    return re;
}
// JS's ^ and $ without the m flag: the whole string, no newline before the end
const QRegularExpression &hyperLinkOnlyRegex()
{
    static const QRegularExpression re(QStringLiteral(R"(\A\[.+?\]\((https?://.+?)\)\z)"));
    return re;
}

Id toId(const QString &digits)
{
    return digits.toULongLong();
}

// content.split(/\s/)
QStringList splitOnWhitespace(const QString &s)
{
    QStringList items;
    QString current;
    for (QChar c : s) {
        if (Js::isWhitespace(c)) {
            items.append(current);
            current.clear();
        } else {
            current += c;
        }
    }
    items.append(current);
    return items;
}

// new URL(s) only accepts absolute URLs
bool parsesAsUrl(const QString &s)
{
    static const QRegularExpression scheme(QStringLiteral(R"(\A[A-Za-z][A-Za-z0-9+.\-]*:)"));
    return scheme.match(s).hasMatch() && QUrl(s.trimmed()).isValid();
}

// Discord's custom emoji markup allows [a-zA-Z0-9_]{1,32}
QString markupName(const QString &name)
{
    QString out;
    for (QChar c : name) {
        const ushort u = c.unicode();
        const bool ok = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == '_';
        out += ok ? c : QLatin1Char('_');
        if (out.size() == 32)
            break;
    }
    return out.isEmpty() ? QStringLiteral("_") : out;
}

// Qord's markdown parser (src/Core/Markdown/Parser.cpp): its url, link and
// autolink rules and the inline code it skips
const QRegularExpression &urlRule()
{
    static const QRegularExpression re(QStringLiteral(R"(\G(https?://[^\s<]+[^<.,:;"'\]\s]))"));
    return re;
}
const QRegularExpression &linkRule()
{
    static const QRegularExpression re(QStringLiteral(
            R"(\G\[((?:\[[^\]]*\]|[^\[\]]|\](?=[^\[]*\]))*)\]\(\s*<?((?:\([^)]*\)|[^\s\\]|\\.)*?)>?(?:\s+['"]([\s\S]*?)['"])?\s*\))"));
    return re;
}
const QRegularExpression &autolinkRule()
{
    static const QRegularExpression re(QStringLiteral(R"(\G<([^: >]+:/[^ >]+)>)"));
    return re;
}
const QRegularExpression &inlineCodeRule()
{
    static const QRegularExpression re(QStringLiteral(R"(\G(`+)([\s\S]*?[^`])\1(?!`))"));
    return re;
}

QString trimUnbalancedTrailingParens(QString url)
{
    while (url.endsWith(QLatin1Char(')')) && url.count(QLatin1Char(')')) > url.count(QLatin1Char('(')))
        url.chop(1);
    return url;
}

} // namespace

namespace FakeNitro {

QString wordBoundary(const QString &s, qsizetype offset)
{
    return (offset < 0 || offset >= s.size() || Js::isWhitespace(s.at(offset))) ? QString() : QStringLiteral(" ");
}

QString formEncode(const QString &s)
{
    QString out;
    for (const char ch : s.toUtf8()) {
        const auto b = static_cast<unsigned char>(ch);
        if ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') || b == '*' || b == '-' ||
            b == '.' || b == '_')
            out += QLatin1Char(char(b));
        else if (b == ' ')
            out += QLatin1Char('+');
        else
            out += QStringLiteral("%%1").arg(b, 2, 16, QLatin1Char('0')).toUpper();
    }
    return out;
}

QString formDecode(const QString &s)
{
    QByteArray bytes = s.toUtf8();
    bytes.replace('+', ' ');
    return QString::fromUtf8(QByteArray::fromPercentEncoding(bytes));
}

std::optional<QString> queryParam(const QString &url, const QString &name)
{
    if (!parsesAsUrl(url))
        return std::nullopt;
    qsizetype q = url.indexOf(QLatin1Char('?'));
    if (q < 0)
        return std::nullopt;
    qsizetype end = url.indexOf(QLatin1Char('#'), q);
    const QString query = url.mid(q + 1, end < 0 ? -1 : end - q - 1);
    for (const QString &pair : query.split(QLatin1Char('&'))) {
        if (pair.isEmpty())
            continue;
        const qsizetype eq = pair.indexOf(QLatin1Char('='));
        const QString key = formDecode(eq < 0 ? pair : pair.left(eq));
        if (key == name)
            return eq < 0 ? QString() : formDecode(pair.mid(eq + 1));
    }
    return std::nullopt;
}

QString emojiLink(Id id, const QString &name, bool animated, int size, bool forSend)
{
    // IconUtils.getEmojiURL({ id, animated, size })
    const QString ext = animated && forSend ? QStringLiteral("gif") : QStringLiteral("webp");
    QString url = QStringLiteral("https://cdn.discordapp.com/emojis/%1.%2?size=%3").arg(id).arg(ext).arg(size);
    if (animated)
        url += QStringLiteral("&animated=true");
    return url + QStringLiteral("&name=") + formEncode(name) + QStringLiteral("&lossless=true");
}

QString stickerLink(Id id, const QString &name, int formatType, int size)
{
    const QString ext = formatType == StickerGif ? QStringLiteral("gif") : QStringLiteral("png");
    return QStringLiteral("https://media.discordapp.net/stickers/%1.%2?size=%3&name=%4&lossless=true")
            .arg(id)
            .arg(ext)
            .arg(size)
            .arg(formEncode(name));
}

QString uploadUrl(Id id, bool sticker, bool animated, int size)
{
    const QString ext = animated ? QStringLiteral("gif") : QStringLiteral("png");
    return sticker ? QStringLiteral("https://media.discordapp.net/stickers/%1.%2?size=%3").arg(id).arg(ext).arg(size)
                   : QStringLiteral("https://cdn.discordapp.com/emojis/%1.%2?size=%3").arg(id).arg(ext).arg(size);
}

QString withoutTokens(QString text, const QStringList &tokens)
{
    for (const QString &token : tokens)
        text.remove(token);
    // Spaces left where an emoji was, on each line
    static const QRegularExpression spaces(QStringLiteral("[ \\t]{2,}"));
    text.replace(spaces, QStringLiteral(" "));
    QStringList lines = text.split(QLatin1Char('\n'));
    for (QString &line : lines)
        line = line.trimmed();
    return lines.join(QLatin1Char('\n')).trimmed();
}

QString linkMarkup(const QString &url, const QString &name, bool hyperlink, const QString &hyperLinkText)
{
    if (!hyperlink)
        return url;
    return QStringLiteral("[%1](%2)").arg(Js::replaceAllLiteral(hyperLinkText, QStringLiteral("{{NAME}}"), name), url);
}

std::optional<FakeEmoji> fakeEmojiFromLink(const QString &target,
                                           const std::function<std::optional<QString>(Id)> &knownName)
{
    const QRegularExpressionMatch m = emojiRegex().match(target);
    if (!m.hasMatch())
        return std::nullopt;
    FakeEmoji emoji;
    emoji.id = toId(m.captured(1));
    if (auto name = knownName ? knownName(emoji.id) : std::nullopt)
        emoji.name = *name;
    else if (auto param = queryParam(target, NameParam))
        emoji.name = *param;
    else
        emoji.name = QStringLiteral("FakeNitroEmoji");
    emoji.animated = m.captured(2) == QLatin1String("gif") ||
                     queryParam(target, QStringLiteral("animated")) == QStringLiteral("true");
    return emoji;
}

bool isFakeStickerLink(const QString &target, const std::function<bool(Id)> &knownSticker)
{
    if (stickerRegex().match(target).hasMatch())
        return true;
    const QRegularExpressionMatch gif = gifStickerRegex().match(target);
    return gif.hasMatch() && knownSticker && knownSticker(toId(gif.captured(1)));
}

QList<FakeSticker> fakeStickers(const QString &content, const QStringList &gifAttachmentUrls, bool compound,
                                const std::function<std::optional<QString>(Id)> &knownStickerName)
{
    QStringList items;
    const QStringList contentItems = splitOnWhitespace(content);
    if (compound)
        items = contentItems;
    else if (contentItems.size() == 1)
        items.append(contentItems.first());
    else if (hyperLinkOnlyRegex().match(content).hasMatch())
        items.append(content);
    items += gifAttachmentUrls;

    QList<FakeSticker> stickers;
    for (const QString &item : std::as_const(items)) {
        if (!compound && !item.startsWith(QLatin1String("http")) && !hyperLinkRegex().match(item).hasMatch())
            continue;

        const QRegularExpressionMatch img = stickerRegex().match(item);
        if (img.hasMatch()) {
            FakeSticker sticker;
            sticker.id = toId(img.captured(1));
            sticker.formatType = StickerPng;
            if (auto name = knownStickerName ? knownStickerName(sticker.id) : std::nullopt)
                sticker.name = *name;
            else if (auto param = queryParam(item, NameParam))
                sticker.name = *param;
            else
                sticker.name = QStringLiteral("FakeNitroSticker");
            stickers.append(sticker);
            continue;
        }

        const QRegularExpressionMatch gif = gifStickerRegex().match(item);
        if (gif.hasMatch()) {
            const Id id = toId(gif.captured(1));
            const auto name = knownStickerName ? knownStickerName(id) : std::nullopt;
            if (!name)
                continue;
            stickers.append({ id, *name, StickerApng });
        }
    }
    return stickers;
}

bool shouldIgnoreEmbed(const QString &embedType, const QString &embedUrl, const QString &content, bool compound,
                       bool transformEmojis, bool transformStickers, const std::function<bool(Id)> &knownSticker)
{
    const QStringList contentItems = splitOnWhitespace(content);
    const bool hyperLinkOnly = hyperLinkOnlyRegex().match(content).hasMatch();
    if (contentItems.size() > 1 && !compound && !hyperLinkOnly)
        return false;
    if (embedType != QLatin1String("image") || embedUrl.isEmpty())
        return false;
    if (!compound && !hyperLinkOnly) {
        const bool inContent = std::any_of(contentItems.begin(), contentItems.end(), [&](const QString &item) {
            if (item == embedUrl)
                return true;
            const QRegularExpressionMatch m = hyperLinkRegex().match(item);
            return m.hasMatch() && m.captured(1) == embedUrl;
        });
        if (!inContent)
            return false;
    }
    if (transformEmojis && emojiRegex().match(embedUrl).hasMatch())
        return true;
    if (transformStickers) {
        if (stickerRegex().match(embedUrl).hasMatch())
            return true;
        const QRegularExpressionMatch gif = gifStickerRegex().match(embedUrl);
        // There is no way to tell a GIF upload from a fake animated sticker
        // except that the id is a sticker the client knows
        if (gif.hasMatch() && knownSticker && knownSticker(toId(gif.captured(1))))
            return true;
    }
    return false;
}

bool isFakeStickerAttachment(const QString &url, const QString &contentType,
                             const std::function<bool(Id)> &knownSticker)
{
    if (contentType != QLatin1String("image/gif"))
        return false;
    const QRegularExpressionMatch m = gifStickerRegex().match(url);
    return m.hasMatch() && knownSticker && knownSticker(toId(m.captured(1)));
}

QList<ContentLink> topLevelLinks(const QString &content)
{
    QList<ContentLink> links;
    qsizetype i = 0;
    while (i < content.size()) {
        const QChar c = content.at(i);
        if (c == QLatin1Char('\\') && i + 1 < content.size() && !content.at(i + 1).isLetterOrNumber() &&
            !content.at(i + 1).isSpace()) {
            i += 2;
            continue;
        }
        if (c == QLatin1Char('`')) {
            const QRegularExpressionMatch code = inlineCodeRule().match(content, i);
            if (code.hasMatch() && code.capturedStart() == i) {
                i += code.capturedLength();
                continue;
            }
            // An unclosed run of backticks is text
            while (i < content.size() && content.at(i) == QLatin1Char('`'))
                ++i;
            continue;
        }
        if (c == QLatin1Char('<')) {
            const QRegularExpressionMatch m = autolinkRule().match(content, i);
            if (m.hasMatch() && m.capturedStart() == i) {
                links.append({ i, m.capturedLength(), m.captured(1) });
                i += m.capturedLength();
                continue;
            }
        } else if (c == QLatin1Char('[')) {
            const QRegularExpressionMatch m = linkRule().match(content, i);
            if (m.hasMatch() && m.capturedStart() == i) {
                links.append({ i, m.capturedLength(), m.captured(2) });
                i += m.capturedLength();
                continue;
            }
        } else if (c == QLatin1Char('h')) {
            const QRegularExpressionMatch m = urlRule().match(content, i);
            if (m.hasMatch() && m.capturedStart() == i) {
                const QString url = trimUnbalancedTrailingParens(m.captured(1));
                links.append({ i, url.size(), url });
                i += url.size();
                continue;
            }
        }
        ++i;
    }
    return links;
}

bool isCompound(const QString &content, const QList<ContentLink> &links)
{
    if (links.size() != 1)
        return true;
    return links.first().start != 0 || links.first().length != content.size();
}

const QString HiddenLinkText = QStringLiteral("\u200B");

bool isEmojiOnly(const QString &content)
{
    static const QRegularExpression customEmoji(QStringLiteral(R"(<a?:\w+:\d+>)"));
    QString rest = content;
    rest.remove(customEmoji);
    return rest.trimmed().isEmpty() && rest.size() < content.size();
}

int messageLengthLimit(int premiumType)
{
    return premiumType == 2 ? 4000 : 2000;
}

namespace {

// The language after the last opening fence when `text` ends inside a code
// block; nullopt when it does not
std::optional<QString> openFenceLanguage(const QString &text)
{
    static const QRegularExpression fence(QStringLiteral("```"));
    qsizetype count = 0;
    qsizetype lastOpen = -1;
    auto it = fence.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (count % 2 == 0)
            lastOpen = m.capturedEnd();
        ++count;
    }
    if (count % 2 == 0)
        return std::nullopt;
    static const QRegularExpression language(QStringLiteral(R"(\G([A-Za-z0-9_+#.\-]{1,16})\n)"));
    const QRegularExpressionMatch m = language.match(text, lastOpen);
    return m.hasMatch() ? m.captured(1) : QString();
}

} // namespace

QStringList splitMessage(const QString &content, int limit)
{
    // Room for closing and reopening a code block
    constexpr int FenceReserve = 4 + 3 + 16 + 1;
    if (limit <= FenceReserve * 2)
        return { content };
    QStringList parts;
    QString rest = content;
    while (rest.size() > limit) {
        const bool hasFences = rest.contains(QLatin1String("```"));
        const qsizetype window = hasFences ? limit - FenceReserve : limit;
        qsizetype cut = -1;
        qsizetype skip = 0;
        const qsizetype newline = rest.lastIndexOf(QLatin1Char('\n'), window);
        if (newline > window / 2) {
            cut = newline;
            skip = 1;
        } else {
            for (qsizetype i = window; i > window / 2; --i) {
                if (Js::isWhitespace(rest.at(i))) {
                    cut = i;
                    skip = 1;
                    break;
                }
            }
        }
        if (cut < 0) {
            cut = window;
            // Don't cut a surrogate pair in half
            if (rest.at(cut).isLowSurrogate())
                --cut;
        }
        QString part = rest.left(cut);
        rest = rest.mid(cut + skip);
        if (const std::optional<QString> language = openFenceLanguage(part)) {
            part += part.endsWith(QLatin1Char('\n')) ? QStringLiteral("```") : QStringLiteral("\n```");
            rest = QStringLiteral("```") + *language + QLatin1Char('\n') + rest;
        }
        parts.append(part);
    }
    if (!rest.isEmpty() || parts.isEmpty())
        parts.append(rest);
    return parts;
}

namespace {

bool hasTransparentPixels(const QImage &image)
{
    if (!image.hasAlphaChannel())
        return false;
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < argb.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x)
            if (qAlpha(line[x]) < 255)
                return true;
    }
    return false;
}

QByteArray encodeImage(const QImage &image, const char *format, int quality)
{
    QByteArray out;
    QBuffer buffer(&out);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, format, quality))
        return {};
    return out;
}

// An animated image (GIF, animated WebP or PNG) as a smaller GIF: every frame
// scaled down, then every other frame dropped (its time going to the one
// kept), until it fits
std::optional<AttachedFile> shrinkAnimation(QImageReader &reader, const AttachedFile &file, qint64 limit)
{
    constexpr int MaxFrames = 1000;
    QList<QImage> frames;
    QList<double> delays;
    while (frames.size() < MaxFrames) {
        const QImage frame = reader.read();
        if (frame.isNull())
            break;
        frames.append(frame.convertToFormat(QImage::Format_ARGB32));
        delays.append(qMax(20, reader.nextImageDelay()));
        if (!reader.canRead())
            break;
    }
    if (frames.size() < 2)
        return std::nullopt;
    const QSize original = frames.first().size();
    const qsizetype dot = file.fileName.lastIndexOf(QLatin1Char('.'));
    const QString name = (dot > 0 ? file.fileName.left(dot) : file.fileName) + QStringLiteral(".gif");

    // A first guess from the size it has to lose, then smaller
    double scale = qMin(1.0, std::sqrt(double(limit) / double(file.data.size())));
    for (int attempt = 0; attempt < 10; ++attempt) {
        const QSize size(qMax(1, int(original.width() * scale)), qMax(1, int(original.height() * scale)));
        if (size.width() < 32 && size.height() < 32)
            break;
        const QList<QImage> scaled = ApngToGif::scaledFrames(frames, size);
        const QByteArray gif = ApngToGif::encodeGif(scaled, delays);
        if (!gif.isEmpty() && gif.size() <= limit)
            return AttachedFile { name, QStringLiteral("image/gif"), gif };
        // Past the third try, drop every other frame too
        if (attempt >= 2 && frames.size() > 8) {
            QList<QImage> fewer;
            QList<double> longer;
            for (qsizetype i = 0; i < frames.size(); i += 2) {
                fewer.append(frames[i]);
                longer.append(delays[i] + delays.value(i + 1, 0));
            }
            frames = fewer;
            delays = longer;
        }
        const double ratio = gif.isEmpty() ? 0.8 : std::sqrt(double(limit) / double(gif.size()));
        scale *= qBound(0.5, ratio * 0.95, 0.9);
    }
    return std::nullopt;
}

} // namespace

std::optional<AttachedFile> shrinkImage(const AttachedFile &file, qint64 limit)
{
    if (limit <= 0 || file.data.isEmpty())
        return std::nullopt;
    QBuffer input;
    input.setData(file.data);
    input.open(QIODevice::ReadOnly);
    QImageReader reader(&input);
    reader.setAutoTransform(true);
    if (reader.supportsAnimation() && reader.imageCount() > 1)
        return shrinkAnimation(reader, file, limit);
    QImage image = reader.read();
    if (image.isNull())
        return std::nullopt;

    const bool transparent = hasTransparentPixels(image);
    const char *format = transparent ? "PNG" : "JPEG";
    if (!transparent && image.hasAlphaChannel())
        image = image.convertToFormat(QImage::Format_RGB32);
    const QString baseName = [&] {
        const qsizetype dot = file.fileName.lastIndexOf(QLatin1Char('.'));
        const QString base = dot > 0 ? file.fileName.left(dot) : file.fileName;
        return base.isEmpty() ? QStringLiteral("image") : base;
    }();
    const auto result = [&](const QByteArray &data) {
        return AttachedFile { baseName + (transparent ? QStringLiteral(".png") : QStringLiteral(".jpg")),
                              transparent ? QStringLiteral("image/png") : QStringLiteral("image/jpeg"), data };
    };

    // Full size first, at falling JPEG quality
    if (!transparent) {
        for (int quality : { 92, 85, 75 }) {
            const QByteArray data = encodeImage(image, format, quality);
            if (!data.isEmpty() && data.size() <= limit)
                return result(data);
        }
    } else {
        const QByteArray data = encodeImage(image, format, -1);
        if (!data.isEmpty() && data.size() <= limit)
            return result(data);
    }
    // Then smaller, each step 80% of the size before
    QSize size = image.size();
    for (int step = 0; step < 24; ++step) {
        size = QSize(qMax(1, int(size.width() * 0.8)), qMax(1, int(size.height() * 0.8)));
        if (size.width() < 64 && size.height() < 64)
            break;
        const QImage scaled = image.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QByteArray data = encodeImage(scaled, format, transparent ? -1 : 85);
        if (!data.isEmpty() && data.size() <= limit)
            return result(data);
    }
    return std::nullopt;
}

} // namespace FakeNitro

using namespace FakeNitro;

FakeNitroPlugin::FakeNitroPlugin() : alive(std::make_shared<bool>(true)) {}

FakeNitroPlugin::~FakeNitroPlugin()
{
    *alive = false;
}

void FakeNitroPlugin::start(PluginContext &ctx)
{
    context = &ctx;
    applyPickerBypass();
    applyQordBypasses();
}

void FakeNitroPlugin::stop()
{
    if (context)
        if (auto *expressions = context->expressions()) {
            expressions->setChatBypass(false, false);
            expressions->setStreamQualityBypass(false);
            expressions->setOversizeImageHandler(nullptr);
            expressions->setVideoShrinking(false);
            expressions->setFakeReactions(false);
            expressions->setLongMessageSplitter(nullptr);
        }
    context = nullptr;
}

void FakeNitroPlugin::onSettingChanged(const QString &key)
{
    if (key == QLatin1String("enableEmojiBypass") || key == QLatin1String("enableStickerBypass"))
        applyPickerBypass();
    if (key == QLatin1String("enableStreamQualityBypass") || key == QLatin1String("shrinkLargeImages") ||
        key == QLatin1String("shrinkLargeVideos") || key == QLatin1String("splitLongMessages") ||
        key == QLatin1String("fakeReactions") || key == QLatin1String("enableEmojiBypass") ||
        key == QLatin1String("videoPriority") || key == QLatin1String("videoMaxLength") ||
        key == QLatin1String("trimLongVideos"))
        applyQordBypasses();
}

// Go Live's quality choices, and images over the upload limit
void FakeNitroPlugin::applyQordBypasses()
{
    if (!context)
        return;
    auto *expressions = context->expressions();
    if (!expressions)
        return;
    expressions->setStreamQualityBypass(context->settings().boolValue(QStringLiteral("enableStreamQualityBypass")));
    if (context->settings().boolValue(QStringLiteral("shrinkLargeImages")))
        expressions->setOversizeImageHandler(&FakeNitro::shrinkImage);
    else
        expressions->setOversizeImageHandler(nullptr);
    expressions->setVideoShrinking(context->settings().boolValue(QStringLiteral("shrinkLargeVideos")));
    const QString priority = context->settings().stringValue(QStringLiteral("videoPriority"));
    expressions->setVideoShrinkOptions(priority == QLatin1String("sharp") ? 1 : priority == QLatin1String("smooth") ? 2 : 0,
                                       context->settings().stringValue(QStringLiteral("videoMaxLength")).toInt(),
                                       context->settings().boolValue(QStringLiteral("trimLongVideos")));
    // A fake reaction is a reply with the emoji, which needs the emoji bypass to go as a link
    expressions->setFakeReactions(context->settings().boolValue(QStringLiteral("fakeReactions"))
                                  && context->settings().boolValue(QStringLiteral("enableEmojiBypass")));
    if (context->settings().boolValue(QStringLiteral("splitLongMessages")))
        expressions->setLongMessageSplitter(
                [](const QString &text, int limit) { return int(FakeNitro::splitMessage(text, limit).size()); });
    else
        expressions->setLongMessageSplitter(nullptr);
}

// The link text: the configured one, or an invisible character so other
// people see only the emoji's image
QString FakeNitroPlugin::linkText() const
{
    if (context && context->settings().boolValue(QStringLiteral("hideLinkText")))
        return FakeNitro::HiddenLinkText;
    return context ? context->settings().stringValue(QStringLiteral("hyperLinkText")) : QStringLiteral("{{NAME}}");
}

// Emoji size, or the sentence size for an emoji among other text
int FakeNitroPlugin::emojiSizeFor(const QString &content) const
{
    PluginSettings &s = context->settings();
    const QString sentence = s.stringValue(QStringLiteral("sentenceEmojiSize"));
    if (sentence != QLatin1String("same") && !FakeNitro::isEmojiOnly(content))
        return sentence.toInt();
    return s.value(QStringLiteral("emojiSize")).toString().toInt();
}

// Upstream's patches make Discord's emoji picker (for messages, not
// reactions) and sticker picker offer everything; Qord's pickers ask the host
void FakeNitroPlugin::applyPickerBypass()
{
    if (!context)
        return;
    if (auto *expressions = context->expressions())
        expressions->setChatBypass(context->settings().boolValue(QStringLiteral("enableEmojiBypass")),
                                   context->settings().boolValue(QStringLiteral("enableStickerBypass")));
}

bool FakeNitroPlugin::hasPermission(Id accountId, Id channelId, quint64 permission) const
{
    auto *expressions = context ? context->expressions() : nullptr;
    return !expressions || expressions->hasChannelPermission(accountId, channelId, permission);
}

bool FakeNitroPlugin::canUseEmote(const CustomEmojiInfo &emoji, Id accountId, Id guildId, Id channelId) const
{
    if (!emoji.available)
        return false;
    auto *expressions = context ? context->expressions() : nullptr;
    if (!expressions)
        return true;

    // isUnusableRoleSubscriptionEmoji(): Qord can't tell role subscription
    // roles from others, so any emoji kept to roles you don't have counts,
    // unless you are an administrator of its server
    if (!emoji.managed && emoji.guildId && !emoji.roles.isEmpty()) {
        const QList<Id> myRoles = expressions->ownRoles(accountId, emoji.guildId);
        const bool hasRole = std::any_of(emoji.roles.begin(), emoji.roles.end(),
                                         [&](Id role) { return myRoles.contains(role); });
        if (!hasRole && !expressions->hasGuildPermission(accountId, emoji.guildId, Administrator))
            return false;
    }

    bool isUsableTwitchSubEmote = false;
    if (emoji.managed && emoji.guildId) {
        const QList<Id> myRoles = expressions->ownRoles(accountId, emoji.guildId);
        isUsableTwitchSubEmote = std::any_of(emoji.roles.begin(), emoji.roles.end(),
                                             [&](Id role) { return myRoles.contains(role); });
    }

    const bool canUseEmotes = expressions->premiumType(accountId) > 0;
    if (canUseEmotes || isUsableTwitchSubEmote)
        return emoji.guildId == guildId || hasPermission(accountId, channelId, UseExternalEmojis);
    return !emoji.animated && emoji.guildId == guildId;
}

bool FakeNitroPlugin::confirmWithoutEmbeds()
{
    auto *ui = context ? context->ui() : nullptr;
    if (!ui)
        return true;
    UiService::Confirmation confirmation;
    confirmation.title = QStringLiteral("Hold on!");
    confirmation.text = QStringLiteral(
            "You are trying to send/edit a message that contains a FakeNitro emoji or sticker, however you do not have "
            "permissions to embed links in the current channel. Are you sure you want to send this message? Your "
            "FakeNitro items will appear as a link only.");
    confirmation.confirmText = QStringLiteral("Send Anyway");
    confirmation.cancelText = QStringLiteral("Cancel");
    confirmation.checkboxText = QStringLiteral("Disable this check");
    confirmation.checkboxChecked = context->settings().boolValue(QStringLiteral("disableEmbedPermissionCheck"));
    const bool confirmed = ui->confirm(confirmation);
    if (context && confirmation.checkboxChecked !=
                           context->settings().boolValue(QStringLiteral("disableEmbedPermissionCheck")))
        context->settings().setValue(QStringLiteral("disableEmbedPermissionCheck"), confirmation.checkboxChecked);
    return confirmed;
}

void FakeNitroPlugin::onBeforeSend(OutgoingMessage &message)
{
    if (!context)
        return;
    PluginSettings &s = context->settings();
    const bool emojiBypass = s.boolValue(QStringLiteral("enableEmojiBypass"));
    const bool stickerBypass = s.boolValue(QStringLiteral("enableStickerBypass"));
    auto *expressions = context->expressions();
    if (!expressions)
        return;

    bool hasBypass = false;
    // Qord: what to upload instead when links won't embed here
    const QString original = message.content;
    QList<Upload> uploads;
    QStringList fakeTokens;
    const int uploadSize = int(s.value(QStringLiteral("emojiSize")).toString().toInt());

    // Qord: every sticker (Discord takes three), not only the first
    QList<Upload> animatedStickers;
    if (stickerBypass && !message.stickerIds.isEmpty()) {
        // Discord's own stickers are free; usable ones are sent as they are
        const bool canUseStickers = expressions->premiumType(message.accountId) > 1 &&
                                    hasPermission(message.accountId, message.channelId, UseExternalStickers);
        const int size = int(s.value(QStringLiteral("stickerSize")).toString().toInt());
        QList<Id> kept;
        for (const Id stickerId : std::as_const(message.stickerIds)) {
            const std::optional<StickerInfo> sticker = expressions->sticker(message.accountId, stickerId);
            if (!sticker || sticker->standard ||
                (sticker->available && (canUseStickers || sticker->guildId == message.guildId))) {
                kept.append(stickerId);
                continue;
            }
            if (sticker->formatType == StickerApng) {
                if (!hasPermission(message.accountId, message.channelId, AttachFiles)) {
                    if (auto *ui = context->ui()) {
                        UiService::Confirmation notice;
                        notice.title = QStringLiteral("Hold on!");
                        notice.text = QStringLiteral(
                                "You cannot send this message because it contains an animated FakeNitro sticker, and "
                                "you do not have permissions to attach files in the current channel. Please remove the "
                                "sticker to proceed.");
                        notice.confirmText = QStringLiteral("OK");
                        ui->confirm(notice);
                    }
                    message.cancel = true;
                    return;
                }
                const QString png =
                        QStringLiteral("https://media.discordapp.net/stickers/%1.png?size=%2").arg(sticker->id).arg(size);
                // Qord: converted and sent with the message; upstream puts it
                // in the message box and stops the send
                if (!s.boolValue(QStringLiteral("attachAnimatedStickers")) || message.deferred || !context->network()) {
                    sendAnimatedSticker(*sticker, png);
                    message.cancel = true;
                    return;
                }
                animatedStickers.append({ png, QStringLiteral("%1.gif").arg(sticker->id), QStringLiteral("image/gif"), true });
                continue;
            }
            hasBypass = true;
            const bool gif = sticker->formatType == StickerGif;
            uploads.append({ uploadUrl(sticker->id, true, gif, size),
                             sticker->name + (gif ? QStringLiteral(".gif") : QStringLiteral(".png")),
                             gif ? QStringLiteral("image/gif") : QStringLiteral("image/png") });
            const QString link = stickerLink(sticker->id, sticker->name, sticker->formatType, size);
            message.content += wordBoundary(message.content, message.content.size() - 1) +
                               linkMarkup(link, sticker->name, s.boolValue(QStringLiteral("useStickerHyperLinks")),
                                          linkText());
        }
        message.stickerIds = kept;
    }

    if (emojiBypass) {
        static const QRegularExpression customEmoji(QStringLiteral(R"(<(a?):(\w+):(\d+)>)"));
        // Each custom emoji once, in order; every occurrence is replaced
        QList<QString> seen;
        auto it = customEmoji.globalMatch(message.content);
        QList<QRegularExpressionMatch> matches;
        while (it.hasNext())
            matches.append(it.next());
        for (const QRegularExpressionMatch &m : std::as_const(matches)) {
            const QString emojiString = m.captured(0);
            if (seen.contains(emojiString))
                continue;
            seen.append(emojiString);
            const std::optional<CustomEmojiInfo> emoji = expressions->customEmoji(message.accountId, toId(m.captured(3)));
            if (!emoji || canUseEmote(*emoji, message.accountId, message.guildId, message.channelId))
                continue;
            hasBypass = true;
            fakeTokens.append(emojiString);
            uploads.append({ uploadUrl(emoji->id, false, emoji->animated, uploadSize),
                             emoji->name + (emoji->animated ? QStringLiteral(".gif") : QStringLiteral(".png")),
                             emoji->animated ? QStringLiteral("image/gif") : QStringLiteral("image/png") });
            const int size = emojiSizeFor(message.content);
            const QString link = linkMarkup(emojiLink(emoji->id, emoji->name, emoji->animated, size, true), emoji->name,
                                            s.boolValue(QStringLiteral("useEmojiHyperLinks")), linkText());
            qsizetype offset;
            while ((offset = message.content.indexOf(emojiString)) >= 0) {
                const QString &orig = message.content;
                const QString replacement =
                        wordBoundary(orig, offset - 1) + link + wordBoundary(orig, offset + emojiString.size());
                message.content.replace(offset, emojiString.size(), replacement);
            }
        }
    }

    if (hasBypass && !hasPermission(message.accountId, message.channelId, EmbedLinks)) {
        // Qord: where files can be attached, the images go as files instead
        if (s.boolValue(QStringLiteral("uploadWithoutEmbedLinks")) &&
            hasPermission(message.accountId, message.channelId, AttachFiles) && !message.deferred &&
            context->network()) {
            uploadInstead(message, withoutTokens(original, fakeTokens), uploads + animatedStickers);
            return;
        }
        if (!s.boolValue(QStringLiteral("disableEmbedPermissionCheck")) && !confirmWithoutEmbeds()) {
            message.cancel = true;
            return;
        }
    }

    if (!animatedStickers.isEmpty()) {
        uploadInstead(message, message.content, animatedStickers);
        return;
    }

    if (s.boolValue(QStringLiteral("splitLongMessages")))
        splitLongMessage(message);
}

// Qord: Discord refuses a message over the length limit; send it as several
void FakeNitroPlugin::splitLongMessage(OutgoingMessage &message)
{
    // Another plugin's split or deferred send is left alone
    if (!message.followUps.isEmpty() || message.deferred)
        return;
    static const QString SilentPrefix = QStringLiteral("@silent ");
    const bool silent = message.content.startsWith(SilentPrefix);
    const QString text = silent ? message.content.mid(SilentPrefix.size()) : message.content;
    const int limit = messageLengthLimit(context->expressions()->premiumType(message.accountId));
    if (text.size() <= limit)
        return;
    QStringList parts = splitMessage(text, limit);
    if (silent)
        for (QString &part : parts)
            part.prepend(SilentPrefix);
    message.content = parts.takeFirst();
    message.followUps = parts;
}

void FakeNitroPlugin::onBeforeEdit(OutgoingMessage &message)
{
    if (!context)
        return;
    PluginSettings &s = context->settings();
    auto *expressions = context->expressions();
    if (!s.boolValue(QStringLiteral("enableEmojiBypass")) || !expressions)
        return;

    bool hasBypass = false;
    static const QRegularExpression customEmoji(QStringLiteral(R"((?<!\\)<a?:(?:\w+):(\d+)>)"),
                                                QRegularExpression::CaseInsensitiveOption);
    const QString orig = message.content;
    QString out;
    qsizetype last = 0;
    auto it = customEmoji.globalMatch(orig);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += orig.mid(last, m.capturedStart() - last);
        last = m.capturedEnd();
        const std::optional<CustomEmojiInfo> emoji = expressions->customEmoji(message.accountId, toId(m.captured(1)));
        if (!emoji || canUseEmote(*emoji, message.accountId, message.guildId, message.channelId)) {
            out += m.captured(0);
            continue;
        }
        hasBypass = true;
        const int size = emojiSizeFor(orig);
        out += wordBoundary(orig, m.capturedStart() - 1) +
               linkMarkup(emojiLink(emoji->id, emoji->name, emoji->animated, size, false), emoji->name,
                          s.boolValue(QStringLiteral("useEmojiHyperLinks")), linkText()) +
               wordBoundary(orig, m.capturedEnd());
    }
    out += orig.mid(last);
    message.content = out;

    if (hasBypass && !s.boolValue(QStringLiteral("disableEmbedPermissionCheck")) &&
        !hasPermission(message.accountId, message.channelId, EmbedLinks) && !confirmWithoutEmbeds())
        message.cancel = true;
}

void FakeNitroPlugin::transformIncomingContent(IncomingMessage &message)
{
    if (!context)
        return;
    PluginSettings &s = context->settings();
    const bool transformEmojis = s.boolValue(QStringLiteral("transformEmojis"));
    const bool transformStickers = s.boolValue(QStringLiteral("transformStickers"));
    if (!transformEmojis && !transformStickers)
        return;
    const bool compound = s.boolValue(QStringLiteral("transformCompoundSentence"));
    auto *expressions = context->expressions();
    const Id accountId = message.accountId;
    const auto stickerName = [expressions, accountId](Id id) -> std::optional<QString> {
        if (!expressions)
            return std::nullopt;
        if (auto sticker = expressions->sticker(accountId, id))
            return sticker->name;
        return std::nullopt;
    };
    const auto knownSticker = [&stickerName](Id id) { return stickerName(id).has_value(); };
    const auto emojiName = [expressions, accountId](Id id) -> std::optional<QString> {
        if (!expressions)
            return std::nullopt;
        if (auto emoji = expressions->customEmoji(accountId, id))
            return emoji->name;
        return std::nullopt;
    };

    const QString original = message.content;

    // transformAst(): links become the emoji they stand for, sticker links go
    // (the sticker is shown instead)
    const QList<ContentLink> links = topLevelLinks(original);
    if (compound || !isCompound(original, links)) {
        for (auto it = links.crbegin(); it != links.crend(); ++it) {
            if (transformStickers && isFakeStickerLink(it->target, knownSticker)) {
                message.content.remove(it->start, it->length);
                continue;
            }
            if (!transformEmojis)
                continue;
            if (auto emoji = fakeEmojiFromLink(it->target, emojiName)) {
                message.content.replace(it->start, it->length,
                                        QStringLiteral("<%1:%2:%3>")
                                                .arg(emoji->animated ? QStringLiteral("a") : QString(),
                                                     markupName(emoji->name))
                                                .arg(emoji->id));
                message.emojiNotes.insert(emoji->id, QStringLiteral("This is a FakeNitro emoji and renders like a real "
                                                                    "emoji only for you. Appears as a link to "
                                                                    "non-plugin users."));
            }
        }
    }

    QStringList gifUrls;
    for (int i = 0; i < message.attachments.size(); ++i) {
        const auto &attachment = message.attachments[i];
        if (attachment.contentType == QLatin1String("image/gif"))
            gifUrls.append(attachment.url);
        if (transformStickers && isFakeStickerAttachment(attachment.url, attachment.contentType, knownSticker))
            message.hiddenAttachments.append(i);
    }
    if (transformStickers)
        for (const FakeSticker &sticker : fakeStickers(original, gifUrls, compound, stickerName))
            message.addedStickers.append({ sticker.id, sticker.name, sticker.formatType,
                                           QStringLiteral("This is a FakeNitro sticker and renders like a real sticker "
                                                          "only for you. Appears as a link to non-plugin users.") });

    for (int i = 0; i < message.embeds.size(); ++i)
        if (shouldIgnoreEmbed(message.embeds[i].type, message.embeds[i].url, original, compound, transformEmojis,
                              transformStickers, knownSticker))
            message.hiddenEmbeds.append(i);
}

// Animated (APNG) stickers can't be linked: upstream redraws them as a GIF
// and puts it in the upload queue
// The message goes once every image is downloaded, with them as its files
// (Discord takes ten at most)
void FakeNitroPlugin::uploadInstead(OutgoingMessage &message, const QString &text, const QList<Upload> &uploads)
{
    message.content = text;
    const QList<Upload> files = uploads.mid(0, 10);
    const int stickerSize = int(context->settings().value(QStringLiteral("stickerSize")).toString().toInt());
    std::weak_ptr<bool> weak = alive;
    message.deferred = [this, weak, files, stickerSize](const QString &content, OutgoingMessage::DeferredDone done) {
        struct State
        {
            QList<std::optional<AttachedFile>> results;
            int pending = 0;
            bool failed = false;
        };
        auto state = std::make_shared<State>();
        state->results.resize(files.size());
        state->pending = int(files.size());
        for (int i = 0; i < files.size(); ++i) {
            const Upload upload = files[i];
            context->network()->get(QUrl(upload.url), [this, weak, state, i, upload, content, done,
                                                      stickerSize](const NetworkService::Reply &reply) {
                auto a = weak.lock();
                if (!a || !*a || !context)
                    return done(std::nullopt);
                const QByteArray data = !reply.ok ? QByteArray()
                                        : upload.apng
                                                ? ApngToGif::convert(reply.body, stickerSize > 0 ? stickerSize : 160)
                                                : reply.body;
                if (!data.isEmpty())
                    state->results[i] = AttachedFile { upload.fileName, upload.mimeType, data };
                else
                    state->failed = true;
                if (--state->pending > 0)
                    return;
                if (state->failed) {
                    if (auto *ui = context->ui())
                        ui->showToast(QStringLiteral("FakeNitro couldn't download an emoji or sticker to attach."),
                                      UiService::ToastType::Failure);
                    return done(std::nullopt);
                }
                QList<AttachedFile> attached;
                for (const auto &file : state->results)
                    attached.append(*file);
                done(content, attached);
            });
        }
    };
}

void FakeNitroPlugin::sendAnimatedSticker(const StickerInfo &sticker, const QString &link)
{
    auto *network = context->network();
    if (!network)
        return;
    const int resolution = int(context->settings().value(QStringLiteral("stickerSize")).toString().toInt());
    std::weak_ptr<bool> weak = alive;
    const Id id = sticker.id;
    network->get(QUrl(link), [this, weak, id, resolution](const NetworkService::Reply &reply) {
        auto a = weak.lock();
        if (!a || !*a || !context || !reply.ok)
            return;
        const QByteArray gif = ApngToGif::convert(reply.body, resolution > 0 ? resolution : 160);
        if (gif.isEmpty())
            return;
        if (auto *ui = context->ui())
            ui->attachFile(QStringLiteral("%1.gif").arg(id), gif, QStringLiteral("image/gif"));
    });
}

QByteArray qord_manifest_fakenitro();

void qord_register_fakenitro(Registry &registry)
{
    registry.add({ QStringLiteral("fakenitro/plugin.json"), [] { return std::make_unique<FakeNitroPlugin>(); },
                   qord_manifest_fakenitro() });
}

} // namespace Plugins
} // namespace Qord
