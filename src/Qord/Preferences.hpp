#pragma once

// Qord's app preferences: one place that knows every setting's key and
// default, reads and writes them in QSettings, and tells the rest of the
// client when one changes, so a toggle in Settings applies at once.

#include <QObject>
#include <QString>
#include <QTime>
#include <QVariant>

namespace Qord {

namespace Pref {
// Chat
inline constexpr const char *AnimateEmoji = "chat/animate_emoji";
inline constexpr const char *AnimateStickers = "chat/animate_stickers";
inline constexpr const char *ShowEmbeds = "chat/show_embeds";
inline constexpr const char *ShowLinkMedia = "chat/show_link_media";
inline constexpr const char *ShowAttachmentMedia = "chat/show_attachment_media";
inline constexpr const char *ShowReactions = "chat/show_reactions";
inline constexpr const char *ConvertEmoticons = "chat/convert_emoticons";
inline constexpr const char *RevealSpoilers = "chat/reveal_spoilers";
inline constexpr const char *TimeFormat = "chat/time_format"; // "system", "12" or "24"
inline constexpr const char *CtrlEnterSends = "chat/ctrl_enter_sends";
inline constexpr const char *GroupSpacing = "chat/group_spacing"; // pixels between message groups
inline constexpr const char *HighlightMentions = "chat/highlight_mentions";
// Accessibility
inline constexpr const char *Zoom = "a11y/zoom"; // percent, applied at start
inline constexpr const char *ReduceMotion = "a11y/reduce_motion";
inline constexpr const char *UnderlineLinks = "a11y/underline_links";
// Notifications
inline constexpr const char *NotifyDesktop = "notifications/desktop";
inline constexpr const char *NotifySound = "notifications/sound";
inline constexpr const char *NotifyDms = "notifications/dms";
inline constexpr const char *NotifyMentions = "notifications/mentions";
inline constexpr const char *NotifyShowContent = "notifications/show_content";
inline constexpr const char *NotifyFlash = "notifications/flash";
inline constexpr const char *NotifyVolume = "notifications/volume"; // percent
// Window
inline constexpr const char *StartMinimized = "window/start_minimized";
inline constexpr const char *MinimizeToTray = "window/minimize_to_tray";
inline constexpr const char *CloseToTray = "window/close_to_tray";
inline constexpr const char *ShowMenuBar = "ui/showMenuBar";
// Streamer mode
inline constexpr const char *Streamer = "streamer/enabled";
inline constexpr const char *StreamerHidePersonal = "streamer/hide_personal";
inline constexpr const char *StreamerHideInvites = "streamer/hide_invites";
inline constexpr const char *StreamerNoNotifications = "streamer/no_notifications";
inline constexpr const char *StreamerNoSounds = "streamer/no_sounds";
// Advanced
inline constexpr const char *DeveloperMode = "advanced/developer_mode";
// Voice
inline constexpr const char *NoiseSuppression = "voice/noise_suppression";
inline constexpr const char *RnnoiseVad = "voice/rnnoise_vad";
} // namespace Pref

class Preferences : public QObject
{
    Q_OBJECT
public:
    static Preferences &instance();

    [[nodiscard]] static QVariant defaultValue(const QString &key);
    [[nodiscard]] static QVariant value(const QString &key);
    [[nodiscard]] static bool flag(const QString &key) { return value(key).toBool(); }
    [[nodiscard]] static int number(const QString &key) { return value(key).toInt(); }
    [[nodiscard]] static QString text(const QString &key) { return value(key).toString(); }
    // Writes the setting and emits changed() when it differs
    static void set(const QString &key, const QVariant &value);
    // Forgets the values read so far, after QSettings was written behind its back
    static void reload();

    // Streamer mode is on and the given part of it (Pref::StreamerHide...) too
    [[nodiscard]] static bool streamerHides(const QString &key);
    [[nodiscard]] static bool developerMode() { return flag(Pref::DeveloperMode); }

    // A time of day in the chosen clock (Pref::TimeFormat)
    [[nodiscard]] static QString timeText(const QTime &time);
    // Animations play when allowed and Reduce motion is off
    [[nodiscard]] static bool animateEmoji() { return flag(Pref::AnimateEmoji) && !flag(Pref::ReduceMotion); }
    [[nodiscard]] static bool animateStickers() { return flag(Pref::AnimateStickers) && !flag(Pref::ReduceMotion); }

    // Turns typed emoticons such as :) and <3 into emoji, outside code
    [[nodiscard]] static QString convertEmoticons(const QString &text);

signals:
    void changed(const QString &key);

private:
    Preferences() = default;
};

} // namespace Qord
