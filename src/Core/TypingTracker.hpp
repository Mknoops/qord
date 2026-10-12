#pragma once

#include <QObject>
#include <QTimer>
#include <QHash>
#include <QDateTime>
#include <optional>

#include "Snowflake.hpp"

namespace Acheron {
namespace Core {

class UserManager;

struct TyperInfo
{
    Snowflake userId;
    std::optional<Snowflake> guildId;
    QString name;
};

class TypingTracker : public QObject
{
    Q_OBJECT
public:
    explicit TypingTracker(QObject *parent = nullptr);

    void setUserManager(UserManager *manager);
    void setActiveChannel(Snowflake channelId);
    void setCurrentUserId(Snowflake userId);

    void addTyper(Snowflake channelId, Snowflake userId, std::optional<Snowflake> guildId);
    void removeTyper(Snowflake channelId, Snowflake userId);
    void clear();

    QList<TyperInfo> getActiveTypers() const;
    // Qord: who is typing in any channel (TypingIndicator), oldest first
    [[nodiscard]] QList<Snowflake> typers(Snowflake channelId) const;
    [[nodiscard]] bool anyTypers() const { return !channelTypers.isEmpty(); }

signals:
    void typersChanged();
    // Qord: a channel's typers changed (Invalid: every channel's)
    void channelTypersChanged(Snowflake channelId);

private slots:
    void cleanupExpired();

private:
    struct TypingEntry
    {
        Snowflake userId;
        std::optional<Snowflake> guildId;
        QDateTime expiresAt;
    };

    QHash<Snowflake, QList<TypingEntry>> channelTypers;
    Snowflake activeChannel;
    Snowflake currentUserId;
    UserManager *userManager = nullptr;
    QTimer cleanupTimer;
};

} // namespace Core
} // namespace Acheron
