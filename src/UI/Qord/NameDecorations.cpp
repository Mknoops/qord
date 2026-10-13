#include "NameDecorations.hpp"

#include "Core/ClientInstance.hpp"
#include "Core/Presence/PresenceManager.hpp"
#ifndef ACHERON_NO_VOICE
#include "Core/Audio/VoiceManager.hpp"
#endif
#include "Core/Session.hpp"
#include "Core/Theme/Manager.hpp"
#include "Core/UserManager.hpp"
#include "Plugins/PluginHost.hpp"
#include "Plugins/Client/MessageContexts.hpp"

#include <QPainter>
#include <QPointer>
#include <QSvgRenderer>

namespace Acheron {
namespace UI {

namespace {
QPointer<NameDecorations> current;

Qord::Plugins::DecorationContext::Place pluginPlace(NameDecorations::Place place)
{
    switch (place) {
    case NameDecorations::Place::MemberList:
        return Qord::Plugins::DecorationContext::Place::MemberList;
    case NameDecorations::Place::Profile:
        return Qord::Plugins::DecorationContext::Place::Profile;
    case NameDecorations::Place::Message:
        break;
    }
    return Qord::Plugins::DecorationContext::Place::Message;
}

// "#rrggbb", "status:<status>" for the theme's status colour, or "icon"
QColor resolveColor(const QString &color)
{
    using Core::Theme::Token;
    if (color == QLatin1String("icon"))
        return Core::Theme::Manager::instance().color(Token::MutedText);
    if (!color.startsWith(QLatin1String("status:")))
        return QColor(color);
    const QString status = color.mid(7);
    const auto &theme = Core::Theme::Manager::instance();
    if (status == QLatin1String("online"))
        return theme.color(Token::StatusOnline);
    if (status == QLatin1String("idle"))
        return theme.color(Token::StatusIdle);
    if (status == QLatin1String("dnd"))
        return theme.color(Token::StatusDnd);
    if (status == QLatin1String("streaming"))
        return theme.color(Token::StatusStreaming);
    return theme.color(Token::StatusOffline);
}
} // namespace

NameDecorations::NameDecorations(Core::Session *session, QObject *parent)
    : QObject(parent), session(session)
{
    current = this;
    if (auto *host = Qord::Plugins::PluginHost::instance()) {
        connect(host, &Qord::Plugins::PluginHost::presentationChanged, this, &NameDecorations::invalidate);
        connect(host, &Qord::Plugins::PluginHost::pluginStateChanged, this, &NameDecorations::invalidate);
    }
    connect(&Core::Theme::Manager::instance(), &Core::Theme::Manager::themeChanged, this, [this]() {
        pixmaps.clear();
        invalidate();
    });
}

NameDecorations::~NameDecorations() = default;

NameDecorations *NameDecorations::instance()
{
    auto *host = Qord::Plugins::PluginHost::instance();
    if (!host || !host->hasHook(Qord::Plugins::Hook::UserDecorations))
        return nullptr;
    return current.data();
}

void NameDecorations::watch(Core::ClientInstance *instance)
{
    if (!instance || watched.contains(quint64(instance->accountId())))
        return;
    const quint64 accountId = quint64(instance->accountId());
    watched.insert(accountId);
    connect(instance->presences(), &Core::PresenceManager::presencesChanged, this,
            [this, accountId](const QList<Core::Snowflake> &userIds) { dropUsers(accountId, userIds); });
#ifndef ACHERON_NO_VOICE
    // UserVoiceShow: icons follow voice states
    if (auto *voice = instance->voice()) {
        // (the tooltip lists who else is there)
        connect(voice, &Core::Audio::VoiceManager::channelVoiceMemberChanged, this,
                [this, accountId, voice](Core::Snowflake channelId, Core::Snowflake userId, bool) {
                    dropUsers(accountId, voice->channelVoiceUsers(channelId) << userId);
                });
        connect(voice, &Core::Audio::VoiceManager::participantVoiceStateChanged, this,
                [this, accountId](Core::Snowflake, Core::Snowflake userId) { dropUsers(accountId, { userId }); });
        connect(voice, &Core::Audio::VoiceManager::voiceStateChanged, this,
                [this, accountId, instance]() { dropUsers(accountId, { instance->accountId() }); });
    }
#endif
}

void NameDecorations::dropUsers(quint64 accountId, const QList<Core::Snowflake> &userIds)
{
    if (cache.isEmpty())
        return;
    bool dropped = false;
    const QString accountPrefix = QString::number(accountId) + QLatin1Char('/');
    for (auto it = cache.begin(); it != cache.end();) {
        const QStringList parts = it.key().split(QLatin1Char('/'));
        if (it.key().startsWith(accountPrefix) && parts.size() == 4 &&
            userIds.contains(Core::Snowflake(parts[2].toULongLong()))) {
            it = cache.erase(it);
            dropped = true;
        } else {
            ++it;
        }
    }
    if (dropped)
        emit changed();
}

void NameDecorations::invalidate()
{
    cache.clear();
    emit changed();
}

QPixmap NameDecorations::render(const QByteArray &svg, const QString &color, QSize size, qreal dpr)
{
    const QColor resolved = resolveColor(color);
    const QString key = QStringLiteral("%1|%2|%3x%4@%5")
                                .arg(QString::number(qHash(svg)), resolved.name(QColor::HexArgb))
                                .arg(size.width())
                                .arg(size.height())
                                .arg(dpr);
    auto it = pixmaps.constFind(key);
    if (it != pixmaps.constEnd())
        return it.value();

    QByteArray document = svg;
    document.replace("currentColor", resolved.name().toLatin1());
    QSvgRenderer renderer(document);
    QPixmap pixmap(size * dpr);
    pixmap.fill(Qt::transparent);
    if (renderer.isValid()) {
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        // Like a browser's default preserveAspectRatio: fit the viewBox, centred
        QSizeF box = renderer.viewBoxF().size();
        const QRectF area(QPointF(0, 0), QSizeF(pixmap.size()));
        if (box.isEmpty())
            box = area.size();
        box.scale(area.size(), Qt::KeepAspectRatio);
        renderer.render(&p, QRectF(area.center() - QPointF(box.width() / 2, box.height() / 2), box));
    }
    pixmap.setDevicePixelRatio(dpr);
    if (pixmaps.size() > 256)
        pixmaps.clear();
    pixmaps.insert(key, pixmap);
    return pixmap;
}

QList<NameDecorations::Decoration> NameDecorations::forUser(Core::Snowflake accountId, Core::Snowflake guildId,
                                                            Core::Snowflake userId, Place place, qreal dpr)
{
    auto *host = Qord::Plugins::PluginHost::instance();
    if (!host || !host->hasHook(Qord::Plugins::Hook::UserDecorations) || !userId.isValid())
        return {};
    const QString key = QStringLiteral("%1/%2/%3/%4")
                                .arg(quint64(accountId))
                                .arg(quint64(guildId))
                                .arg(quint64(userId))
                                .arg(int(place));
    auto cached = cache.constFind(key);
    if (cached != cache.constEnd())
        return cached.value();

    QList<Decoration> out;
    Qord::Plugins::DecorationContext ctx;
    if (context(accountId, guildId, userId, place, ctx)) {
        for (const auto &d : host->dispatchNameDecorations(ctx)) {
            const QSize size(qBound(1, d.width, 64), qBound(1, d.height, 64));
            out.append({ render(d.svg, d.color, size, dpr), size, d.tooltip, d.pluginId, d.clickId });
        }
    }
    if (cache.size() > 4096)
        cache.clear();
    cache.insert(key, out);
    return out;
}

bool NameDecorations::context(Core::Snowflake accountId, Core::Snowflake guildId, Core::Snowflake userId, Place place,
                              Qord::Plugins::DecorationContext &ctx) const
{
    Core::ClientInstance *instance = session ? session->client(accountId) : nullptr;
    if (!instance)
        return false;
    ctx.accountId = quint64(accountId);
    ctx.guildId = Qord::Plugins::pluginId(guildId);
    ctx.userId = quint64(userId);
    ctx.place = pluginPlace(place);
    ctx.isSelf = userId == instance->accountId();
    if (const auto user = instance->users()->getUser(userId))
        ctx.isBot = user->bot.hasValue() && user->bot.get();
    ctx.clientStatus = instance->presences()->clientPlatforms(userId, guildId);
    if (ctx.isSelf)
        for (const auto &s : instance->presences()->ownSessions())
            ctx.ownSessions.append({ s.client, s.status.hasValue() ? s.status.get() : QString() });
    return true;
}

void NameDecorations::click(Core::Snowflake accountId, Core::Snowflake guildId, Core::Snowflake userId, Place place,
                            const Decoration &decoration, bool doubleClick)
{
    auto *host = Qord::Plugins::PluginHost::instance();
    Qord::Plugins::DecorationContext ctx;
    if (!host || !decoration.clickable() || !context(accountId, guildId, userId, place, ctx))
        return;
    host->dispatchNameDecorationClicked(decoration.pluginId, decoration.clickId, ctx, doubleClick);
}

int NameDecorations::width(const QList<Decoration> &decorations)
{
    int w = 0;
    for (const auto &d : decorations)
        w += Spacing + d.size.width();
    return w;
}

QList<QRect> NameDecorations::paint(QPainter &painter, const QList<Decoration> &decorations, int x, int centerY)
{
    QList<QRect> rects;
    for (const auto &d : decorations) {
        x += Spacing;
        const QRect rect(x, centerY - d.size.height() / 2, d.size.width(), d.size.height());
        painter.drawPixmap(rect, d.pixmap);
        rects.append(rect);
        x += d.size.width();
    }
    return rects;
}

} // namespace UI
} // namespace Acheron
