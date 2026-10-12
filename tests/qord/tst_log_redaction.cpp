#include "Core/LogRedaction.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

using namespace Acheron::Core;

class TestLogRedaction : public QObject
{
    Q_OBJECT

private slots:
    void voiceIdentifyAndSessionDescription()
    {
        const QByteArray identify = R"({"op":0,"d":{"server_id":"1","user_id":"2","session_id":"s","token":"abc.def"}})";
        const QByteArray out = redactSecrets(identify);
        QVERIFY(!out.contains("abc.def"));
        QVERIFY(out.contains(R"("token":"<redacted>")"));
        QVERIFY(out.contains(R"("server_id":"1")")); // the rest stays readable

        const QByteArray session = R"({"op":4,"d":{"mode":"aead_aes256_gcm_rtpsize","secret_key":[1,2,3,250]}})";
        const QByteArray redacted = redactSecrets(session);
        QVERIFY(!redacted.contains("250"));
        QVERIFY(redacted.contains("aead_aes256_gcm_rtpsize"));
    }

    void nestedAndInArrays()
    {
        QJsonObject inner { { "token", "stream-token" }, { "endpoint", "x.discord.media" } };
        QJsonObject root { { "t", "STREAM_SERVER_UPDATE" },
                           { "d", inner },
                           { "list", QJsonArray { QJsonObject { { "Password", "hunter2" } } } },
                           { "analytics_token", "a" } };
        const QJsonObject out = redactSecrets(root);
        const QByteArray text = QJsonDocument(out).toJson(QJsonDocument::Compact);
        QVERIFY(!text.contains("stream-token"));
        QVERIFY(!text.contains("hunter2")); // keys match case-insensitively
        QVERIFY(text.contains("x.discord.media"));
        QCOMPARE(out.value("analytics_token").toString(), QStringLiteral("<redacted>"));
    }

    void nullStaysNull()
    {
        // A null token says something useful ("none") and holds no secret
        const QJsonObject out = redactSecrets(QJsonObject { { "token", QJsonValue::Null } });
        QVERIFY(out.value("token").isNull());
    }

    void nonJsonIsNotEchoed()
    {
        const QByteArray form("token=abc.def&x=1");
        const QByteArray out = redactSecrets(form);
        QVERIFY(!out.contains("abc.def"));
        QCOMPARE(out, QByteArray("<payload, ") + QByteArray::number(form.size()) + " bytes>");
    }
};

QTEST_GUILESS_MAIN(TestLogRedaction)
#include "tst_log_redaction.moc"
