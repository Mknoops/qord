/*
 * Qord, a native Discord client
 * Port of Equicord's BetterSessions plugin (src/plugins/betterSessions)
 * Original: Copyright (c) Vendicated and contributors, author amia
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "BetterSessions.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QDateTime>
#include <QJsonObject>
#include <QLocale>
#include <QTimer>

namespace Qord {
namespace Plugins {

namespace {
const QString SavedKey = QStringLiteral("savedSessions");
const QString RenameId = QStringLiteral("rename");
} // namespace

QString BetterSessionsPlugin::defaultName(const DeviceSession &session)
{
    return session.os + QStringLiteral(" · ") + session.platform;
}

QString BetterSessionsPlugin::osColor(const QString &os)
{
    if (os == QLatin1String("Windows Mobile") || os == QLatin1String("Windows"))
        return QStringLiteral("#55a6ef"); // Light blue
    if (os == QLatin1String("Linux"))
        return QStringLiteral("#cdcd31"); // Yellow
    if (os == QLatin1String("Android"))
        return QStringLiteral("#7bc958"); // Green
    if (os == QLatin1String("Mac OS X") || os == QLatin1String("iOS"))
        return {}; // The theme's
    if (os == QLatin1String("Horizon OS"))
        return QStringLiteral("#0081fb"); // Meta Quest
    return QStringLiteral("#f3799a"); // Pink
}

BetterSessionsPlugin::BetterSessionsPlugin() = default;
BetterSessionsPlugin::~BetterSessionsPlugin() = default;

void BetterSessionsPlugin::start(PluginContext &ctx)
{
    context = &ctx;
    alive = std::make_shared<bool>(true);
    load();
    // Upstream checks once when it starts
    if (SessionService *sessions = context->sessions())
        for (const Id accountId : sessions->accountIds())
            checkNewSessions(accountId);
}

void BetterSessionsPlugin::stop()
{
    qDeleteAll(pendingChecks);
    pendingChecks.clear();
    lastCheckMs.clear();
    saved.clear();
    alive.reset();
    context = nullptr;
}

void BetterSessionsPlugin::load()
{
    saved.clear();
    const QJsonObject all = context->settings().value(SavedKey).toObject();
    for (auto account = all.begin(); account != all.end(); ++account) {
        const QJsonObject sessions = account.value().toObject();
        auto &out = saved[account.key().toULongLong()];
        for (auto it = sessions.begin(); it != sessions.end(); ++it) {
            const QJsonObject s = it.value().toObject();
            out.insert(it.key(), { s.value(QStringLiteral("name")).toString(), s.value(QStringLiteral("isNew")).toBool() });
        }
    }
}

void BetterSessionsPlugin::save()
{
    QJsonObject all;
    for (auto account = saved.cbegin(); account != saved.cend(); ++account) {
        QJsonObject sessions;
        for (auto it = account->cbegin(); it != account->cend(); ++it)
            sessions.insert(it.key(), QJsonObject{ { QStringLiteral("name"), it->name },
                                                   { QStringLiteral("isNew"), it->isNew } });
        all.insert(QString::number(account.key()), sessions);
    }
    context->settings().setValue(SavedKey, all);
}

void BetterSessionsPlugin::checkNewSessions(Id accountId)
{
    SessionService *sessions = context ? context->sessions() : nullptr;
    if (!sessions)
        return;
    lastCheckMs.insert(accountId, QDateTime::currentMSecsSinceEpoch());
    std::weak_ptr<bool> weak = alive;
    sessions->fetchSessions(accountId, [this, weak, accountId](bool ok, const QList<DeviceSession> &list) {
        if (weak.expired() || !ok)
            return;
        auto &known = saved[accountId];
        for (const DeviceSession &session : list) {
            if (known.contains(session.idHash))
                continue;
            known.insert(session.idHash, { QString(), true });
            if (UiService *ui = context->ui()) {
                PluginNotification n;
                n.title = QStringLiteral("BetterSessions");
                n.body = QStringLiteral("New session:\n%1 · %2 · %3").arg(session.os, session.platform, session.location);
                n.permanent = true;
                ui->showNotification(n, [this, weak]() {
                    if (!weak.expired())
                        if (UiService *ui = context->ui())
                            ui->openDevicesSettings();
                });
            }
        }
        save();
        context->sessions()->refreshDevicesPage();
    });
}

void BetterSessionsPlugin::onDeviceSessionsChanged(Id accountId)
{
    if (!context)
        return;
    // An account that connected after the plugin started gets upstream's
    // start-up check
    if (!lastCheckMs.contains(accountId)) {
        checkNewSessions(accountId);
        return;
    }
    if (!context->settings().boolValue(QStringLiteral("backgroundCheck")))
        return;
    // At most once per interval: a change inside it is checked when it ends
    const qint64 intervalMs = qint64(context->settings().numberValue(QStringLiteral("checkInterval")) * 60 * 1000);
    const qint64 sinceMs = QDateTime::currentMSecsSinceEpoch() - lastCheckMs.value(accountId, 0);
    if (sinceMs >= intervalMs) {
        checkNewSessions(accountId);
        return;
    }
    if (pendingChecks.contains(accountId))
        return;
    auto *timer = new QTimer;
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, [this, accountId, timer]() {
        pendingChecks.remove(accountId);
        timer->deleteLater();
        checkNewSessions(accountId);
    });
    pendingChecks.insert(accountId, timer);
    timer->start(int(qMin<qint64>(intervalMs - sinceMs, std::numeric_limits<int>::max())));
}

std::optional<DeviceSessionView> BetterSessionsPlugin::deviceSessionView(Id accountId, const DeviceSession &session)
{
    if (!context)
        return std::nullopt;
    const auto known = saved.value(accountId);
    const auto it = known.constFind(session.idHash);
    DeviceSessionView view;
    view.title = it != known.constEnd() && !it->name.isEmpty() ? it->name + QLatin1Char('*') : defaultName(session);
    // A "NEW" badge the first time a session is seen
    view.isNew = it == known.constEnd() || it->isNew;
    view.iconColor = osColor(session.os);
    view.badgeSvg = platformIconSvg(session.platform);
    if (session.lastUsedMs > 0)
        view.timeTooltip = QLocale().toString(QDateTime::fromMSecsSinceEpoch(session.lastUsedMs), QLocale::ShortFormat);
    if (context->ui())
        view.buttons.append({ RenameId, QStringLiteral("Rename") });
    return view;
}

void BetterSessionsPlugin::onDeviceSessionButton(Id accountId, const QString &buttonId, const DeviceSession &session)
{
    UiService *ui = context ? context->ui() : nullptr;
    if (buttonId != RenameId || !ui)
        return;
    std::weak_ptr<bool> weak = alive;
    const QString current = saved.value(accountId).value(session.idHash).name;
    ui->promptText(QStringLiteral("Rename"), QStringLiteral("New device name"), current, 0,
                   [this, weak, accountId, hash = session.idHash](const QString &name) {
                       if (weak.expired())
                           return;
                       saved[accountId].insert(hash, { name.trimmed(), false });
                       save();
                       context->sessions()->refreshDevicesPage();
                   });
}

void BetterSessionsPlugin::onDevicesPageClosed(Id accountId, const QList<DeviceSession> &sessions)
{
    if (!context)
        return;
    auto &known = saved[accountId];
    QSet<QString> shown;
    for (const DeviceSession &session : sessions) {
        shown.insert(session.idHash);
        if (!known.contains(session.idHash))
            known.insert(session.idHash, { QString(), false });
    }
    // Forget devices that logged out
    if (!shown.isEmpty())
        for (auto it = known.begin(); it != known.end();)
            it = shown.contains(it.key()) ? std::next(it) : known.erase(it);
    // Leaving the page means its devices have been seen
    for (auto &s : known)
        s.isNew = false;
    save();
}

QByteArray qord_manifest_bettersessions();

void qord_register_bettersessions(Registry &registry)
{
    registry.add({ QStringLiteral("bettersessions/plugin.json"), [] { return std::make_unique<BetterSessionsPlugin>(); },
                   qord_manifest_bettersessions() });
}

} // namespace Plugins
} // namespace Qord
