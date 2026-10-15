#include "ApngToGif.hpp"
#include "FakeNitro.hpp"
#include "PluginTestUtils.hpp"

#include <QBuffer>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

namespace Qord::Plugins {
void qord_register_fakenitro(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

namespace {

constexpr Id Channel = 500000000000000001ULL;

QJsonObject golden()
{
    QFile file(QStringLiteral(QORD_TEST_DATA_DIR "/fakenitro-golden.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

Id idOf(const QJsonValue &v)
{
    return v.toString().toULongLong();
}

quint64 permissionBit(const QString &name)
{
    if (name == QLatin1String("EMBED_LINKS"))
        return FakeNitro::EmbedLinks;
    if (name == QLatin1String("ATTACH_FILES"))
        return FakeNitro::AttachFiles;
    if (name == QLatin1String("USE_EXTERNAL_EMOJIS"))
        return FakeNitro::UseExternalEmojis;
    if (name == QLatin1String("USE_EXTERNAL_STICKERS"))
        return FakeNitro::UseExternalStickers;
    return 0;
}

// The golden data's stores, as the client's expression service
void fillStores(FakeExpressions &expressions, const QJsonObject &g)
{
    const QJsonObject emojis = g[u"emojis"].toObject();
    for (auto it = emojis.begin(); it != emojis.end(); ++it) {
        const QJsonObject e = it.value().toObject();
        CustomEmojiInfo info;
        info.id = idOf(e[u"id"]);
        info.name = e[u"name"].toString();
        info.animated = e[u"animated"].toBool();
        info.guildId = idOf(e[u"guildId"]);
        info.available = e[u"available"].toBool();
        info.managed = e[u"managed"].toBool();
        for (const auto &r : e[u"roles"].toArray())
            info.roles.append(idOf(r));
        expressions.emojis.insert(info.id, info);
    }
    const QJsonObject stickers = g[u"stickers"].toObject();
    for (auto it = stickers.begin(); it != stickers.end(); ++it) {
        const QJsonObject s = it.value().toObject();
        StickerInfo info;
        info.id = idOf(s[u"id"]);
        info.name = s[u"name"].toString();
        info.formatType = s[u"format_type"].toInt();
        info.guildId = idOf(s[u"guild_id"]);
        info.available = s[u"available"].toBool();
        info.standard = s.contains(u"pack_id");
        expressions.stickers.insert(info.id, info);
    }
}

void applyCase(SinglePluginHost &h, const QJsonObject &c)
{
    h.expressions.premium = c[u"premium"].toInt();
    h.expressions.denied = 0;
    for (const auto &d : c[u"denied"].toArray())
        h.expressions.denied |= permissionBit(d.toString());
    h.expressions.roles.clear();
    const QJsonObject roles = c[u"myRoles"].toObject();
    for (auto it = roles.begin(); it != roles.end(); ++it)
        for (const auto &r : it.value().toArray())
            h.expressions.roles[it.key().toULongLong()].append(idOf(r));
    h.ui.confirmAnswer = !c.contains(u"confirm") || c[u"confirm"].toBool();
    h.ui.confirmations.clear();
    h.network.urls.clear();

    // Defaults, then the case's settings
    auto *s = h.host->settings(h.id);
    const QJsonObject defaults {
        { "enableEmojiBypass", true }, { "emojiSize", "48" }, { "transformEmojis", true },
        { "enableStickerBypass", true }, { "stickerSize", "160" }, { "transformStickers", true },
        { "transformCompoundSentence", false }, { "useStickerHyperLinks", true }, { "useEmojiHyperLinks", true },
        { "hyperLinkText", "{{NAME}}" }, { "disableEmbedPermissionCheck", false },
    };
    QJsonObject settings = defaults;
    const QJsonObject overrides = c[u"settings"].toObject();
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
        settings[it.key()] = it.value().isDouble() ? QJsonValue(QString::number(it.value().toInt())) : it.value();
    for (auto it = settings.begin(); it != settings.end(); ++it)
        s->setValue(it.key(), it.value());
}

int countConfirmations(const FakeUi &ui, const QString &confirmText)
{
    int n = 0;
    for (const auto &c : ui.confirmations)
        n += c.confirmText == confirmText ? 1 : 0;
    return n;
}

QByteArray chunk(const char *type, const QByteArray &data)
{
    QByteArray typed(type, 4);
    typed += data;
    quint32 crc = 0xFFFFFFFFu;
    for (const char ch : typed) {
        crc ^= quint8(ch);
        for (int k = 0; k < 8; ++k)
            crc = (crc & 1) ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
    }
    crc ^= 0xFFFFFFFFu;
    QByteArray out;
    const auto dword = [&out](quint32 v) {
        out.append(char(v >> 24)).append(char(v >> 16)).append(char(v >> 8)).append(char(v));
    };
    dword(quint32(data.size()));
    out += typed;
    dword(crc);
    return out;
}

QByteArray dword(quint32 v)
{
    QByteArray out;
    out.append(char(v >> 24)).append(char(v >> 16)).append(char(v >> 8)).append(char(v));
    return out;
}

// The IDAT data of a PNG Qt writes
QByteArray idatOf(const QImage &image, QByteArray *ihdr)
{
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    QByteArray data;
    for (int off = 8; off + 12 <= png.size();) {
        const quint32 length = (quint32(quint8(png[off])) << 24) | (quint32(quint8(png[off + 1])) << 16) |
                               (quint32(quint8(png[off + 2])) << 8) | quint32(quint8(png[off + 3]));
        const QByteArray type = png.mid(off + 4, 4);
        if (type == "IHDR" && ihdr)
            *ihdr = png.mid(off + 8, length);
        if (type == "IDAT")
            data += png.mid(off + 8, length);
        off += 12 + int(length);
    }
    return data;
}

// A two-frame 8x8 APNG: red, then a blue square over it (blend over, no disposal)
QByteArray makeApng()
{
    QImage first(8, 8, QImage::Format_ARGB32);
    first.fill(QColor(255, 0, 0));
    QImage second(4, 4, QImage::Format_ARGB32);
    second.fill(QColor(0, 0, 255));
    QByteArray ihdr;
    const QByteArray firstData = idatOf(first, &ihdr);
    QByteArray secondIhdr;
    const QByteArray secondData = idatOf(second, &secondIhdr);
    // Both frames must share the colour type; Qt writes RGBA for ARGB32 images
    const auto fctl = [](quint32 seq, quint32 w, quint32 h, quint32 x, quint32 y, int blend) {
        QByteArray d = dword(seq) + dword(w) + dword(h) + dword(x) + dword(y);
        d.append(char(0)).append(char(5)).append(char(0)).append(char(100)); // 5/100 s
        d.append(char(0)).append(char(blend));
        return d;
    };
    QByteArray apng("\x89PNG\r\n\x1a\n", 8);
    apng += chunk("IHDR", ihdr);
    apng += chunk("acTL", dword(2) + dword(0));
    apng += chunk("fcTL", fctl(0, 8, 8, 0, 0, 0));
    apng += chunk("IDAT", firstData);
    apng += chunk("fcTL", fctl(1, 4, 4, 2, 2, 1));
    apng += chunk("fdAT", dword(2) + secondData);
    apng += chunk("IEND", {});
    return apng;
}

} // namespace

class TestFakeNitro : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { QVERIFY(!golden().isEmpty()); }

    void wordBoundaryMatchesUpstream()
    {
        for (const auto &v : golden()[u"boundaries"].toArray()) {
            const QJsonObject c = v.toObject();
            QCOMPARE(FakeNitro::wordBoundary(c[u"s"].toString(), c[u"offset"].toInt()), c[u"expected"].toString());
        }
    }

    // Upstream's send listener: emoji and sticker bypass, permissions, the
    // embed notice and animated stickers
    void sendMatchesUpstream()
    {
        const QJsonObject g = golden();
        SinglePluginHost h(qord_register_fakenitro);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        fillStores(h.expressions, g);
        for (const auto &v : g[u"send"].toArray()) {
            const QJsonObject c = v.toObject();
            const QString name = c[u"name"].toString();
            applyCase(h, c);
            OutgoingMessage out;
            out.accountId = 1;
            out.channelId = Channel;
            out.guildId = idOf(c[u"guildId"]);
            out.content = c[u"content"].toString();
            for (const auto &s : c[u"stickerIds"].toArray())
                out.stickerIds.append(idOf(s));
            h.host->dispatchBeforeSend(out);

            const QJsonObject e = c[u"expected"].toObject();
            QVERIFY2(out.content == e[u"content"].toString(),
                     qPrintable(name + QStringLiteral(": ") + out.content));
            QList<Id> stickers;
            for (const auto &s : e[u"stickerIds"].toArray())
                stickers.append(idOf(s));
            QVERIFY2(out.stickerIds == stickers, qPrintable(name));
            QVERIFY2(out.cancel == e[u"cancel"].toBool(), qPrintable(name));
            QVERIFY2(countConfirmations(h.ui, QStringLiteral("Send Anyway")) == e[u"embedNotices"].toInt(),
                     qPrintable(name));
            QVERIFY2(countConfirmations(h.ui, QStringLiteral("OK")) == e[u"modals"].toInt(), qPrintable(name));
            QStringList fetched;
            for (const QUrl &url : std::as_const(h.network.urls))
                fetched.append(url.toString());
            QStringList expectedFetches;
            for (const auto &a : e[u"animatedSent"].toArray())
                expectedFetches.append(a.toObject()[u"link"].toString());
            QVERIFY2(fetched == expectedFetches, qPrintable(name));
        }
    }

    void editMatchesUpstream()
    {
        const QJsonObject g = golden();
        SinglePluginHost h(qord_register_fakenitro);
        QVERIFY(h.enableWithAllPermissions());
        fillStores(h.expressions, g);
        for (const auto &v : g[u"edit"].toArray()) {
            const QJsonObject c = v.toObject();
            applyCase(h, c);
            OutgoingMessage out;
            out.accountId = 1;
            out.channelId = Channel;
            out.guildId = idOf(c[u"guildId"]);
            out.messageId = 7;
            out.isEdit = true;
            out.content = c[u"content"].toString();
            h.host->dispatchBeforeEdit(out);
            const QJsonObject e = c[u"expected"].toObject();
            QCOMPARE(out.content, e[u"content"].toString());
            QCOMPARE(out.cancel, e[u"cancel"].toBool());
            QCOMPARE(countConfirmations(h.ui, QStringLiteral("Send Anyway")), e[u"embedNotices"].toInt());
        }
    }

    void receivedLinksMatchUpstream()
    {
        const QJsonObject g = golden();
        FakeExpressions stores;
        fillStores(stores, g);
        const auto emojiName = [&](Id id) -> std::optional<QString> {
            if (stores.emojis.contains(id))
                return stores.emojis.value(id).name;
            return std::nullopt;
        };
        const auto stickerName = [&](Id id) -> std::optional<QString> {
            if (stores.stickers.contains(id))
                return stores.stickers.value(id).name;
            return std::nullopt;
        };
        const auto knownSticker = [&](Id id) { return stores.stickers.contains(id); };

        // transformAst()
        for (const auto &v : g[u"ast"].toArray()) {
            const QJsonObject c = v.toObject();
            const QString target = c[u"target"].toString();
            const QJsonValue result = c[u"result"];
            if (result.isNull()) {
                QVERIFY2(FakeNitro::isFakeStickerLink(target, knownSticker), qPrintable(target));
                continue;
            }
            QVERIFY2(!FakeNitro::isFakeStickerLink(target, knownSticker), qPrintable(target));
            const QJsonObject node = result.toObject();
            const auto emoji = FakeNitro::fakeEmojiFromLink(target, emojiName);
            if (node[u"type"].toString() == QLatin1String("link")) {
                QVERIFY2(!emoji, qPrintable(target));
                continue;
            }
            QVERIFY2(emoji, qPrintable(target));
            QCOMPARE(emoji->id, idOf(node[u"emojiId"]));
            QCOMPARE(QStringLiteral(":%1:").arg(emoji->name), node[u"name"].toString());
            QCOMPARE(emoji->animated, node[u"animated"].toBool());
        }

        // patchFakeNitroStickers()
        for (const auto &v : g[u"stickers_"].toArray()) {
            const QJsonObject c = v.toObject();
            QStringList gifs;
            for (const auto &u : c[u"gifs"].toArray())
                gifs.append(u.toString());
            const auto stickers = FakeNitro::fakeStickers(c[u"content"].toString(), gifs, c[u"compound"].toBool(),
                                                          stickerName);
            const QJsonArray expected = c[u"expected"].toArray();
            QCOMPARE(stickers.size(), expected.size());
            for (int i = 0; i < stickers.size(); ++i) {
                const QJsonObject e = expected[i].toObject();
                QCOMPARE(stickers[i].id, idOf(e[u"id"]));
                QCOMPARE(stickers[i].name, e[u"name"].toString());
                QCOMPARE(stickers[i].formatType, e[u"format_type"].toInt());
            }
        }

        // shouldIgnoreEmbed()
        for (const auto &v : g[u"embeds"].toArray()) {
            const QJsonObject c = v.toObject();
            const bool transformStickers = !c.contains(u"stickers") || c[u"stickers"].toBool();
            QVERIFY2(FakeNitro::shouldIgnoreEmbed(c[u"type"].toString(), c[u"url"].toString(), c[u"content"].toString(),
                                                  c[u"compound"].toBool(), true, transformStickers, knownSticker) ==
                             c[u"expected"].toBool(),
                     qPrintable(c[u"content"].toString()));
        }

        // filterAttachments()
        for (const auto &v : g[u"attachments"].toArray()) {
            const QJsonObject c = v.toObject();
            QCOMPARE(FakeNitro::isFakeStickerAttachment(c[u"url"].toString(), c[u"content_type"].toString(),
                                                        knownSticker),
                     c[u"hidden"].toBool());
        }
    }

    // The whole display hook: a message that is only a fake emoji link shows
    // the emoji and hides the link's image embed
    void transformsReceivedMessages()
    {
        SinglePluginHost h(qord_register_fakenitro);
        QVERIFY(h.enableWithAllPermissions());
        fillStores(h.expressions, golden());
        const QString link = QStringLiteral(
                "https://cdn.discordapp.com/emojis/200000000000000002.gif?size=48&animated=true&name=party&lossless=true");

        IncomingMessage in;
        in.content = QStringLiteral("[party](%1)").arg(link);
        in.embeds = { { QStringLiteral("image"), link } };
        h.host->dispatchTransformIncomingContent(in);
        QCOMPARE(in.content, QStringLiteral("<a:party:200000000000000002>"));
        QCOMPARE(in.hiddenEmbeds, QList<int> { 0 });

        // Unknown emoji: the name comes from the link, made safe for the markup
        IncomingMessage unknown;
        unknown.content = QStringLiteral(
                "https://cdn.discordapp.com/emojis/299999999999999999.webp?size=48&name=some+thing&lossless=true");
        h.host->dispatchTransformIncomingContent(unknown);
        QCOMPARE(unknown.content, QStringLiteral("<:some_thing:299999999999999999>"));

        // More than the link: left alone, unless compound sentences are on
        IncomingMessage compound;
        compound.content = QStringLiteral("look [party](%1) `[x](%1)`").arg(link);
        compound.embeds = { { QStringLiteral("image"), link } };
        h.host->dispatchTransformIncomingContent(compound);
        QCOMPARE(compound.content, QStringLiteral("look [party](%1) `[x](%1)`").arg(link));
        QVERIFY(compound.hiddenEmbeds.isEmpty());
        h.host->settings(h.id)->setValue(QStringLiteral("transformCompoundSentence"), true);
        IncomingMessage compound2;
        compound2.content = compound.content;
        compound2.embeds = compound.embeds;
        h.host->dispatchTransformIncomingContent(compound2);
        QCOMPARE(compound2.content, QStringLiteral("look <a:party:200000000000000002> `[x](%1)`").arg(link));
        QCOMPARE(compound2.hiddenEmbeds, QList<int> { 0 });
        h.host->settings(h.id)->setValue(QStringLiteral("transformCompoundSentence"), false);

        // A sticker link becomes the sticker
        IncomingMessage sticker;
        sticker.content = QStringLiteral(
                "[png far](https://media.discordapp.net/stickers/400000000000000004.png?size=160&name=png+far&lossless=true)");
        h.host->dispatchTransformIncomingContent(sticker);
        QCOMPARE(sticker.content, QString());
        QCOMPARE(sticker.addedStickers.size(), 1);
        QCOMPARE(sticker.addedStickers[0].id, Id(400000000000000004ULL));
        QCOMPARE(sticker.addedStickers[0].name, QStringLiteral("png far"));

        // A GIF upload of a known sticker is shown as the sticker
        IncomingMessage gif;
        gif.attachments = { { QStringLiteral("https://cdn.discordapp.com/attachments/1/2/400000000000000002.gif"),
                              QStringLiteral("image/gif") } };
        h.host->dispatchTransformIncomingContent(gif);
        QCOMPARE(gif.hiddenAttachments, QList<int> { 0 });
        QCOMPARE(gif.addedStickers.size(), 1);
        QCOMPARE(gif.addedStickers[0].formatType, FakeNitro::StickerApng);

        // Both transforms off: nothing changes
        h.host->settings(h.id)->setValue(QStringLiteral("transformEmojis"), false);
        h.host->settings(h.id)->setValue(QStringLiteral("transformStickers"), false);
        IncomingMessage off;
        off.content = in.content = QStringLiteral("[party](%1)").arg(link);
        off.embeds = { { QStringLiteral("image"), link } };
        h.host->dispatchTransformIncomingContent(off);
        QCOMPARE(off.content, QStringLiteral("[party](%1)").arg(link));
        QVERIFY(off.hiddenEmbeds.isEmpty());
    }

    void linksInMessages()
    {
        const auto links = FakeNitro::topLevelLinks(
                QStringLiteral("a https://x.y/z. [t](https://a.b/c) `https://no.pe` <https://auto.link> (https://p.q/r)"));
        QStringList targets;
        for (const auto &l : links)
            targets.append(l.target);
        QCOMPARE(targets, (QStringList { QStringLiteral("https://x.y/z"), QStringLiteral("https://a.b/c"),
                                         QStringLiteral("https://auto.link"), QStringLiteral("https://p.q/r") }));
        const QString only = QStringLiteral("[t](https://a.b/c)");
        QVERIFY(!FakeNitro::isCompound(only, FakeNitro::topLevelLinks(only)));
        const QString two = QStringLiteral("https://a.b/c https://a.b/d");
        QVERIFY(FakeNitro::isCompound(two, FakeNitro::topLevelLinks(two)));
    }

    void pickerBypassFollowsSettings()
    {
        SinglePluginHost h(qord_register_fakenitro);
        QVERIFY(h.enableWithAllPermissions());
        QCOMPARE(h.expressions.bypass, std::make_pair(true, true));
        h.host->settings(h.id)->setValue(QStringLiteral("enableStickerBypass"), false);
        QCOMPARE(h.expressions.bypass, std::make_pair(true, false));
        QVERIFY(h.host->setEnabled(h.id, false));
        QCOMPARE(h.expressions.bypass, std::make_pair(false, false));
    }

    void checkboxDisablesTheNotice()
    {
        SinglePluginHost h(qord_register_fakenitro);
        QVERIFY(h.enableWithAllPermissions());
        fillStores(h.expressions, golden());
        h.expressions.denied = FakeNitro::EmbedLinks;
        h.ui.checkboxAnswer = true;
        OutgoingMessage out;
        out.accountId = 1;
        out.channelId = Channel;
        out.guildId = 100000000000000001ULL;
        out.content = QStringLiteral("<:far_away:200000000000000003>");
        h.host->dispatchBeforeSend(out);
        QVERIFY(!out.cancel);
        QVERIFY(h.host->settings(h.id)->boolValue(QStringLiteral("disableEmbedPermissionCheck")));
    }

    void needsPermissions()
    {
        SinglePluginHost h(qord_register_fakenitro);
        h.host->setGranted(h.id, Permission::ModifyOutgoingMessages | Permission::ModifyDisplayedMessages);
        QVERIFY(h.host->setEnabled(h.id, true));
        fillStores(h.expressions, golden());
        OutgoingMessage out;
        out.guildId = 100000000000000001ULL;
        out.content = QStringLiteral("<:far_away:200000000000000003>");
        h.host->dispatchBeforeSend(out);
        QCOMPARE(out.content, QStringLiteral("<:far_away:200000000000000003>")); // no read_expressions
    }

    // Animated stickers: fetched, redrawn as a GIF and attached
    void animatedStickerBecomesGif()
    {
        SinglePluginHost h(qord_register_fakenitro);
        QVERIFY(h.enableWithAllPermissions());
        fillStores(h.expressions, golden());
        h.host->settings(h.id)->setValue(QStringLiteral("stickerSize"), QStringLiteral("16"));
        OutgoingMessage out;
        out.accountId = 1;
        out.channelId = Channel;
        out.guildId = 100000000000000001ULL;
        out.stickerIds = { 400000000000000002ULL };
        h.host->dispatchBeforeSend(out);
        QVERIFY(out.cancel);
        QCOMPARE(h.network.urls.size(), 1);
        QVERIFY(h.network.respond(true, makeApng()));
        QCOMPARE(h.ui.attachments.size(), 1);
        QCOMPARE(h.ui.attachments[0].fileName, QStringLiteral("400000000000000002.gif"));
        QCOMPARE(h.ui.attachments[0].mimeType, QStringLiteral("image/gif"));
        QVERIFY(h.ui.attachments[0].data.startsWith("GIF89a"));
    }

    void apngFramesAndGif()
    {
        const ApngToGif::Animation anim = ApngToGif::parse(makeApng());
        QCOMPARE(anim.width, 8);
        QCOMPARE(anim.frames.size(), 2);
        QCOMPARE(anim.frames[1].left, 2);
        QCOMPARE(anim.frames[1].blendOp, 1);
        QCOMPARE(anim.frames[0].delayMs, 50.0);
        QCOMPARE(anim.frames[1].image.size(), QSize(4, 4));

        // A plain PNG is not animated
        QByteArray plain;
        QBuffer buffer(&plain);
        buffer.open(QIODevice::WriteOnly);
        QImage(4, 4, QImage::Format_ARGB32).save(&buffer, "PNG");
        QVERIFY(ApngToGif::parse(plain).frames.isEmpty());
        QVERIFY(ApngToGif::convert("not a png", 16).isEmpty());

        const QByteArray gif = ApngToGif::convert(makeApng(), 16);
        QVERIFY(gif.startsWith("GIF89a"));
        QVERIFY(gif.endsWith(';'));
        if (!QImageReader::supportedImageFormats().contains("gif"))
            QSKIP("Qt's GIF reader is not available to check the frames");
        QBuffer in(const_cast<QByteArray *>(&gif));
        in.open(QIODevice::ReadOnly);
        QImageReader reader(&in, "gif");
        QCOMPARE(reader.imageCount(), 2);
        const QImage first = reader.read();
        QCOMPARE(first.size(), QSize(16, 16));
        QCOMPARE(QColor(first.pixel(1, 1)), QColor(255, 0, 0));
        const QImage second = reader.read();
        QVERIFY(!second.isNull());
        QCOMPARE(QColor(second.pixel(1, 1)), QColor(255, 0, 0));
        QCOMPARE(QColor(second.pixel(8, 8)), QColor(0, 0, 255));
        QCOMPARE(reader.nextImageDelay(), 50);
    }

    void gifKeepsTransparency()
    {
        QImage frame(4, 4, QImage::Format_ARGB32);
        frame.fill(Qt::transparent);
        frame.setPixel(1, 1, qRgba(10, 200, 30, 255));
        const QByteArray gif = ApngToGif::encodeGif({ frame }, { 100 });
        if (!QImageReader::supportedImageFormats().contains("gif"))
            QSKIP("Qt's GIF reader is not available");
        QBuffer in(const_cast<QByteArray *>(&gif));
        in.open(QIODevice::ReadOnly);
        const QImage decoded = QImageReader(&in, "gif").read().convertToFormat(QImage::Format_ARGB32);
        QCOMPARE(qAlpha(decoded.pixel(0, 0)), 0);
        // Colours are kept to 5 bits per channel
        const QRgb px = decoded.pixel(1, 1);
        QCOMPARE(qAlpha(px), 255);
        QVERIFY(qAbs(qRed(px) - 10) <= 8 && qAbs(qGreen(px) - 200) <= 8 && qAbs(qBlue(px) - 30) <= 8);
    }
};

QTEST_GUILESS_MAIN(TestFakeNitro)
#include "tst_fakenitro.moc"
