#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <cstdint>
#include <functional>
#include <optional>

#include "Core/Emoji/EmojiMatch.hpp"
#include "Core/Emoji/FrecencyTracker.hpp"
#include "Plugins/Types.hpp"
#include "Core/Snowflake.hpp"
#include "Discord/Entities.hpp"
#include "Discord/Events.hpp"

namespace Acheron {

namespace Discord {
class Client;
}

namespace Proto {
struct FrecencyUserSettings;
}

namespace Core {

// Qord: an image made small enough to upload
struct ShrunkImage
{
    QString fileName;
    QString mimeType;
    QByteArray data;
};

// Qord: FakeNitro lets the pickers offer emojis and stickers Discord would
// lock, Go Live offer Nitro qualities, and shrinks images too large to upload
struct ExpressionBypass
{
    bool emojis = false;
    bool stickers = false;
    bool streamQuality = false;
    std::function<std::optional<ShrunkImage>(const QString &fileName, const QString &mimeType,
                                             const QByteArray &data, qint64 limit)>
            shrinkImage;
};
inline ExpressionBypass &expressionBypass()
{
    static ExpressionBypass bypass;
    return bypass;
}

struct UnicodeEmoji;
class ClientInstance;

class EmojiManager : public QObject
{
    Q_OBJECT
public:
    EmojiManager(Discord::Client *client, ClientInstance *instance, QObject *parent = nullptr);

    void setGuildEmojis(Snowflake guildId, const QList<Discord::Emoji> &emojis);
    void removeGuild(Snowflake guildId);

    // Qord: forReaction leaves out FakeNitro's bypass
    [[nodiscard]] QList<EmojiMatch> search(const QString &query, Snowflake channelId, int maxResults = 10,
                                           bool forReaction = false);

    void trackMessageEmojis(const QString &content);
    void trackReaction(const QString &reactionEmoji);

    // Qord: what the emoji picker shows (src/Qord/EmojiPickerData.cpp)
    struct PickerSection
    {
        QString title;
        QList<EmojiMatch> emojis;
    };
    [[nodiscard]] QList<PickerSection> pickerSections(Snowflake channelId, bool forReaction);
    // Qord: an emoji by frecency key (custom emoji id or Unicode name), if
    // you can use it here
    [[nodiscard]] std::optional<EmojiMatch> matchForKey(const QString &key, Snowflake channelId, bool forReaction);
    // Qord: your Discord favorite emojis (same keys), in Discord's order
    [[nodiscard]] const QStringList &favoriteEmojis() const { return favorites_; }
    // Qord: an emoji as plugins see it
    [[nodiscard]] Qord::Plugins::EmojiInfo emojiInfo(const QString &key, const EmojiMatch &match) const;

    // Qord: your servers' stickers, and lookups by id (src/Qord/EmojiPickerData.cpp)
    void setGuildStickers(Snowflake guildId, const QList<Discord::Sticker> &stickers);
    void onGuildStickersUpdated(const Discord::GuildStickersUpdate &event);
    [[nodiscard]] std::optional<Discord::Sticker> stickerById(Snowflake stickerId) const;
    [[nodiscard]] QList<Discord::Sticker> guildStickerList(Snowflake guildId) const;
    [[nodiscard]] QList<Snowflake> stickerGuilds() const;
    // The emoji and its server
    [[nodiscard]] std::optional<std::pair<Discord::Emoji, Snowflake>> customEmojiById(Snowflake emojiId) const;

    // write back to settings-proto/2
    void savePendingUsage();

public slots:
    void onReady();
    void onGuildEmojisUpdated(const Discord::GuildEmojisUpdate &event);
    void onUserSettingsProtoUpdated(const Discord::UserSettingsProtoUpdate &event);

private:
    struct CustomEmoji
    {
        Discord::Emoji emoji;
        Snowflake guildId;
        QString originalName;
        // `name~1`, `name~2`
        QString disambiguatedName;
        QString normalizedName;
    };

    struct UnicodeCandidate
    {
        const UnicodeEmoji *emoji = nullptr;
        QStringList normalizedNames;
    };

    struct PendingUsage
    {
        QString key;
        uint64_t timestamp = 0;
        bool reaction = false;
    };

    // for querying emoji usability
    struct UsabilityContext
    {
        Snowflake channelId;
        Snowflake channelGuildId;
        Snowflake selfId;
        bool premium = false;
        bool bypass = false; // Qord: FakeNitro, for messages
        QHash<Snowflake, QSet<Snowflake>> rolesByGuild;
    };

    void fetchSettings();
    void adoptSettings(const Proto::FrecencyUserSettings &settings, bool partial);
    void replayPendingUsages();
    void seedDefaultsIfEmpty();

    void trackUsage(const QString &key, bool reaction);
    void trackSurrogates(const QString &content);

    void onSyncTimer();

    const QList<CustomEmoji> &customEmojis();
    const QList<UnicodeCandidate> &unicodeCandidates();
    bool isUsable(const CustomEmoji &custom, UsabilityContext &context) const;

    [[nodiscard]] static uint64_t nowMs();
    [[nodiscard]] static QString unicodeKey(const QString &surrogates);

    Discord::Client *client;
    ClientInstance *instance;

    QHash<Snowflake /*guildId*/, QList<Discord::Emoji>> guildEmojis;
    QHash<Snowflake /*guildId*/, QList<Discord::Sticker>> guildStickers; // Qord
    QList<CustomEmoji> customCandidates;
    bool customCandidatesDirty = true;
    QList<UnicodeCandidate> unicodeCandidateCache;

    QStringList favorites_;
    FrecencyTracker chatTracker;
    FrecencyTracker reactionTracker;

    QList<PendingUsage> pendingUsages;
    std::optional<uint32_t> dataVersion;
    bool loaded = false;
    bool saveInFlight = false;

    QTimer syncTimer;
};

} // namespace Core
} // namespace Acheron
