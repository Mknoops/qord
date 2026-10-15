#include "Core/Media/InputPolicy.hpp"
#include <QTest>
#include <QDir>

using namespace Acheron::Core::Media;

class TestMediaInputPolicy : public QObject
{
    Q_OBJECT
private slots:
    void remoteMetadataCannotBecomeLocalInput()
    {
        const auto local = QUrl::fromLocalFile(QDir::temp().absoluteFilePath("dummy.wav"));
        QVERIFY(!allowedChatMediaUrl(local));
        QVERIFY(allowedChatMediaUrl(local, true));
        QVERIFY(!allowedChatMediaUrl(QUrl("file://host/share/secret.wav"), true));
        QVERIFY(!allowedChatMediaUrl(QUrl("concat:local.wav|other.wav")));
        QVERIFY(!allowedChatMediaUrl(QUrl("data:audio/wav;base64,AAAA")));
        QVERIFY(!allowedChatMediaUrl(QUrl("ftp://media.invalid/audio.wav")));
        QVERIFY(!allowedChatMediaUrl(QUrl("relative.wav")));
        QVERIFY(localMediaPath("concat:local.wav|other.wav").isEmpty());
        QVERIFY(localMediaPath("//host/share/file.wav").isEmpty());
        QVERIFY(localMediaPath("file://host/share/file.wav").isEmpty());
    }
    void networkPolicy()
    {
        if (Qord::DevMock::enabled()) {
            const QUrl local = Qord::DevMock::rewrite(QUrl("https://media.invalid/video.mp4"));
            QVERIFY(allowedRemoteMediaUrl(local));
            QVERIFY(!allowedRemoteMediaUrl(QUrl("http://media.invalid/video.mp4")));
            QVERIFY(!allowedRemoteMediaUrl(QUrl("http://127.0.0.1:9999/video.mp4")));
        } else {
            QVERIFY(allowedRemoteMediaUrl(QUrl("https://media.invalid/video.mp4")));
            QVERIFY(!allowedRemoteMediaUrl(QUrl("http://media.invalid/video.mp4")));
            QVERIFY(!allowedRemoteMediaUrl(QUrl("https://user:password@media.invalid/video.mp4")));
        }
        QVERIFY(!allowedRemoteMediaUrl(QUrl("file:///tmp/dummy.wav")));
        QVERIFY(!allowedRemoteMediaUrl(QUrl("https://media.invalid/video.mp4#fragment")));
    }
};
QTEST_GUILESS_MAIN(TestMediaInputPolicy)
#include "tst_media_input_policy.moc"
