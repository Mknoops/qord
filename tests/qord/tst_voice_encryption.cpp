#include "Discord/Voice/VoiceEncryption.hpp"

#include <QTest>
#include <QLoggingCategory>

// The crypto unit has no logger, network, account or keychain dependencies.
Q_LOGGING_CATEGORY(LogVoice, "test.voice")

using namespace Acheron::Discord::Voice;

class TestVoiceEncryption : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { QVERIFY(VoiceEncryption::initialize()); }

    void rejectsInvalidKeys()
    {
        for (const auto mode : { EncryptionMode::AEAD_AES256_GCM_RTPSIZE,
                                EncryptionMode::AEAD_XCHACHA20_POLY1305_RTPSIZE }) {
            for (int size : { 0, 1, 31, 33, 64 }) {
                const QByteArray key(size, 'x');
                QVERIFY(!VoiceEncryption::isValidKey(mode, key));
                VoiceEncryption cipher(mode, key);
                QVERIFY(cipher.encrypt(QByteArray(12, 'h'), QByteArray("dummy")).isEmpty());
                QVERIFY(cipher.decrypt(QByteArray(12, 'h'), QByteArray(32, 'x')).isEmpty());
            }
        }
        VoiceEncryption unknown(EncryptionMode::UNKNOWN, QByteArray(32, 'x'));
        QVERIFY(unknown.encrypt({}, QByteArray("dummy")).isEmpty());
        QVERIFY(unknown.decrypt({}, QByteArray(32, 'x')).isEmpty());
    }

    void roundTripAndAuthentication()
    {
        for (const auto mode : { EncryptionMode::AEAD_AES256_GCM_RTPSIZE,
                                EncryptionMode::AEAD_XCHACHA20_POLY1305_RTPSIZE }) {
            if (!VoiceEncryption::isModeAvailable(mode))
                continue;
            const QByteArray key(32, 'x'); // Dummy test data only.
            QVERIFY(VoiceEncryption::isValidKey(mode, key));
            VoiceEncryption cipher(mode, key);
            const QByteArray header(12, 'h');
            const QByteArray plain("dummy media");
            QByteArray encrypted = cipher.encrypt(header, plain);
            QVERIFY(!encrypted.isEmpty());
            QCOMPARE(cipher.decrypt(header, encrypted), plain);
            QVERIFY(cipher.decrypt(QByteArray(12, 'j'), encrypted).isEmpty());
            encrypted[0] = char(encrypted.at(0) ^ 1);
            QVERIFY(cipher.decrypt(header, encrypted).isEmpty());
            QVERIFY(cipher.decrypt(header, QByteArray(19, 'x')).isEmpty());
        }
    }
};

QTEST_GUILESS_MAIN(TestVoiceEncryption)
#include "tst_voice_encryption.moc"
