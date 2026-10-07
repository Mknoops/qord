// Checks Qord::Plugins::Js against results produced by JavaScript (V8) for
// the same inputs.
#include "Plugins/JsCompat.hpp"

#include <QTest>

using namespace Qord::Plugins;

class TestJsCompat : public QObject
{
    Q_OBJECT

private:
    static QString rep(const QString &s, const QString &pattern, const QString &flags, const QString &replacement)
    {
        QString error;
        auto re = Js::compile(pattern, flags, &error);
        if (!re)
            return QStringLiteral("<error>");
        return Js::replace(s, *re, replacement);
    }

private slots:
    void replace_data()
    {
        QTest::addColumn<QString>("subject");
        QTest::addColumn<QString>("pattern");
        QTest::addColumn<QString>("flags");
        QTest::addColumn<QString>("replacement");
        QTest::addColumn<QString>("expected");

        // "aaa".replace(/a/, "b") etc.
        QTest::newRow("first only") << "aaa" << "a" << "" << "b" << "baa";
        QTest::newRow("global") << "aaa" << "a" << "g" << "b" << "bbb";
        QTest::newRow("case") << "AaA" << "a" << "gi" << "b" << "bbb";
        QTest::newRow("$&") << "abc" << "b" << "" << "[$&]" << "a[b]c";
        QTest::newRow("$1 $2") << "john smith" << "(\\w+)\\s(\\w+)" << "" << "$2, $1" << "smith, john";
        QTest::newRow("$$") << "a" << "a" << "" << "$$" << "$";
        QTest::newRow("$` $'") << "abc" << "b" << "" << "[$`|$']" << "a[a|c]c";
        QTest::newRow("$3 out of range") << "ab" << "(a)" << "" << "$3" << "$3b";
        QTest::newRow("$10 falls back to $1+0") << "ab" << "(a)" << "" << "$10" << "a0b";
        QTest::newRow("unmatched group") << "b" << "(a)?b" << "" << "[$1]" << "[]";
        QTest::newRow("named") << "2024-05" << "(?<y>\\d+)-(?<m>\\d+)" << "" << "$<m>/$<y>" << "05/2024";
        QTest::newRow("$< without names") << "ab" << "a" << "" << "$<x>" << "$<x>b";
        QTest::newRow("empty global") << "abc" << "" << "g" << "-" << "-a-b-c-";
        QTest::newRow("dollar is end of input") << "a\n" << "a$" << "" << "X" << "a\n";
        QTest::newRow("multiline dollar") << "a\nb" << "a$" << "m" << "X" << "X\nb";
        QTest::newRow("dot excludes newline") << "a\nb" << "a.b" << "" << "X" << "a\nb";
        QTest::newRow("dotall") << "a\nb" << "a.b" << "s" << "X" << "X";
        QTest::newRow("\\u escape") << "é" << "\\u00e9" << "" << "e" << "e";
        QTest::newRow("\\u{} escape") << "é" << "\\u{e9}" << "u" << "e" << "e";
        QTest::newRow("[^] any") << "a\nb" << "a[^]b" << "" << "X" << "X";
        QTest::newRow("sticky") << "aab" << "a" << "y" << "X" << "Xab";
        QTest::newRow("sticky global stops") << "aaba" << "a" << "gy" << "X" << "XXba";
        QTest::newRow("sticky no match at 0") << "ba" << "a" << "y" << "X" << "ba";
        QTest::newRow("invalid") << "a" << "(" << "" << "X" << "<error>";
        QTest::newRow("bad flag") << "a" << "a" << "q" << "X" << "<error>";
        QTest::newRow("dup flag") << "a" << "a" << "gg" << "X" << "<error>";
    }

    void replace()
    {
        QFETCH(QString, subject);
        QFETCH(QString, pattern);
        QFETCH(QString, flags);
        QFETCH(QString, replacement);
        QFETCH(QString, expected);
        QCOMPARE(rep(subject, pattern, flags, replacement), expected);
    }

    void replaceAllLiteral()
    {
        QCOMPARE(Js::replaceAllLiteral("a.b.c", ".", "-"), QStringLiteral("a-b-c"));
        QCOMPARE(Js::replaceAllLiteral("aaa", "aa", "b"), QStringLiteral("ba"));
        QCOMPARE(Js::replaceAllLiteral("abc", "b", "[$&$$]"), QStringLiteral("a[b$]c"));
        QCOMPARE(Js::replaceAllLiteral("abc", "b", "$1"), QStringLiteral("a$1c"));
        QCOMPARE(Js::replaceAllLiteral("abc", "", "-"), QStringLiteral("-a-b-c-"));
        QCOMPARE(Js::replaceAllLiteral("", "x", "-"), QString());
    }

    void trim()
    {
        QCOMPARE(Js::trim(QStringLiteral(" \t\n a b  　﻿")), QStringLiteral("a b"));
        QCOMPARE(Js::trim(QStringLiteral("   ")), QString());
        // U+0085 is not whitespace to JS
        QCOMPARE(Js::trim(QStringLiteral("\u0085x")), QStringLiteral("\u0085x"));
    }
};

QTEST_GUILESS_MAIN(TestJsCompat)
#include "tst_host_jscompat.moc"
