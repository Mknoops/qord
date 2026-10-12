// Qord: the data behind the emoji picker (frequently used, each server's
// emojis you can use here, then all Unicode emoji), built from Acheron's
// EmojiManager state.

#include "Core/Emoji/EmojiManager.hpp"

#include "Core/ClientInstance.hpp"
#include "Core/Emoji/UnicodeEmojiIndex.hpp"
#include "Discord/Client.hpp"

namespace Acheron {
namespace Core {

QList<EmojiManager::PickerSection> EmojiManager::pickerSections(Snowflake channelId, bool forReaction)
{
    UsabilityContext usability;
    usability.channelId = channelId;
    usability.selfId = client->getMe().id.get();
    usability.premium = client->isPremium();
    usability.channelGuildId = client->getGuildIdForChannel(channelId);
    usability.bypass = !forReaction && expressionBypass().emojis;

    const auto customMatch = [](const CustomEmoji &custom) {
        const Snowflake id = custom.emoji.id.get();
        const bool animated = custom.emoji.animated.hasValue() && custom.emoji.animated.get();
        EmojiMatch m;
        m.insertText = (animated ? "<a:" : "<:") + custom.originalName + ":" + id.toString() + ">";
        m.displayLabel = ":" + custom.disambiguatedName + ":";
        m.customId = id;
        m.imageUrl = custom.emoji.getImageUrl();
        return m;
    };
    const auto unicodeMatch = [](const UnicodeEmoji &emoji) {
        EmojiMatch m;
        const QString name = UnicodeEmojiIndex::primaryName(emoji);
        m.insertText = ":" + name + ":";
        m.displayLabel = m.insertText;
        m.surrogates = emoji.surrogates;
        return m;
    };

    QList<PickerSection> sections;

    // Frequently used, from the same frecency data Discord syncs
    FrecencyTracker &tracker = forReaction ? reactionTracker : chatTracker;
    PickerSection frequent;
    frequent.title = QStringLiteral("Frequently Used");
    for (const QString &key : tracker.frequently(nowMs())) {
        if (frequent.emojis.size() >= 16)
            break;
        bool numeric = false;
        key.toULongLong(&numeric);
        if (numeric) {
            for (const CustomEmoji &custom : customEmojis()) {
                if (custom.emoji.id.get().toString() == key && isUsable(custom, usability)) {
                    frequent.emojis.append(customMatch(custom));
                    break;
                }
            }
        } else if (const UnicodeEmoji *emoji = UnicodeEmojiIndex::instance().byName(key)) {
            frequent.emojis.append(unicodeMatch(*emoji));
        }
    }
    if (!frequent.emojis.isEmpty())
        sections.append(frequent);

    // One section per server, current server first
    QList<Snowflake> guildOrder;
    for (const CustomEmoji &custom : customEmojis()) {
        if (!guildOrder.contains(custom.guildId))
            guildOrder.append(custom.guildId);
    }
    if (guildOrder.removeOne(usability.channelGuildId))
        guildOrder.prepend(usability.channelGuildId);
    for (Snowflake guildId : guildOrder) {
        PickerSection section;
        if (const auto guild = instance->getGuild(guildId))
            section.title = guild->name.get();
        for (const CustomEmoji &custom : customEmojis()) {
            if (custom.guildId == guildId && isUsable(custom, usability))
                section.emojis.append(customMatch(custom));
        }
        if (!section.emojis.isEmpty())
            sections.append(section);
    }

    PickerSection unicode;
    unicode.title = QStringLiteral("Emoji");
    for (const UnicodeEmoji *emoji : UnicodeEmojiIndex::instance().topLevel())
        unicode.emojis.append(unicodeMatch(*emoji));
    sections.append(unicode);
    return sections;
}

void EmojiManager::setGuildStickers(Snowflake guildId, const QList<Discord::Sticker> &stickers)
{
    if (guildId.isValid())
        guildStickers.insert(guildId, stickers);
}

void EmojiManager::onGuildStickersUpdated(const Discord::GuildStickersUpdate &event)
{
    if (event.guildId.hasValue())
        setGuildStickers(event.guildId.get(), event.stickers.get());
}

std::optional<Discord::Sticker> EmojiManager::stickerById(Snowflake stickerId) const
{
    for (const auto &stickers : guildStickers)
        for (const Discord::Sticker &sticker : stickers)
            if (sticker.id.get() == stickerId)
                return sticker;
    return std::nullopt;
}

QList<Discord::Sticker> EmojiManager::guildStickerList(Snowflake guildId) const
{
    return guildStickers.value(guildId);
}

QList<Snowflake> EmojiManager::stickerGuilds() const
{
    return guildStickers.keys();
}

std::optional<std::pair<Discord::Emoji, Snowflake>> EmojiManager::customEmojiById(Snowflake emojiId) const
{
    for (auto it = guildEmojis.cbegin(); it != guildEmojis.cend(); ++it)
        for (const Discord::Emoji &emoji : it.value())
            if (emoji.id.hasValue() && emoji.id.get() == emojiId)
                return std::make_pair(emoji, it.key());
    return std::nullopt;
}

} // namespace Core
} // namespace Acheron
