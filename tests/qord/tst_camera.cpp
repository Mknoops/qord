#include "Qord/DevMock.hpp"
#include "Qord/Video/CameraCapture.hpp"
#include "Qord/Video/VideoCodec.hpp"
#include "Qord/Video/VideoSender.hpp"

#include <QColor>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTest>

#include <atomic>

using namespace Qord::Video;

namespace {

bool near(QRgb a, const QColor &b, int tolerance = 3)
{
    return qAbs(qRed(a) - b.red()) <= tolerance && qAbs(qGreen(a) - b.green()) <= tolerance &&
           qAbs(qBlue(a) - b.blue()) <= tolerance;
}

// A camera stand-in: a solid 640x480 picture, until it "unplugs"
class FakeSource : public FrameSource
{
public:
    explicit FakeSource(std::atomic<bool> *unplugged) : unplugged(unplugged) {}
    bool start() override
    {
        started = true;
        return true;
    }
    void stop() override { started = false; }
    QImage grab() override
    {
        if (*unplugged)
            return {};
        QImage image(640, 480, QImage::Format_RGB32);
        image.fill(QColor(0x20, 0xa0, 0x60));
        return image;
    }
    [[nodiscard]] bool sourceGone() const override { return *unplugged; }
    [[nodiscard]] QString name() const override { return QStringLiteral("Fake Camera"); }

    bool started = false;

private:
    std::atomic<bool> *unplugged;
};

} // namespace

class TestCamera : public QObject
{
    Q_OBJECT

private slots:
    void yuyvToRgb()
    {
        // Two pixels per 4 bytes: Y0 U Y1 V, BT.601 limited range
        const uchar data[] = {
            16, 128, 235, 128, // black, white
            81, 90, 81, 240, // red, red
            145, 54, 41, 34, // green, blue-ish pair sharing chroma
        };
        const QImage white = convertYuyv(data, 2, 1, 4);
        QCOMPARE(white.size(), QSize(2, 1));
        QVERIFY(near(white.pixel(0, 0), Qt::black));
        QVERIFY(near(white.pixel(1, 0), Qt::white));
        const QImage red = convertYuyv(data + 4, 2, 1, 4);
        QVERIFY(near(red.pixel(0, 0), QColor(255, 0, 0)));
        QVERIFY(near(red.pixel(1, 0), QColor(255, 0, 0)));
        // Rows honour the stride (padding after each line)
        const uchar padded[] = { 16, 128, 16, 128, 0xEE, 0xEE, 235, 128, 235, 128, 0xEE, 0xEE };
        const QImage rows = convertYuyv(padded, 2, 2, 6);
        QVERIFY(near(rows.pixel(1, 0), Qt::black));
        QVERIFY(near(rows.pixel(0, 1), Qt::white));
    }

    void modeScoring()
    {
        const QSize wanted(1280, 720);
        const int exact = scoreCameraMode(QSize(1280, 720), 30, wanted, 30);
        const int bigger = scoreCameraMode(QSize(1920, 1080), 30, wanted, 30);
        const int smaller = scoreCameraMode(QSize(640, 480), 30, wanted, 30);
        QVERIFY(exact > bigger);
        QVERIFY(exact > smaller);
        // Scaling a 1080p camera down beats sending 480p
        QVERIFY(bigger > smaller);
        // The same size at half the frame rate is worth less
        QVERIFY(scoreCameraMode(QSize(1280, 720), 15, wanted, 30) < exact);
        // Faster than wanted earns nothing extra
        QCOMPARE(scoreCameraMode(QSize(1280, 720), 60, wanted, 30), exact);
        // Slide shows and empty modes are unusable
        QVERIFY(scoreCameraMode(QSize(1280, 720), 5, wanted, 30) < 0);
        QVERIFY(scoreCameraMode(QSize(), 30, wanted, 30) < 0);
    }

    void mockCameraOnlyInMockMode()
    {
        QVERIFY(Qord::DevMock::enabled());
        const QList<CameraDevice> cameras = listCameras();
        QVERIFY(!cameras.isEmpty());
        QCOMPARE(cameras.first().name, QStringLiteral("Qord Mock Camera"));

        CameraCapture capture(cameras.first(), QSize(1280, 720), 30);
        QVERIFY(capture.start());
        const QImage first = capture.grab();
        QCOMPARE(first.size(), QSize(1280, 720));
        QTest::qWait(120);
        // The pattern moves, so frozen video would show
        QVERIFY(capture.grab() != first);
        QVERIFY(!capture.sourceGone());
        capture.stop();
        QVERIFY(capture.grab().isNull());
    }

    void unknownCameraFailsToStart()
    {
        CameraCapture capture({ QStringLiteral("/dev/qord-no-such-camera"), QStringLiteral("Nothing") },
                              QSize(1280, 720), 30);
        QVERIFY(!capture.start());
    }

    void senderEncodesAnySource()
    {
        H264Encoder probe;
        if (!probe.open({ 320, 180, 30, 500, 3 }))
            QSKIP("No H.264 encoder in this ffmpeg");
        probe.close();

        std::atomic<bool> unplugged = false;
        H264Encoder::Settings settings;
        settings.width = 1280;
        settings.height = 720;
        settings.fps = 30;
        settings.bitrateKbps = 1500;
        VideoSender sender(std::make_unique<FakeSource>(&unplugged), settings, QStringLiteral("Test"));
        sender.setPreviewSize(QSize(320, 180));
        QSignalSpy encoded(&sender, &VideoSender::frameEncoded);
        QSignalSpy previews(&sender, &VideoSender::previewFrame);
        QSignalSpy failed(&sender, &VideoSender::failed);
        QSignalSpy started(&sender, &VideoSender::started);
        sender.begin();
        QCOMPARE(started.size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(encoded.size() >= 5, 5000);

        // A 4:3 camera is not stretched or upscaled into the 16:9 box
        H264Decoder decoder;
        QImage picture;
        for (const auto &args : std::as_const(encoded)) {
            const QImage image = decoder.decode(args.at(0).toByteArray());
            if (!image.isNull())
                picture = image;
        }
        QCOMPARE(picture.size(), QSize(640, 480));
        const QColor centre = picture.pixelColor(320, 240);
        QVERIFY(qAbs(centre.green() - 0xa0) < 24 && qAbs(centre.red() - 0x20) < 24);
        // RTP timestamps run on the 90 kHz clock and never go backwards
        quint32 last = 0;
        for (const auto &args : std::as_const(encoded)) {
            const quint32 ts = args.at(1).toUInt();
            QVERIFY(ts >= last);
            last = ts;
        }
        QVERIFY(!previews.isEmpty());
        const QImage preview = previews.last().at(0).value<QImage>();
        QVERIFY(preview.width() <= 320 && preview.height() <= 180);

        // Unplugging stops the sender with a reason naming the source
        unplugged = true;
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 2000);
        QVERIFY(failed.first().at(0).toString().contains(QStringLiteral("Fake Camera")));
        const int after = encoded.size();
        QTest::qWait(150);
        QCOMPARE(encoded.size(), after);
    }
};

int main(int argc, char **argv)
{
    // Mock mode (loopback only) lists the test-pattern camera
    qputenv("QORD_MOCK_DISCORD", "http://127.0.0.1:9");
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    TestCamera test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_camera.moc"
