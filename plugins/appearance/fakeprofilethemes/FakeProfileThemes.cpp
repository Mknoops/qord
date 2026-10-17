/*
 * Qord, a native Discord client
 * Port of Equicord's FakeProfileThemes plugin (src/plugins/fakeProfileThemes)
 * Original: Copyright (c) 2023 Vendicated and contributors, authors Alyxia, Remty
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "FakeProfileThemes.hpp"

#include "Plugins/Plugin.hpp"
#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"
#include "Plugins/Services.hpp"

#include <QDateTime>
#include <QRegularExpression>

namespace Qord {
namespace Plugins {
namespace FakeProfileThemes {

// encode(): "[#rrggbb,#rrggbb]" as Unicode tag characters (invisible), after a space
QString encode(quint32 primary, quint32 accent)
{
    const QString message = QStringLiteral("[#%1,#%2]")
                                    .arg(primary & 0xFFFFFF, 6, 16, QLatin1Char('0'))
                                    .arg(accent & 0xFFFFFF, 6, 16, QLatin1Char('0'));
    QString out = QStringLiteral(" ");
    for (const QChar ch : message) {
        const char32_t code = ch.unicode();
        if (code < 0x20 || code > 0x7F)
            continue;
        const char32_t tag = code + 0xE0000;
        out += QString::fromUcs4(&tag, 1);
    }
    return out;
}

// decode(): the tag characters back to ASCII, then the first "[#hex,#hex]"
std::optional<std::pair<quint32, quint32>> decode(const QString &bio)
{
    QString ascii;
    for (const char32_t code : bio.toUcs4())
        if (code >= 0xE0000 && code <= 0xE007F)
            ascii += QChar(char16_t(code - 0xE0000));
    static const QRegularExpression pattern(QStringLiteral(R"(\[#([a-fA-F0-9]{1,6}),#([a-fA-F0-9]{1,6})\])"));
    const QRegularExpressionMatch m = pattern.match(ascii);
    if (!m.hasMatch())
        return std::nullopt;
    return std::make_pair(m.captured(1).toUInt(nullptr, 16), m.captured(2).toUInt(nullptr, 16));
}

} // namespace FakeProfileThemes

namespace {

using namespace FakeProfileThemes;

class FakeProfileThemesPlugin : public Plugin
{
public:
    [[nodiscard]] Hooks hooks() const override { return Hook::ProfileTheme; }

    void start(PluginContext &ctx) override { context = &ctx; }
    void stop() override { context = nullptr; }

    // colorDecodeHook(): a theme in the bio, unless the profile has a Nitro
    // one and Nitro colours come first
    std::optional<ProfileTheme> profileTheme(const ProfileThemeContext &profile) override
    {
        if (!context)
            return std::nullopt;
        // Qord: your own profile, shown with the colours being picked
        if (profile.userId == profile.accountId && QDateTime::currentMSecsSinceEpoch() < previewUntilMs)
            return ProfileTheme { pickedColor(QStringLiteral("primaryColor")), pickedColor(QStringLiteral("accentColor")) };
        if (profile.bio.isEmpty())
            return std::nullopt;
        const bool nitroFirst = context->settings().stringValue(QStringLiteral("nitroFirst")) != QLatin1String("fake");
        if (nitroFirst && profile.primary && profile.accent)
            return std::nullopt;
        if (const auto colors = decode(profile.bio))
            return ProfileTheme { colors->first, colors->second };
        return std::nullopt;
    }

    void onSettingAction(const QString &key) override
    {
        if (!context)
            return;
        UiService *ui = context->ui();
        if (key == QLatin1String("copy3y3")) {
            // The settings page's "Copy 3y3": to paste anywhere in your bio
            ClipboardService *clipboard = context->clipboard();
            if (!clipboard)
                return;
            clipboard->setText(encode(pickedColor(QStringLiteral("primaryColor")), pickedColor(QStringLiteral("accentColor"))));
            if (ui)
                ui->showToast(QStringLiteral("Copied to clipboard!"), UiService::ToastType::Success);
        } else if (key == QLatin1String("preview") && ui && ownId()) {
            // Upstream's preview: your profile with these colours, for a minute
            previewUntilMs = QDateTime::currentMSecsSinceEpoch() + 60'000;
            ui->openUserProfile(ownId(), 0, ownId());
        }
    }

private:
    [[nodiscard]] quint32 pickedColor(const QString &key) const
    {
        // "#rrggbb", as the colour setting holds it
        return context->settings().stringValue(key).mid(1).toUInt(nullptr, 16) & 0xFFFFFF;
    }

    [[nodiscard]] Id ownId() const
    {
        UiService *ui = context ? context->ui() : nullptr;
        return ui ? ui->currentAccountId() : 0;
    }

    PluginContext *context = nullptr;
    qint64 previewUntilMs = 0;
};

} // namespace

QByteArray qord_manifest_fakeprofilethemes();

void qord_register_fakeprofilethemes(Registry &registry)
{
    registry.add({ QStringLiteral("fakeprofilethemes/plugin.json"),
                   [] { return std::make_unique<FakeProfileThemesPlugin>(); }, qord_manifest_fakeprofilethemes() });
}

} // namespace Plugins
} // namespace Qord
