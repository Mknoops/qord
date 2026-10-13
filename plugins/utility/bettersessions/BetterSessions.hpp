/*
 * Qord, a native Discord client
 * Port of Equicord's BetterSessions plugin (src/plugins/betterSessions)
 * Original: Copyright (c) Vendicated and contributors, author amia
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "Plugins/Plugin.hpp"

#include <QHash>
#include <QJsonObject>

#include <memory>

class QTimer;

namespace Qord {
namespace Plugins {

class PluginContext;

// Settings → Devices with names, exact times and NEW badges, and a
// notification when a device logs in to your account.
class BetterSessionsPlugin : public Plugin
{
public:
    struct Saved
    {
        QString name;
        bool isNew = false;
    };

    // getDefaultName, GetOsColor and GetPlatformIcon
    static QString defaultName(const DeviceSession &session);
    static QString osColor(const QString &os);
    static QByteArray platformIconSvg(const QString &platform);

    BetterSessionsPlugin();
    ~BetterSessionsPlugin() override;

    [[nodiscard]] Hooks hooks() const override { return Hook::DeviceSessions; }
    void start(PluginContext &context) override;
    void stop() override;

    std::optional<DeviceSessionView> deviceSessionView(Id accountId, const DeviceSession &session) override;
    void onDeviceSessionButton(Id accountId, const QString &buttonId, const DeviceSession &session) override;
    void onDevicesPageClosed(Id accountId, const QList<DeviceSession> &sessions) override;
    void onDeviceSessionsChanged(Id accountId) override;

    void checkNewSessions(Id accountId);

private:
    void load();
    void save();

    PluginContext *context = nullptr;
    QHash<Id, QHash<QString, Saved>> saved; // by account, then id_hash
    QHash<Id, qint64> lastCheckMs;
    QHash<Id, QTimer *> pendingChecks;
    std::shared_ptr<bool> alive;
};

} // namespace Plugins
} // namespace Qord
