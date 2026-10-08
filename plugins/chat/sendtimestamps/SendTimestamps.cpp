/*
 * Qord, a native Discord client
 * Port of Equicord's SendTimestamps plugin (src/plugins/sendTimestamps)
 * Original: Copyright (c) Vendicated and contributors, authors V, \GGTyler\, Grzesiek11
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "SendTimestamps.hpp"

#include "Plugins/PluginContext.hpp"
#include "Plugins/Registry.hpp"

#include <QRegularExpression>

namespace Qord {
namespace Plugins {

namespace SendTimestamps {

std::optional<int> secondsAfterMidnight(const QString &clean)
{
    static const QRegularExpression re(QStringLiteral(R"(^(\d{1,2}):(\d{1,2})(?: (AM|PM))?$)"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(clean);
    if (!m.hasMatch())
        return std::nullopt;
    int hour = m.captured(1).toInt();
    int minute = m.captured(2).toInt();
    // V8's legacy parser ignores a minute value it can't use and keeps the hour
    if (minute > 59)
        minute = 0;
    const QString meridiem = m.captured(3).toUpper();
    if (!meridiem.isEmpty()) {
        if (hour > 12)
            return std::nullopt;
        hour = hour % 12 + (meridiem == QLatin1String("PM") ? 12 : 0);
    } else if (hour > 24 || (hour == 24 && minute != 0)) {
        return std::nullopt;
    }
    return hour * 3600 + minute * 60;
}

QString parseTime(const QString &backticked, const QDateTime &now)
{
    static const QRegularExpression meridiem(QStringLiteral(R"((\d)(AM|PM)$)"), QRegularExpression::CaseInsensitiveOption);
    QString clean = backticked.mid(1, backticked.size() - 2);
    clean.replace(meridiem, QStringLiteral("\\1 \\2"));
    const auto offset = secondsAfterMidnight(clean);
    if (!offset)
        return backticked;
    const QDateTime local = now.toLocalTime();
    // new Date(`${new Date().toDateString()} ${time}`): today's date, local time
    QDateTime when(local.date(), QTime(0, 0));
    when = when.addSecs(*offset);
    qint64 secs = when.toSecsSinceEpoch();
    // add 24h if time is in the past
    if (now.toSecsSinceEpoch() > secs)
        secs += 86400;
    return QStringLiteral("<t:%1:t>").arg(secs);
}

QString replaceTimes(const QString &content, const QDateTime &now)
{
    static const QRegularExpression re(QStringLiteral(R"(`\d{1,2}:\d{2} ?(?:AM|PM)?`)"),
                                       QRegularExpression::CaseInsensitiveOption);
    QString out;
    qsizetype last = 0;
    auto it = re.globalMatch(content);
    while (it.hasNext()) {
        const auto m = it.next();
        out += content.mid(last, m.capturedStart() - last);
        out += parseTime(m.captured(0), now);
        last = m.capturedEnd();
    }
    out += content.mid(last);
    return out;
}

} // namespace SendTimestamps

void SendTimestampsPlugin::onBeforeSend(OutgoingMessage &message)
{
    if (context->settings().boolValue(QStringLiteral("replaceMessageContents")))
        message.content = SendTimestamps::replaceTimes(message.content, QDateTime::currentDateTime());
}

QList<ChatBarButton> SendTimestampsPlugin::chatBarButtons(const ChatContext &chat)
{
    Q_UNUSED(chat);
    ChatBarButton b;
    b.id = QStringLiteral("insert");
    b.tooltip = QStringLiteral("Insert Timestamp");
    b.svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\">"
            "<path fill=\"currentColor\" d=\"M19 3h-1V1h-2v2H8V1H6v2H5c-1.11 0-1.99.9-1.99 2L3 19a2 2 0 0 0 2 "
            "2h14c1.1 0 2-.9 2-2V5c0-1.1-.9-2-2-2zm0 16H5V8h14v11zM7 10h5v5H7v-5z\"/></svg>";
    return { b };
}

void SendTimestampsPlugin::onChatBarButtonClicked(const QString &id, const ChatContext &chat)
{
    Q_UNUSED(chat);
    if (id != QLatin1String("insert"))
        return;
    if (UiService *ui = context->ui()) {
        // upstream inserts the markup followed by a space
        ui->pickTimestamp([ui](const QString &markup) { ui->insertIntoMessageBox(markup + QLatin1Char(' ')); });
    }
}

QByteArray qord_manifest_sendtimestamps();

void qord_register_sendtimestamps(Registry &registry)
{
    registry.add({ QStringLiteral("sendtimestamps/plugin.json"), [] { return std::make_unique<SendTimestampsPlugin>(); },
                   qord_manifest_sendtimestamps() });
}

} // namespace Plugins
} // namespace Qord
