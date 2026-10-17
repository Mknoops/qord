/*
 * Qord, a native Discord client
 * FakeNitro's animated sticker conversion. The APNG reading follows
 * Equicord's src/utils/apng.ts (apng-canvas, MIT, David Mzareulyan); the
 * frame drawing follows FakeNitro's sendAnimatedSticker(). Upstream encodes
 * with gifenc; this is a small GIF89a writer instead.
 * The GIF writer (parallel, frame deltas): Maarten
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ApngToGif.hpp"

#include <cmath>

#include <QHash>
#include <QPainter>
#include <QVector>

#include <algorithm>
#include <array>
#include <atomic>
#include <thread>
#include <vector>

namespace Qord {
namespace Plugins {
namespace ApngToGif {

namespace {

const QByteArray PngSignature("\x89PNG\r\n\x1a\n", 8);

// Raw bytes: QByteArray's operator[] is ambiguous with 64-bit indexes on Qt 5
quint8 byteAt(const QByteArray &b, qsizetype off)
{
    return reinterpret_cast<const quint8 *>(b.constData())[off];
}

quint32 readDWord(const QByteArray &b, qsizetype off)
{
    return (quint32(byteAt(b, off)) << 24) | (quint32(byteAt(b, off + 1)) << 16) | (quint32(byteAt(b, off + 2)) << 8) |
           quint32(byteAt(b, off + 3));
}

quint16 readWord(const QByteArray &b, qsizetype off)
{
    return quint16((byteAt(b, off) << 8) | byteAt(b, off + 1));
}

void appendDWord(QByteArray &out, quint32 v)
{
    out.append(char(v >> 24)).append(char(v >> 16)).append(char(v >> 8)).append(char(v));
}

quint32 crc32(const QByteArray &bytes)
{
    static const auto table = [] {
        std::array<quint32, 256> t {};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    quint32 crc = 0xFFFFFFFFu;
    for (const char ch : bytes)
        crc = (crc >> 8) ^ table[(crc ^ quint8(ch)) & 0xFF];
    return crc ^ 0xFFFFFFFFu;
}

QByteArray makeChunk(const char *type, const QByteArray &data)
{
    QByteArray typed(type, 4);
    typed += data;
    QByteArray out;
    appendDWord(out, quint32(data.size()));
    out += typed;
    appendDWord(out, crc32(typed));
    return out;
}

void appendLe16(QByteArray &out, int v)
{
    out.append(char(v & 0xFF)).append(char((v >> 8) & 0xFF));
}

// Median cut over the opaque pixels' colours (5 bits per channel)
struct Box
{
    QList<quint16> colors;
    qint64 population = 0;
};

QList<QRgb> quantize(const QHash<quint16, int> &histogram, int maxColors)
{
    const auto channel = [](quint16 c, int ch) { return (c >> (10 - 5 * ch)) & 0x1F; };
    QList<Box> boxes;
    Box all;
    for (auto it = histogram.cbegin(); it != histogram.cend(); ++it) {
        all.colors.append(it.key());
        all.population += it.value();
    }
    if (all.colors.isEmpty())
        return {};
    boxes.append(all);
    while (boxes.size() < maxColors) {
        // The splittable box with the most pixels
        int pick = -1;
        for (int i = 0; i < boxes.size(); ++i)
            if (boxes[i].colors.size() > 1 && (pick < 0 || boxes[i].population > boxes[pick].population))
                pick = i;
        if (pick < 0)
            break;
        Box box = boxes.takeAt(pick);
        int bestChannel = 0, bestRange = -1;
        for (int ch = 0; ch < 3; ++ch) {
            int lo = 31, hi = 0;
            for (quint16 c : box.colors) {
                lo = std::min(lo, channel(c, ch));
                hi = std::max(hi, channel(c, ch));
            }
            if (hi - lo > bestRange) {
                bestRange = hi - lo;
                bestChannel = ch;
            }
        }
        std::sort(box.colors.begin(), box.colors.end(),
                  [&](quint16 a, quint16 b) { return channel(a, bestChannel) < channel(b, bestChannel); });
        // Split at the pixel median
        qint64 half = box.population / 2, seen = 0;
        qsizetype cut = 1;
        for (qsizetype i = 0; i < box.colors.size() - 1; ++i) {
            seen += histogram.value(box.colors[i]);
            cut = i + 1;
            if (seen >= half)
                break;
        }
        Box a, b;
        a.colors = box.colors.mid(0, cut);
        b.colors = box.colors.mid(cut);
        for (quint16 c : std::as_const(a.colors))
            a.population += histogram.value(c);
        b.population = box.population - a.population;
        boxes.append(a);
        boxes.append(b);
    }
    QList<QRgb> palette;
    for (const Box &box : std::as_const(boxes)) {
        qint64 r = 0, g = 0, bl = 0;
        for (quint16 c : box.colors) {
            const qint64 n = histogram.value(c);
            r += qint64(channel(c, 0)) * n;
            g += qint64(channel(c, 1)) * n;
            bl += qint64(channel(c, 2)) * n;
        }
        const qint64 n = std::max<qint64>(1, box.population);
        const auto expand = [](qint64 v5) { return int((v5 * 255 + 15) / 31); };
        palette.append(qRgb(expand(r / n), expand(g / n), expand(bl / n)));
    }
    return palette;
}

// GIF's variable-width LZW, as gif.h and gifenc write it
QByteArray lzw(const QByteArray &indices, int minCodeSize)
{
    QByteArray out;
    out.reserve(indices.size() / 2);
    quint32 bitBuffer = 0;
    int bitCount = 0;
    const auto write = [&](int code, int size) {
        bitBuffer |= quint32(code) << bitCount;
        bitCount += size;
        while (bitCount >= 8) {
            out.append(char(bitBuffer & 0xFF));
            bitBuffer >>= 8;
            bitCount -= 8;
        }
    };
    const int clearCode = 1 << minCodeSize;
    int codeSize = minCodeSize + 1;
    int maxCode = clearCode + 1;
    // (prefix code, next index) -> code, open addressing: much faster than
    // a QHash, and cheap to clear
    constexpr quint32 TableSize = 8192;
    std::vector<qint32> keys(TableSize, -1);
    std::vector<quint16> codes(TableSize);
    const auto slot = [&](quint32 key) {
        quint32 h = (key * 2654435761u) >> 19; // top 13 bits
        while (keys[h] != -1 && quint32(keys[h]) != key)
            h = (h + 1) & (TableSize - 1);
        return h;
    };
    write(clearCode, codeSize);
    if (indices.isEmpty()) {
        write(clearCode + 1, codeSize);
    } else {
        int current = byteAt(indices, 0);
        for (qsizetype i = 1; i < indices.size(); ++i) {
            const int next = byteAt(indices, i);
            const quint32 key = (quint32(current) << 8) | quint32(next);
            const quint32 h = slot(key);
            if (keys[h] != -1) {
                current = codes[h];
                continue;
            }
            write(current, codeSize);
            ++maxCode;
            keys[h] = qint32(key);
            codes[h] = quint16(maxCode);
            if (maxCode >= (1 << codeSize))
                ++codeSize;
            if (maxCode == 4095) {
                write(clearCode, codeSize);
                std::fill(keys.begin(), keys.end(), -1);
                codeSize = minCodeSize + 1;
                maxCode = clearCode + 1;
            }
            current = next;
        }
        write(current, codeSize);
        write(clearCode + 1, codeSize);
    }
    if (bitCount > 0)
        out.append(char(bitBuffer & 0xFF));

    // Sub-blocks of at most 255 bytes
    QByteArray blocks;
    for (qsizetype i = 0; i < out.size(); i += 255) {
        const QByteArray part = out.mid(i, 255);
        blocks.append(char(part.size()));
        blocks += part;
    }
    blocks.append('\0');
    return blocks;
}

// Runs body(0..count-1) over the machine's cores
template <typename F>
void parallelFor(qsizetype count, F &&body)
{
    const qsizetype threads =
            qMin<qsizetype>(count, qBound(1, int(std::thread::hardware_concurrency()), 16));
    if (threads <= 1) {
        for (qsizetype i = 0; i < count; ++i)
            body(i);
        return;
    }
    std::atomic<qsizetype> next { 0 };
    const auto work = [&] {
        for (qsizetype i = next++; i < count; i = next++)
            body(i);
    };
    std::vector<std::thread> pool;
    pool.reserve(size_t(threads - 1));
    for (qsizetype t = 1; t < threads; ++t)
        pool.emplace_back(work);
    work();
    for (std::thread &thread : pool)
        thread.join();
}

// A colour at 5 bits per channel, as the palette is built
inline quint16 colorKey(QRgb px)
{
    return quint16(((qRed(px) >> 3) << 10) | ((qGreen(px) >> 3) << 5) | (qBlue(px) >> 3));
}

struct FramePlan
{
    QRect rect;
    bool delta = false; // drawn over the frame before
    bool sparse = false; // and its unchanged pixels left transparent
    int disposal = 2; // 1 keep, 2 clear, before the next frame
};

// One frame's graphic control extension, descriptor, colour table and data
QByteArray encodeFrame(const QImage &image, const QImage *before, const FramePlan &plan, double delayMs)
{
    const QRect r = plan.rect;
    const auto drawn = [&](int x, int y, QRgb px) {
        if (qAlpha(px) < 128)
            return false;
        if (!plan.sparse)
            return true;
        const QRgb old = reinterpret_cast<const QRgb *>(before->constScanLine(y))[x];
        return qAlpha(old) < 128 || colorKey(old) != colorKey(px);
    };

    QHash<quint16, int> histogram;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = r.left(); x <= r.right(); ++x)
            if (drawn(x, y, line[x]))
                ++histogram[colorKey(line[x])];
    }
    const QList<QRgb> palette = histogram.isEmpty() ? QList<QRgb>() : quantize(histogram, 255);
    // The smallest table that holds the palette and the transparent index
    const int transparent = int(palette.size());
    int bits = 1;
    while ((1 << bits) < transparent + 1)
        ++bits;

    std::vector<qint16> nearest(32768, -1);
    QByteArray indices;
    indices.reserve(qsizetype(r.width()) * r.height());
    for (int y = r.top(); y <= r.bottom(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = r.left(); x <= r.right(); ++x) {
            const QRgb px = line[x];
            if (!drawn(x, y, px)) {
                indices.append(static_cast<char>(quint8(transparent)));
                continue;
            }
            const quint16 key = colorKey(px);
            if (nearest[key] < 0) {
                int best = 0;
                long bestDistance = -1;
                for (int i = 0; i < palette.size(); ++i) {
                    const long dr = qRed(palette[i]) - qRed(px);
                    const long dg = qGreen(palette[i]) - qGreen(px);
                    const long db = qBlue(palette[i]) - qBlue(px);
                    const long d = dr * dr + dg * dg + db * db;
                    if (bestDistance < 0 || d < bestDistance) {
                        bestDistance = d;
                        best = i;
                    }
                }
                nearest[key] = qint16(best);
            }
            indices.append(char(nearest[key]));
        }
    }

    QByteArray out;
    // Graphic control: disposal, transparent index, delay
    out.append("\x21\xF9\x04", 3);
    out.append(char((plan.disposal << 2) | 1));
    appendLe16(out, int(std::lround(delayMs / 10.0)));
    out.append(static_cast<char>(quint8(transparent)));
    out.append('\0');
    // Image descriptor with a local colour table
    out.append('\x2C');
    appendLe16(out, r.left());
    appendLe16(out, r.top());
    appendLe16(out, r.width());
    appendLe16(out, r.height());
    out.append(static_cast<char>(quint8(0x80 | (bits - 1))));
    for (int i = 0; i < (1 << bits); ++i) {
        const QRgb c = i < palette.size() ? palette[i] : qRgb(0, 0, 0);
        out.append(char(qRed(c))).append(char(qGreen(c))).append(char(qBlue(c)));
    }
    const int minCodeSize = qMax(2, bits);
    out.append(char(minCodeSize));
    out += lzw(indices, minCodeSize);
    return out;
}

} // namespace

Animation parse(const QByteArray &png)
{
    Animation anim;
    if (!png.startsWith(PngSignature))
        return anim;

    struct Chunk
    {
        QByteArray type;
        qsizetype offset; // of the length field
        quint32 length;
    };
    QList<Chunk> chunks;
    for (qsizetype off = 8; off + 12 <= png.size();) {
        const quint32 length = readDWord(png, off);
        if (off + 12 + qsizetype(length) > png.size())
            break;
        chunks.append({ png.mid(off + 4, 4), off, length });
        if (chunks.last().type == "IEND")
            break;
        off += 12 + qsizetype(length);
    }
    if (std::none_of(chunks.begin(), chunks.end(), [](const Chunk &c) { return c.type == "acTL"; }))
        return anim;

    QByteArray header;
    QByteArray pre, post;
    struct RawFrame
    {
        Frame frame;
        QList<QByteArray> dataParts;
    };
    QList<RawFrame> raw;
    for (const Chunk &c : std::as_const(chunks)) {
        const qsizetype data = c.offset + 8;
        if (c.type == "IHDR") {
            header = png.mid(data, c.length);
            anim.width = int(readDWord(png, data));
            anim.height = int(readDWord(png, data + 4));
        } else if (c.type == "acTL") {
            // numPlays is not needed: the GIF loops forever, as upstream's does
        } else if (c.type == "fcTL") {
            if (c.length < 26)
                return {};
            RawFrame f;
            f.frame.width = int(readDWord(png, data + 4));
            f.frame.height = int(readDWord(png, data + 8));
            f.frame.left = int(readDWord(png, data + 12));
            f.frame.top = int(readDWord(png, data + 16));
            const int delayN = readWord(png, data + 20);
            int delayD = readWord(png, data + 22);
            if (delayD == 0)
                delayD = 100;
            f.frame.delayMs = 1000.0 * delayN / delayD;
            if (f.frame.delayMs <= 10)
                f.frame.delayMs = 100;
            f.frame.disposeOp = byteAt(png, data + 24);
            f.frame.blendOp = byteAt(png, data + 25);
            raw.append(f);
        } else if (c.type == "fdAT") {
            if (!raw.isEmpty() && c.length >= 4)
                raw.last().dataParts.append(png.mid(data + 4, c.length - 4));
        } else if (c.type == "IDAT") {
            if (!raw.isEmpty())
                raw.last().dataParts.append(png.mid(data, c.length));
        } else if (c.type == "IEND") {
            post += png.mid(c.offset, 12 + c.length);
        } else {
            pre += png.mid(c.offset, 12 + c.length);
        }
    }
    if (raw.isEmpty() || header.size() < 8)
        return {};

    for (RawFrame &f : raw) {
        QByteArray ihdr = header;
        QByteArray size;
        appendDWord(size, quint32(f.frame.width));
        appendDWord(size, quint32(f.frame.height));
        ihdr.replace(0, 8, size);
        QByteArray single = PngSignature + makeChunk("IHDR", ihdr) + pre;
        for (const QByteArray &part : std::as_const(f.dataParts))
            single += makeChunk("IDAT", part);
        single += post.isEmpty() ? makeChunk("IEND", {}) : post;
        f.frame.image = QImage::fromData(single, "PNG");
        if (f.frame.image.isNull())
            return {};
        anim.frames.append(f.frame);
    }
    return anim;
}

QByteArray convert(const QByteArray &apng, int resolution)
{
    const Animation anim = parse(apng);
    if (anim.frames.isEmpty() || anim.width <= 0 || anim.height <= 0 || resolution <= 0)
        return {};

    // The animation is drawn at its own size and scaled onto the square
    // canvas, top left, as upstream's ctx.scale() does
    QImage canvas(anim.width, anim.height, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    const double scale = double(resolution) / std::max(anim.width, anim.height);

    QList<QImage> frames;
    QList<double> delays;
    for (const Frame &frame : anim.frames) {
        const QRect rect(frame.left, frame.top, frame.width, frame.height);
        const QImage previous = canvas.copy(rect);
        {
            QPainter p(&canvas);
            if (frame.blendOp == 0) {
                p.setCompositionMode(QPainter::CompositionMode_Source);
                p.fillRect(rect, Qt::transparent);
                p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            }
            p.drawImage(rect.topLeft(), frame.image);
        }

        QImage out(resolution, resolution, QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::transparent);
        {
            QPainter p(&out);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.scale(scale, scale);
            p.drawImage(0, 0, canvas);
        }
        frames.append(out);
        delays.append(frame.delayMs);

        QPainter p(&canvas);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        if (frame.disposeOp == 1)
            p.fillRect(rect, Qt::transparent);
        else if (frame.disposeOp == 2)
            p.drawImage(rect.topLeft(), previous);
    }
    return encodeGif(frames, delays);
}

QByteArray encodeGif(const QList<QImage> &frames, const QList<double> &delaysMs)
{
    if (frames.isEmpty())
        return {};
    const int width = frames.first().width();
    const int height = frames.first().height();
    if (width <= 0 || height <= 0 || width > 0xFFFF || height > 0xFFFF)
        return {};

    QVector<QImage> images(frames.size()); // sized up front: filled by index on several threads
    parallelFor(frames.size(), [&](qsizetype i) { images[i] = frames[i].convertToFormat(QImage::Format_ARGB32); });
    // Qord: a frame like the one before only adds its time to it
    QList<QImage> kept;
    QList<double> times;
    for (qsizetype i = 0; i < images.size(); ++i) {
        if (images[i].width() != width || images[i].height() != height)
            return {};
        const double delay = delaysMs.value(i, 100);
        if (!kept.isEmpty() && images[i] == kept.last()) {
            times.last() += delay;
            continue;
        }
        kept.append(images[i]);
        times.append(delay);
    }
    images.clear();

    // Qord: a frame after the first draws only the rectangle that changed,
    // over the frame before (kept: disposal 1), with what didn't change in
    // it transparent unless most of it did.
    // A pixel turning transparent can't be drawn that way, so then the
    // frame before is cleared (disposal 2) and this one drawn whole.
    const qsizetype count = kept.size();
    QVector<FramePlan> plans(count);
    parallelFor(count, [&](qsizetype i) {
        FramePlan &plan = plans[i];
        plan.rect = QRect(0, 0, width, height);
        if (i == 0)
            return;
        int minX = width, minY = height, maxX = -1, maxY = -1;
        qint64 changed = 0;
        for (int y = 0; y < height; ++y) {
            const auto *now = reinterpret_cast<const QRgb *>(kept[i].constScanLine(y));
            const auto *before = reinterpret_cast<const QRgb *>(kept[i - 1].constScanLine(y));
            for (int x = 0; x < width; ++x) {
                const bool opaque = qAlpha(now[x]) >= 128;
                const bool wasOpaque = qAlpha(before[x]) >= 128;
                if (!opaque && wasOpaque)
                    return;
                if (opaque != wasOpaque || (opaque && colorKey(now[x]) != colorKey(before[x]))) {
                    ++changed;
                    minX = qMin(minX, x);
                    maxX = qMax(maxX, x);
                    minY = qMin(minY, y);
                    maxY = qMax(maxY, y);
                }
            }
        }
        plan.delta = true;
        plan.rect = maxX < 0 ? QRect(0, 0, 1, 1) : QRect(QPoint(minX, minY), QPoint(maxX, maxY));
        // Scattered transparent pixels in a mostly changed area cost more
        // than they save
        plan.sparse = changed < qint64(plan.rect.width()) * plan.rect.height() * 3 / 4;
    });
    for (qsizetype i = 0; i < count; ++i)
        plans[i].disposal = i + 1 < count && plans[i + 1].delta ? 1 : 2;

    // Each frame quantized and compressed on its own thread, joined in order
    QVector<QByteArray> blocks(count);
    parallelFor(count, [&](qsizetype i) {
        blocks[i] = encodeFrame(kept[i], i ? &kept[i - 1] : nullptr, plans[i], times[i]);
    });

    QByteArray gif("GIF89a");
    appendLe16(gif, width);
    appendLe16(gif, height);
    gif.append('\0'); // no global colour table
    gif.append('\0'); // background
    gif.append('\0'); // aspect
    // Loop forever (NETSCAPE2.0)
    gif.append("\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00", 19);
    for (const QByteArray &block : std::as_const(blocks))
        gif += block;
    gif.append('\x3B');
    return gif;
}

QList<QImage> scaledFrames(const QList<QImage> &frames, const QSize &size)
{
    QVector<QImage> scaled(frames.size());
    parallelFor(frames.size(), [&](qsizetype i) {
        scaled[i] = frames[i].size() == size ? frames[i]
                                             : frames[i].scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    });
    return QList<QImage>(scaled.begin(), scaled.end());
}

} // namespace ApngToGif
} // namespace Plugins
} // namespace Qord
