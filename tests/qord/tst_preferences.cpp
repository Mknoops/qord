#include "Qord/Keybinds.hpp"
#include "Qord/Preferences.hpp"

#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using Qord::Keybinds;
using Qord::Preferences;
namespace Pref = Qord::Pref;

class TestPreferences : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("QordTests"));
        QCoreApplication::setApplicationName(QStringLiteral("TestPreferences"));
        QSettings().clear();
    }

    void defaultsAndChanges()
    {
        QCOMPARE(Preferences::flag(Pref::ShowEmbeds), true);
        QCOMPARE(Preferences::number(Pref::GroupSpacing), 16);
        QCOMPARE(Preferences::text(Pref::TimeFormat), QStringLiteral("system"));

        QSignalSpy changed(&Preferences::instance(), &Preferences::changed);
        Preferences::set(Pref::ShowEmbeds, false);
        Preferences::set(Pref::ShowEmbeds, false); // unchanged: no signal
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().first().toString(), QString::fromLatin1(Pref::ShowEmbeds));
        QCOMPARE(Preferences::flag(Pref::ShowEmbeds), false);
        // INI files hand strings back; the default's type wins
        QSettings().setValue(Pref::GroupSpacing, QStringLiteral("8"));
        Preferences::reload();
        QCOMPARE(Preferences::value(Pref::GroupSpacing).userType(), int(QMetaType::Int));
        QCOMPARE(Preferences::number(Pref::GroupSpacing), 8);
    }

    void streamerMode()
    {
        QVERIFY(!Preferences::streamerHides(Pref::StreamerHideInvites));
        Preferences::set(Pref::Streamer, true);
        QVERIFY(Preferences::streamerHides(Pref::StreamerHideInvites));
        Preferences::set(Pref::StreamerHideInvites, false);
        QVERIFY(!Preferences::streamerHides(Pref::StreamerHideInvites));
    }

    void reduceMotion()
    {
        QVERIFY(Preferences::animateEmoji());
        Preferences::set(Pref::ReduceMotion, true);
        QVERIFY(!Preferences::animateEmoji());
        QVERIFY(!Preferences::animateStickers());
        Preferences::set(Pref::ReduceMotion, false);
    }

    void timeFormat()
    {
        const QTime time(15, 5);
        Preferences::set(Pref::TimeFormat, QStringLiteral("24"));
        QCOMPARE(Preferences::timeText(time), QStringLiteral("15:05"));
        Preferences::set(Pref::TimeFormat, QStringLiteral("12"));
        QCOMPARE(Preferences::timeText(time), QStringLiteral("3:05 PM"));
    }

    void emoticons_data()
    {
        QTest::addColumn<QString>("typed");
        QTest::addColumn<QString>("sent");
        QTest::newRow("smile") << QStringLiteral("hi :)") << QStringLiteral("hi \U0001F642");
        QTest::newRow("heart alone") << QStringLiteral("<3") << QStringLiteral("❤️");
        QTest::newRow("several") << QStringLiteral(":D and ;)") << QStringLiteral("\U0001F603 and \U0001F609");
        QTest::newRow("inside a word") << QStringLiteral("http://x.com a:b") << QStringLiteral("http://x.com a:b");
        QTest::newRow("inline code") << QStringLiteral("`:)` :)") << QStringLiteral("`:)` \U0001F642");
        QTest::newRow("code block") << QStringLiteral("```\n:)\n``` :(") << QStringLiteral("```\n:)\n``` \U0001F641");
        QTest::newRow("newlines kept") << QStringLiteral(":)\n:)") << QStringLiteral("\U0001F642\n\U0001F642");
    }

    void emoticons()
    {
        QFETCH(QString, typed);
        QFETCH(QString, sent);
        QCOMPARE(Preferences::convertEmoticons(typed), sent);
    }

    void keybinds()
    {
        QCOMPARE(Keybinds::key(QStringLiteral("openSettings")), QKeySequence(Qt::CTRL | Qt::Key_Comma));
        QCOMPARE(Keybinds::conflict(QKeySequence(Qt::CTRL | Qt::Key_B), QString()), QStringLiteral("toggleChannelList"));
        QVERIFY(Keybinds::conflict(QKeySequence(Qt::CTRL | Qt::Key_B), QStringLiteral("toggleChannelList")).isEmpty());

        QSignalSpy changed(&Keybinds::instance(), &Keybinds::changed);
        Keybinds::setKey(QStringLiteral("openSettings"), QKeySequence(Qt::CTRL | Qt::Key_P));
        QCOMPARE(Keybinds::key(QStringLiteral("openSettings")), QKeySequence(Qt::CTRL | Qt::Key_P));
        Keybinds::setKey(QStringLiteral("openSettings"), QKeySequence());
        QVERIFY(Keybinds::key(QStringLiteral("openSettings")).isEmpty()); // cleared, not the default
        Keybinds::reset(QStringLiteral("openSettings"));
        QCOMPARE(Keybinds::key(QStringLiteral("openSettings")), QKeySequence(Qt::CTRL | Qt::Key_Comma));
        QCOMPARE(changed.count(), 3);
    }
};

QTEST_GUILESS_MAIN(TestPreferences)
#include "tst_preferences.moc"
