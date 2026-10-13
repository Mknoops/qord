#include "Core/RoleStyle.hpp"
#include "UI/Qord/FlowLayout.hpp"

#include <QApplication>
#include <QTest>
#include <QWidget>

using Acheron::UI::FlowLayout;

// Qord's small UI pieces: the flow layout and the role color rules
class TestQordUi : public QObject
{
    Q_OBJECT

    static QWidget *box(QWidget *parent, const QString &name)
    {
        auto *w = new QWidget(parent);
        w->setObjectName(name);
        w->setFixedSize(32, 32);
        return w;
    }

private slots:
    void wrapsAndInserts()
    {
        QWidget host;
        auto *flow = new FlowLayout(&host, 6);
        QWidget *a = box(&host, QStringLiteral("a"));
        QWidget *b = box(&host, QStringLiteral("b"));
        QWidget *c = box(&host, QStringLiteral("c"));
        flow->addWidget(a);
        flow->addWidget(b);
        flow->addWidget(c);
        host.resize(100, 100); // two per line: 32 + 6 + 32 = 70, a third needs 108
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        QCOMPARE(a->geometry(), QRect(0, 0, 32, 32));
        QCOMPARE(b->geometry(), QRect(38, 0, 32, 32));
        QCOMPARE(c->geometry(), QRect(0, 38, 32, 32));
        QCOMPARE(flow->heightForWidth(100), 70);
        QCOMPARE(flow->heightForWidth(200), 32);

        // Inserted after `a`, shown, and laid out with the rest
        QWidget *x = box(&host, QStringLiteral("x"));
        flow->insertWidget(flow->indexOfWidget(a) + 1, x);
        x->show();
        QTRY_COMPARE(x->geometry(), QRect(38, 0, 32, 32));
        QCOMPARE(b->geometry(), QRect(0, 38, 32, 32));
        QCOMPARE(c->geometry(), QRect(38, 38, 32, 32));
        QCOMPARE(flow->indexOfWidget(x), 1);
    }

    void roleStyles()
    {
        using namespace Acheron::Core;
        for (const auto style : { RoleStyle::Username, RoleStyle::Dot, RoleStyle::Hidden })
            QCOMPARE(roleStyleFromKey(roleStyleKey(style)), style);
        QCOMPARE(roleStyleFromKey(QStringLiteral("junk")), RoleStyle::Username);

        auto &style = roleStyle();
        auto &dot = roleDotOptions();
        style = RoleStyle::Username;
        QVERIFY(roleColorOnName() && !roleDotAfterName());
        style = RoleStyle::Dot;
        QVERIFY(!roleColorOnName() && roleDotAfterName());
        QVERIFY(!profileRoleDotsCopy());
        style = RoleStyle::Hidden;
        QVERIFY(!roleColorOnName() && !roleDotAfterName());

        // BetterRoleDot
        dot.enabled = true;
        QVERIFY(!profileRoleDotsCopy());
        dot.bothStyles = true; // both, whatever the setting
        QVERIFY(roleColorOnName() && roleDotAfterName());
        QVERIFY(profileRoleDotsCopy());
        dot.bothStyles = false;
        dot.inProfile = true;
        QVERIFY(profileRoleDotsCopy());
        dot.inProfile = false;
        style = RoleStyle::Dot;
        QVERIFY(profileRoleDotsCopy());
        dot = {};
        style = RoleStyle::Username;
    }
};

QTEST_MAIN(TestQordUi)
#include "tst_qord_ui.moc"
