#pragma once

#include "Plugins/Plugin.hpp"

#include <QHash>

#include <functional>
#include <memory>

namespace Qord {
namespace Plugins {

class ExpressionService;

// The pieces of upstream's FakeNitro that work on plain strings, kept static
// so the tests can run them against upstream's own code.
namespace FakeNitro {

// Discord permission bits FakeNitro checks
constexpr quint64 EmbedLinks = quint64(1) << 14;
constexpr quint64 AttachFiles = quint64(1) << 15;
constexpr quint64 UseExternalEmojis = quint64(1) << 18;
constexpr quint64 UseExternalStickers = quint64(1) << 37;

// Discord's sticker format types
constexpr int StickerPng = 1;
constexpr int StickerApng = 2;
constexpr int StickerLottie = 3;
constexpr int StickerGif = 4;

// getWordBoundary(): a space unless `offset` is past either end or on whitespace
QString wordBoundary(const QString &s, qsizetype offset);
// URLSearchParams' serialization (application/x-www-form-urlencoded)
QString formEncode(const QString &s);
QString formDecode(const QString &s);
// A query parameter as URL.searchParams.get() returns it
std::optional<QString> queryParam(const QString &url, const QString &name);

// The link sent in place of an emoji: IconUtils.getEmojiURL(), then size,
// name and lossless set. `forSend` also turns animated .webp into .gif, as
// the send path does (editing does not).
QString emojiLink(Id id, const QString &name, bool animated, int size, bool forSend);
// getStickerLink() with name and lossless set
QString stickerLink(Id id, const QString &name, int formatType, int size);
// [text](url) or the bare url
QString linkMarkup(const QString &url, const QString &name, bool hyperlink, const QString &hyperLinkText);

// transformAst(): a link to a fake emoji, as shown in its place
struct FakeEmoji
{
    Id id = 0;
    QString name; // without colons
    bool animated = false;
};
std::optional<FakeEmoji> fakeEmojiFromLink(const QString &target,
                                           const std::function<std::optional<QString>(Id)> &knownName);
// A link transformAst() drops because the sticker is shown instead
bool isFakeStickerLink(const QString &target, const std::function<bool(Id)> &knownSticker);

// patchFakeNitroStickers(): stickers to show for a message
struct FakeSticker
{
    Id id = 0;
    QString name;
    int formatType = StickerPng;
};
QList<FakeSticker> fakeStickers(const QString &content, const QStringList &gifAttachmentUrls, bool compound,
                                const std::function<std::optional<QString>(Id)> &knownStickerName);
// shouldIgnoreEmbed(): an image embed of a fake emoji or sticker is not shown
bool shouldIgnoreEmbed(const QString &embedType, const QString &embedUrl, const QString &content, bool compound,
                       bool transformEmojis, bool transformStickers, const std::function<bool(Id)> &knownSticker);
// filterAttachments(): a GIF upload of a known sticker is shown as the sticker
bool isFakeStickerAttachment(const QString &url, const QString &contentType,
                             const std::function<bool(Id)> &knownSticker);

// The top-level links of a message (what Discord's parser makes "link"
// nodes of), outside code
struct ContentLink
{
    qsizetype start = 0;
    qsizetype length = 0;
    QString target;
};
QList<ContentLink> topLevelLinks(const QString &content);
// Whether the message is anything but a single link (more than one AST node)
bool isCompound(const QString &content, const QList<ContentLink> &links);

} // namespace FakeNitro

class FakeNitroPlugin : public Plugin
{
public:
    FakeNitroPlugin();
    ~FakeNitroPlugin() override;

    [[nodiscard]] Hooks hooks() const override
    {
        return Hook::BeforeSend | Hook::BeforeEdit | Hook::TransformIncomingContent;
    }
    void start(PluginContext &ctx) override;
    void stop() override;
    void onSettingChanged(const QString &key) override;

    void onBeforeSend(OutgoingMessage &message) override;
    void onBeforeEdit(OutgoingMessage &message) override;
    void transformIncomingContent(IncomingMessage &message) override;

    // canUseEmote()
    [[nodiscard]] bool canUseEmote(const CustomEmojiInfo &emoji, Id accountId, Id guildId, Id channelId) const;

private:
    [[nodiscard]] bool hasPermission(Id accountId, Id channelId, quint64 permission) const;
    [[nodiscard]] bool confirmWithoutEmbeds();
    void applyPickerBypass();
    void sendAnimatedSticker(const StickerInfo &sticker, const QString &link);

    PluginContext *context = nullptr;
    std::shared_ptr<bool> alive;
};

} // namespace Plugins
} // namespace Qord
