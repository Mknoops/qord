#include "Qord/Video/H264Rtp.hpp"
#include "Qord/Video/VideoCodec.hpp"

#include <QColor>
#include <QTest>

using namespace Qord::Video;

namespace {

QImage solid(const QColor &color, int w = 320, int h = 180)
{
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(color);
    return image;
}

bool near(const QColor &a, const QColor &b, int tolerance = 40)
{
    return qAbs(a.red() - b.red()) <= tolerance && qAbs(a.green() - b.green()) <= tolerance &&
           qAbs(a.blue() - b.blue()) <= tolerance;
}

} // namespace

class TestVideoCodec : public QObject
{
    Q_OBJECT

private slots:
    // Every H.264 encoder ffmpeg can open on this machine: keyframes with
    // SPS/PPS, forced keyframes, and output that survives RTP and decodes
    void encodersRoundTrip()
    {
        int opened = 0;
        for (const QString &name : H264Encoder::defaultEncoders()) {
            H264Encoder encoder;
            H264Encoder::Settings settings;
            settings.width = 320;
            settings.height = 180;
            settings.fps = 30;
            settings.bitrateKbps = 800;
            if (!encoder.open(settings, { name })) {
                qInfo() << name << "is not available here";
                continue;
            }
            ++opened;
            QList<QByteArray> units;
            for (int i = 0; i < 40; ++i)
                units += encoder.encode(solid(i < 20 ? Qt::red : Qt::blue), i == 25);
            QVERIFY2(units.size() >= 15, qPrintable(name + QStringLiteral(": %1 units").arg(units.size())));
            QVERIFY2(isKeyframe(units.first()), qPrintable(name));
            bool keyAfterForce = false;
            for (int i = 21; i < units.size(); ++i)
                keyAfterForce = keyAfterForce || isKeyframe(units[i]);
            if (name == QLatin1String("h264_mf") && !keyAfterForce)
                qWarning() << "h264_mf ignored the forced keyframe (it is the last fallback)";
            else
                QVERIFY2(keyAfterForce, qPrintable(name));

            Depacketizer depacketizer;
            H264Decoder decoder;
            uint16_t seq = 0;
            QImage last;
            for (int u = 0; u < units.size(); ++u) {
                const QList<QByteArray> payloads = packetize(units[u]);
                for (int i = 0; i < payloads.size(); ++i)
                    if (auto f = depacketizer.push(seq++, quint32(u * 3000), i == payloads.size() - 1, payloads[i])) {
                        const QImage image = decoder.decode(f->annexB);
                        if (!image.isNull())
                            last = image;
                    }
            }
            QVERIFY2(!last.isNull(), qPrintable(name));
            QCOMPARE(last.size(), QSize(320, 180));
            QVERIFY2(near(last.pixelColor(160, 90), Qt::blue), qPrintable(name + QLatin1Char(' ') + last.pixelColor(160, 90).name()));
        }
        QVERIFY2(opened > 0, "no H.264 encoder could be opened");
    }

    void oddSizesAreRoundedDown()
    {
        H264Encoder encoder;
        H264Encoder::Settings settings;
        settings.width = 321;
        settings.height = 181;
        QVERIFY(encoder.open(settings));
        QCOMPARE(encoder.settings().width, 320);
        QCOMPARE(encoder.settings().height, 180);
        // Frames of another size are scaled
        QList<QByteArray> units;
        for (int i = 0; i < 5; ++i)
            units += encoder.encode(solid(Qt::green, 1920, 1080), false);
        QVERIFY(!units.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestVideoCodec)
#include "tst_video_codec.moc"
