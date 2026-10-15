#include "Core/Media/MediaDecoder.hpp"
#include "Core/Media/CurlAvio.hpp"
#include <QDataStream>
#include <QFile>
#include <QLoggingCategory>
#include <QTemporaryDir>
#include <QTest>

Q_LOGGING_CATEGORY(LogVideo, "test.media")
// This fixture exercises the real FFmpeg decoder and local/nested file policy;
// its transport seam cannot create any network connection.
namespace Acheron::Discord::CurlUtils {
QString getUserAgent() { return QStringLiteral("Qord-test"); }
}
namespace Acheron::Core::Media::CurlAvio {
AVIOContext *create(const QString &, const ProxyConfig &, const AVIOInterruptCB &) { return nullptr; }
void destroy(AVIOContext *) {}
}
using namespace Acheron::Core::Media;

class TestMediaDecoderPolicy : public QObject
{
    Q_OBJECT
private slots:
    void localWaveWorksButNestedPlaylistFails()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile wave(dir.filePath("sample.wav"));
        QVERIFY(wave.open(QIODevice::WriteOnly));
        QDataStream out(&wave);
        out.setByteOrder(QDataStream::LittleEndian);
        out.writeRawData("RIFF", 4); out << quint32(36 + 1600);
        out.writeRawData("WAVEfmt ", 8); out << quint32(16) << quint16(1) << quint16(1);
        out << quint32(8000) << quint32(16000) << quint16(2) << quint16(16);
        out.writeRawData("data", 4); out << quint32(1600);
        out.writeRawData(QByteArray(1600, '\0').constData(), 1600);
        wave.close();
        QString error;
        MediaDecoder direct;
        QVERIFY2(direct.open(wave.fileName(), {}, &error), qPrintable(error));
        QVERIFY(direct.hasAudio());
        direct.close();
        QFile playlist(dir.filePath("list.ffconcat"));
        QVERIFY(playlist.open(QIODevice::WriteOnly));
        playlist.write("ffconcat version 1.0\nfile 'sample.wav'\n");
        playlist.close();
        MediaDecoder nested;
        QVERIFY(!nested.open(playlist.fileName(), {}, &error));
    }
};
QTEST_GUILESS_MAIN(TestMediaDecoderPolicy)
#include "tst_media_decoder_policy.moc"
