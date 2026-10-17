#include "Preferences.hpp"

#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>

namespace Qord {

namespace {
const QHash<QString, QVariant> &defaults()
{
    static const QHash<QString, QVariant> table = {
        { Pref::AnimateEmoji, true },
        { Pref::AnimateStickers, true },
        { Pref::ShowEmbeds, true },
        { Pref::ShowLinkMedia, true },
        { Pref::ShowAttachmentMedia, true },
        { Pref::ShowReactions, true },
        { Pref::ConvertEmoticons, false },
        { Pref::RevealSpoilers, false },
        { Pref::TimeFormat, QStringLiteral("system") },
        { Pref::CtrlEnterSends, false },
        { Pref::GroupSpacing, 16 },
        { Pref::HighlightMentions, true },
        { Pref::Zoom, 100 },
        { Pref::ReduceMotion, false },
        { Pref::UnderlineLinks, false },
        { Pref::NotifyDesktop, true },
        { Pref::NotifySound, true },
        { Pref::NotifyDms, true },
        { Pref::NotifyMentions, true },
        { Pref::NotifyShowContent, true },
        { Pref::NotifyFlash, true },
        { Pref::NotifyVolume, 100 },
        { Pref::StartMinimized, false },
        { Pref::MinimizeToTray, false },
        { Pref::CloseToTray, false },
        { Pref::ShowMenuBar, false },
        { Pref::Streamer, false },
        { Pref::StreamerHidePersonal, true },
        { Pref::StreamerHideInvites, true },
        { Pref::StreamerNoNotifications, true },
        { Pref::StreamerNoSounds, true },
        { Pref::DeveloperMode, true },
        { Pref::NoiseSuppression, true },
        { Pref::RnnoiseVad, true },
    };
    return table;
}
} // namespace

Preferences &Preferences::instance()
{
    static Preferences preferences;
    return preferences;
}

QVariant Preferences::defaultValue(const QString &key)
{
    return defaults().value(key);
}

// Read on every chat repaint, so kept in memory; only the GUI thread uses it
static QHash<QString, QVariant> &cache()
{
    static QHash<QString, QVariant> values;
    return values;
}

QVariant Preferences::value(const QString &key)
{
    if (const auto it = cache().constFind(key); it != cache().constEnd())
        return *it;
    const QVariant fallback = defaultValue(key);
    QVariant stored = QSettings().value(key, fallback);
    // QSettings' INI files give strings back; keep the default's type
    switch (fallback.userType()) {
    case QMetaType::Bool:
        stored = stored.toBool();
        break;
    case QMetaType::Int:
        stored = stored.toInt();
        break;
    case QMetaType::QString:
        stored = stored.toString();
        break;
    default:
        break;
    }
    cache().insert(key, stored);
    return stored;
}

void Preferences::reload()
{
    cache().clear();
}

void Preferences::set(const QString &key, const QVariant &newValue)
{
    if (value(key) == newValue)
        return;
    QSettings().setValue(key, newValue);
    cache().remove(key);
    emit instance().changed(key);
}

bool Preferences::streamerHides(const QString &key)
{
    return flag(Pref::Streamer) && flag(key);
}

QString Preferences::timeText(const QTime &time)
{
    const QString format = text(Pref::TimeFormat);
    if (format == QLatin1String("12"))
        return QLocale::c().toString(time, QStringLiteral("h:mm AP"));
    if (format == QLatin1String("24"))
        return time.toString(QStringLiteral("HH:mm"));
    return QLocale().toString(time, QLocale::ShortFormat);
}

QString Preferences::convertEmoticons(const QString &text)
{
    // Discord's list, matched only as whole words so "http://" and "a:b" stay
    static const QList<QPair<QString, QString>> emoticons = {
        { QStringLiteral(":')"), QStringLiteral("\U0001F972") }, { QStringLiteral(":'("), QStringLiteral("\U0001F622") },
        { QStringLiteral(">:("), QStringLiteral("\U0001F620") }, { QStringLiteral(":)"), QStringLiteral("\U0001F642") },
        { QStringLiteral(":-)"), QStringLiteral("\U0001F642") }, { QStringLiteral("(:"), QStringLiteral("\U0001F642") },
        { QStringLiteral(":("), QStringLiteral("\U0001F641") },  { QStringLiteral(":-("), QStringLiteral("\U0001F641") },
        { QStringLiteral(":D"), QStringLiteral("\U0001F603") },  { QStringLiteral(":-D"), QStringLiteral("\U0001F603") },
        { QStringLiteral(";)"), QStringLiteral("\U0001F609") },  { QStringLiteral(";-)"), QStringLiteral("\U0001F609") },
        { QStringLiteral(":P"), QStringLiteral("\U0001F61B") },  { QStringLiteral(":p"), QStringLiteral("\U0001F61B") },
        { QStringLiteral(":O"), QStringLiteral("\U0001F62E") },  { QStringLiteral(":o"), QStringLiteral("\U0001F62E") },
        { QStringLiteral(":|"), QStringLiteral("\U0001F610") },  { QStringLiteral(":/"), QStringLiteral("\U0001F615") },
        { QStringLiteral("<3"), QStringLiteral("❤️") }, { QStringLiteral("</3"), QStringLiteral("\U0001F494") },
        { QStringLiteral("B)"), QStringLiteral("\U0001F60E") },  { QStringLiteral("xD"), QStringLiteral("\U0001F606") },
        { QStringLiteral("XD"), QStringLiteral("\U0001F606") },
    };
    static const QHash<QString, QString> lookup = [] {
        QHash<QString, QString> map;
        for (const auto &[from, to] : emoticons)
            map.insert(from, to);
        return map;
    }();

    QString out;
    out.reserve(text.size());
    bool inBlock = false; // ``` fenced
    bool inInline = false; // `code`
    int i = 0;
    while (i < text.size()) {
        if (text.mid(i, 3) == QStringLiteral("```")) {
            inBlock = !inBlock;
            out += QStringLiteral("```");
            i += 3;
            continue;
        }
        if (!inBlock && text.at(i) == QLatin1Char('`')) {
            inInline = !inInline;
            out += text.at(i++);
            continue;
        }
        if (inBlock || inInline || text.at(i).isSpace()) {
            out += text.at(i++);
            continue;
        }
        int end = i;
        while (end < text.size() && !text.at(end).isSpace() && text.at(end) != QLatin1Char('`'))
            ++end;
        const QString word = text.mid(i, end - i);
        out += lookup.value(word, word);
        i = end;
    }
    return out;
}

} // namespace Qord
