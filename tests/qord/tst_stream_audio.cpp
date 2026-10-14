#include "Core/Audio/IAudioBackend.hpp"
#include "Core/Audio/OpusDecoder.hpp"
#include "Qord/Audio/StreamAudio.hpp"

#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <vector>

using namespace Qord::Audio;
using Acheron::Core::Audio::AUDIO_FRAME_SIZE;

namespace {

// Interleaved stereo s16
std::vector<int16_t> tone(int frames, double hz, int start = 0)
{
    std::vector<int16_t> out(size_t(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        const auto v = int16_t(8000 * std::sin(2 * 3.14159265358979323846 * hz * (start + i) / 48000.0));
        out[size_t(i) * 2] = v;
        out[size_t(i) * 2 + 1] = v;
    }
    return out;
}

double rms(const QByteArray &pcm)
{
    const auto *s = reinterpret_cast<const int16_t *>(pcm.constData());
    const int n = int(pcm.size() / 2);
    double sum = 0;
    for (int i = 0; i < n; ++i)
        sum += double(s[i]) * s[i];
    return n ? std::sqrt(sum / n) : 0;
}

} // namespace

class TestStreamAudio : public QObject
{
    Q_OBJECT

private slots:
    void assemblesTwentyMillisecondFrames()
    {
        FrameAssembler assembler;
        QList<QByteArray> frames;
        const auto collect = [&](const QByteArray &f) { frames.append(f); };
        // Capture buffers rarely line up with 960 samples: 441, 441, 441...
        const std::vector<int16_t> samples = tone(441 * 10, 440);
        for (int i = 0; i < 10; ++i)
            assembler.push(samples.data() + size_t(i) * 441 * 2, 441, collect);
        QCOMPARE(frames.size(), 4410 / 960); // 4 whole frames, the rest waits
        for (const QByteArray &f : std::as_const(frames))
            QCOMPARE(f.size(), AUDIO_FRAME_SIZE);
        // Nothing is lost or reordered across buffers
        QCOMPARE(frames[1], QByteArray(reinterpret_cast<const char *>(samples.data() + 960 * 2), AUDIO_FRAME_SIZE));
        assembler.push(samples.data(), 960 * 5 - 4410, collect);
        QCOMPARE(frames.size(), 5);
    }

    void encodesMusicAndSkipsSilence()
    {
        StreamAudio audio;
        QVERIFY(audio.prepareEncoder());
        QSignalSpy packets(&audio, &StreamAudio::opusFrame);

        // One second of a tone, in odd-sized buffers
        int at = 0;
        for (int chunk : { 333, 1500, 480, 2048, 1000, 42639 }) {
            const std::vector<int16_t> s = tone(chunk, 440, at);
            audio.pushSamples(s.data(), size_t(chunk));
            at += chunk;
        }
        QCOMPARE(at, 48000);
        QCOMPARE(packets.size(), 50);

        Acheron::Core::Audio::OpusDecoder decoder;
        QVERIFY(decoder.init(48000, 2));
        double loudest = 0;
        for (const auto &args : std::as_const(packets)) {
            const QByteArray opus = args.at(0).toByteArray();
            QVERIFY(opus.size() > 2);
            for (const QByteArray &pcm : decoder.decode(opus))
                loudest = qMax(loudest, rms(pcm));
        }
        // 8000 peak: about 5650 RMS
        QVERIFY2(loudest > 4000, qPrintable(QString::number(loudest)));

        // Two seconds of silence: DTX packets aren't sent
        packets.clear();
        const std::vector<int16_t> quiet(96000 * 2, 0);
        audio.pushSamples(quiet.data(), 96000);
        QVERIFY2(packets.size() < 20, qPrintable(QString::number(packets.size())));
    }

    void stoppedEncoderSendsNothing()
    {
        StreamAudio audio;
        QSignalSpy packets(&audio, &StreamAudio::opusFrame);
        const std::vector<int16_t> s = tone(9600, 440);
        audio.pushSamples(s.data(), 9600);
        QCOMPARE(packets.size(), 0);
        QVERIFY(audio.prepareEncoder());
        audio.stop();
        audio.pushSamples(s.data(), 9600);
        QCOMPARE(packets.size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestStreamAudio)
#include "tst_stream_audio.moc"
