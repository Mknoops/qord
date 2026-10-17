#include "PluginTestUtils.hpp"

#include "FakeProfileThemes.hpp"

#include <QTest>

namespace Qord::Plugins {
void qord_register_fakeprofilethemes(Registry &registry);
}

using namespace Qord::Plugins;
using namespace Qord::Plugins::Testing;

class TestFakeProfileThemes : public QObject
{
    Q_OBJECT

    static ProfileThemeContext profile(const QString &bio, Id userId = 7)
    {
        ProfileThemeContext p;
        p.accountId = 1;
        p.userId = userId;
        p.bio = bio;
        return p;
    }

private slots:
    // encode() and decode() as upstream: invisible tag characters after a space
    void encodesAndDecodes()
    {
        const QString text = FakeProfileThemes::encode(0x5865f2, 0xeb459e);
        QVERIFY(text.startsWith(QLatin1Char(' ')));
        const auto codes = text.mid(1).toUcs4(); // QVector on Qt 5, QList on Qt 6
        QCOMPARE(codes.size(), 17); // "[#5865f2,#eb459e]"
        QCOMPARE(codes.first(), uint(0xE0000 + '['));
        for (uint code : codes)
            QVERIFY(code >= 0xE0020 && code <= 0xE007F);
        const auto colors = FakeProfileThemes::decode(QStringLiteral("I like cats") + text + QStringLiteral(" :)"));
        QVERIFY(colors);
        QCOMPARE(colors->first, quint32(0x5865f2));
        QCOMPARE(colors->second, quint32(0xeb459e));
        // Short hex, as upstream's {1,6} allows
        QCOMPARE(FakeProfileThemes::decode(FakeProfileThemes::encode(0xff, 0)).value(), std::make_pair(quint32(0xff), quint32(0)));
        QVERIFY(!FakeProfileThemes::decode(QStringLiteral("[#5865f2,#eb459e]"))); // visible text doesn't count
        QVERIFY(!FakeProfileThemes::decode(QString()));
    }

    void themesProfiles()
    {
        SinglePluginHost h(&qord_register_fakeprofilethemes);
        QVERIFY(h.info()->loadError.isEmpty());
        QVERIFY(h.enableWithAllPermissions());
        const QString bio = QStringLiteral("hello") + FakeProfileThemes::encode(0x112233, 0x445566);
        auto theme = h.host->dispatchProfileTheme(profile(bio));
        QVERIFY(theme);
        QCOMPARE(theme->primary, quint32(0x112233));
        QCOMPARE(theme->accent, quint32(0x445566));
        QVERIFY(!h.host->dispatchProfileTheme(profile(QStringLiteral("no colours here"))));

        // A Nitro theme comes first unless fake colours are preferred
        ProfileThemeContext nitro = profile(bio);
        nitro.primary = 0xaaaaaa;
        nitro.accent = 0xbbbbbb;
        QVERIFY(!h.host->dispatchProfileTheme(nitro));
        h.host->settings(h.id)->setValue(QStringLiteral("nitroFirst"), QStringLiteral("fake"));
        theme = h.host->dispatchProfileTheme(nitro);
        QVERIFY(theme);
        QCOMPARE(theme->primary, quint32(0x112233));
    }

    void copiesAndPreviews()
    {
        SinglePluginHost h(&qord_register_fakeprofilethemes);
        QVERIFY(h.enableWithAllPermissions());
        h.host->settings(h.id)->setValue(QStringLiteral("primaryColor"), QStringLiteral("#5865f2"));
        h.host->settings(h.id)->setValue(QStringLiteral("accentColor"), QStringLiteral("#eb459e"));
        h.host->dispatchSettingAction(h.id, QStringLiteral("copy3y3"));
        QCOMPARE(h.clipboard.text, FakeProfileThemes::encode(0x5865f2, 0xeb459e));

        // Your own profile with the picked colours; others' as they are
        QVERIFY(!h.host->dispatchProfileTheme(profile(QString(), 1)));
        h.host->dispatchSettingAction(h.id, QStringLiteral("preview"));
        QCOMPARE(h.ui.profilesOpened.size(), 1);
        QCOMPARE(h.ui.profilesOpened[0].second, Id(1));
        const auto own = h.host->dispatchProfileTheme(profile(QString(), 1));
        QVERIFY(own);
        QCOMPARE(own->accent, quint32(0xeb459e));
        QVERIFY(!h.host->dispatchProfileTheme(profile(QString(), 7)));
    }

    // Colour settings must look like #rrggbb
    void colorSettingsAreChecked()
    {
        SinglePluginHost h(&qord_register_fakeprofilethemes);
        QVERIFY(h.enableWithAllPermissions());
        QVERIFY(!h.host->settings(h.id)->setValue(QStringLiteral("primaryColor"), QStringLiteral("blue")).isEmpty());
        QVERIFY(h.host->settings(h.id)->setValue(QStringLiteral("primaryColor"), QStringLiteral("#00FF7f")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestFakeProfileThemes)
#include "tst_fakeprofilethemes.moc"
