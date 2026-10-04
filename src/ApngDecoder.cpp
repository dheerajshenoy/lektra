#include "ApngDecoder.hpp"

#include <QFile>
#include <QPainter>
#include <array>

namespace
{
const QByteArray kSignature("\x89PNG\r\n\x1a\n", 8);

quint32
be32(const char *p)
{
    return (static_cast<quint32>(static_cast<uchar>(p[0])) << 24)
           | (static_cast<quint32>(static_cast<uchar>(p[1])) << 16)
           | (static_cast<quint32>(static_cast<uchar>(p[2])) << 8)
           | static_cast<quint32>(static_cast<uchar>(p[3]));
}

quint16
be16(const char *p)
{
    return static_cast<quint16>((static_cast<uchar>(p[0]) << 8)
                                | static_cast<uchar>(p[1]));
}

void
append32(QByteArray &out, quint32 v)
{
    out.append(static_cast<char>(v >> 24));
    out.append(static_cast<char>(v >> 16));
    out.append(static_cast<char>(v >> 8));
    out.append(static_cast<char>(v));
}

quint32
crc32(const char *data, qsizetype size, quint32 crc = 0)
{
    static const std::array<quint32, 256> table = []
    {
        std::array<quint32, 256> t{};
        for (quint32 n = 0; n < 256; ++n)
        {
            quint32 c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (qsizetype i = 0; i < size; ++i)
        crc = table[(crc ^ static_cast<uchar>(data[i])) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

// A chunk: length, type, data, and the CRC over type and data.
void
appendChunk(QByteArray &out, const char type[4], const QByteArray &data)
{
    append32(out, static_cast<quint32>(data.size()));
    out.append(type, 4);
    out.append(data);
    quint32 crc = crc32(type, 4);
    crc         = crc32(data.constData(), data.size(), crc);
    append32(out, crc);
}

struct Chunk
{
    QByteArray type;
    qsizetype dataStart = 0;
    qsizetype dataSize  = 0;
    qsizetype start     = 0; // of the length field
    qsizetype end       = 0; // after the CRC
};

// The chunks of a PNG, or none if it does not look like one.
QList<Chunk>
chunksOf(const QByteArray &png)
{
    QList<Chunk> chunks;
    if (!png.startsWith(kSignature))
        return chunks;
    qsizetype pos = kSignature.size();
    while (pos + 12 <= png.size())
    {
        const quint32 length = be32(png.constData() + pos);
        if (length > 0x7FFFFFFFu || pos + 12 + static_cast<qsizetype>(length) > png.size())
            break;
        Chunk c;
        c.start     = pos;
        c.type      = png.mid(pos + 4, 4);
        c.dataStart = pos + 8;
        c.dataSize  = length;
        c.end       = pos + 12 + length;
        chunks.append(c);
        pos = c.end;
        if (c.type == "IEND")
            break;
    }
    return chunks;
}
} // namespace

bool
ApngDecoder::isAnimated(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.read(kSignature.size()) != kSignature)
        return false;
    // The animation control chunk comes before the image data, so only the
    // chunk headers up to there are read.
    for (int i = 0; i < 64; ++i)
    {
        const QByteArray head = file.read(8); // length and type
        if (head.size() != 8)
            return false;
        const quint32 length = be32(head.constData());
        const QByteArray type = head.mid(4, 4);
        if (type == "acTL")
        {
            const QByteArray data = file.read(8);
            return data.size() == 8 && be32(data.constData()) > 1;
        }
        if (type == "IDAT" || type == "IEND")
            return false;
        if (!file.seek(file.pos() + length + 4)) // the data and the CRC
            return false;
    }
    return false;
}

std::unique_ptr<ApngDecoder>
ApngDecoder::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return nullptr;
    return fromData(file.readAll());
}

std::unique_ptr<ApngDecoder>
ApngDecoder::fromData(const QByteArray &png)
{
    const QList<Chunk> chunks = chunksOf(png);
    if (chunks.isEmpty() || chunks.first().type != "IHDR"
        || chunks.first().dataSize != 13)
        return nullptr;

    std::unique_ptr<ApngDecoder> d(new ApngDecoder);
    const char *base = png.constData();
    d->m_ihdr        = png.mid(chunks.first().dataStart, 13);
    d->m_size        = QSize(static_cast<int>(be32(base + chunks.first().dataStart)),
                             static_cast<int>(be32(base + chunks.first().dataStart + 4)));
    if (d->m_size.isEmpty() || d->m_size.width() > 65535 || d->m_size.height() > 65535)
        return nullptr;

    bool sawAnimation = false;
    bool sawData      = false;
    for (qsizetype i = 1; i < chunks.size(); ++i)
    {
        const Chunk &c = chunks.at(i);
        const char *p  = base + c.dataStart;
        if (c.type == "acTL")
        {
            if (c.dataSize < 8)
                return nullptr;
            sawAnimation = true;
            d->m_plays   = static_cast<int>(be32(p + 4));
        }
        else if (c.type == "fcTL")
        {
            if (c.dataSize < 26)
                return nullptr;
            Frame f;
            f.rect = QRect(static_cast<int>(be32(p + 12)), static_cast<int>(be32(p + 16)),
                           static_cast<int>(be32(p + 4)), static_cast<int>(be32(p + 8)));
            const int num = be16(p + 20);
            const int den = be16(p + 22) == 0 ? 100 : be16(p + 22);
            f.delayMs     = 1000 * num / den;
            if (f.delayMs <= 10)
                f.delayMs = 100; // like browsers: a delay of 0 is too fast to mean it
            f.dispose = static_cast<uchar>(p[24]);
            f.blend   = static_cast<uchar>(p[25]);
            d->m_frames.append(f);
        }
        else if (c.type == "IDAT")
        {
            sawData = true;
            // Part of the animation only if a frame control chunk came first;
            // otherwise it is a picture for readers that cannot animate.
            if (!d->m_frames.isEmpty())
                d->m_frames.last().data.append(p, static_cast<qsizetype>(c.dataSize));
        }
        else if (c.type == "fdAT")
        {
            if (c.dataSize < 4 || d->m_frames.isEmpty())
                return nullptr;
            d->m_frames.last().data.append(p + 4, static_cast<qsizetype>(c.dataSize - 4));
        }
        else if (!sawData && c.type != "IEND")
        {
            // palette, transparency, colour space ...: every frame needs them
            d->m_prelude.append(png.mid(c.start, c.end - c.start));
        }
    }

    if (!sawAnimation || d->m_frames.size() < 2)
        return nullptr;
    for (const Frame &f : std::as_const(d->m_frames))
        if (f.data.isEmpty() || f.rect.isEmpty()
            || !QRect(QPoint(0, 0), d->m_size).contains(f.rect))
            return nullptr;
    d->reset();
    return d;
}

void
ApngDecoder::reset()
{
    m_index  = -1;
    m_canvas = QImage(m_size, QImage::Format_ARGB32_Premultiplied);
    m_canvas.fill(Qt::transparent);
    m_saved = QImage();
}

// One frame as a PNG of its own, which Qt can decode.
QImage
ApngDecoder::decode(const Frame &frame) const
{
    QByteArray png = kSignature;
    QByteArray ihdr = m_ihdr;
    // the header says how big the frame is, not the whole picture
    QByteArray size;
    append32(size, static_cast<quint32>(frame.rect.width()));
    append32(size, static_cast<quint32>(frame.rect.height()));
    ihdr.replace(0, 8, size);
    appendChunk(png, "IHDR", ihdr);
    png.append(m_prelude);
    appendChunk(png, "IDAT", frame.data);
    appendChunk(png, "IEND", QByteArray());

    QImage image;
    image.loadFromData(png, "PNG");
    if (image.isNull())
        return {};
    return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QImage
ApngDecoder::nextFrame(int *delayMs, bool *wrapped)
{
    if (m_frames.isEmpty())
        return {};

    // What the previous frame asked for happens before the next one is drawn.
    if (m_index >= 0)
    {
        const Frame &previous = m_frames.at(m_index);
        if (previous.dispose == 1)
        {
            QPainter p(&m_canvas);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.fillRect(previous.rect, Qt::transparent);
        }
        else if (previous.dispose == 2 && !m_saved.isNull())
        {
            QPainter p(&m_canvas);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.drawImage(previous.rect.topLeft(), m_saved);
        }
    }

    bool wrap = false;
    ++m_index;
    if (m_index >= m_frames.size())
    {
        m_index = 0;
        wrap    = true;
        m_canvas.fill(Qt::transparent); // a new round starts from nothing
    }
    if (wrapped)
        *wrapped = wrap;

    const Frame &frame = m_frames.at(m_index);
    // Bringing back "the previous canvas" needs it kept before drawing. On the
    // first frame there is nothing before it, so it is cleared instead.
    m_saved = (frame.dispose == 2 && m_index > 0) ? m_canvas.copy(frame.rect)
                                                  : QImage();

    const QImage image = decode(frame);
    if (image.isNull())
        return {};

    {
        QPainter p(&m_canvas);
        p.setCompositionMode(frame.blend == 1 ? QPainter::CompositionMode_SourceOver
                                              : QPainter::CompositionMode_Source);
        p.drawImage(frame.rect.topLeft(), image);
    }

    if (frame.dispose == 2 && m_index == 0)
    {
        // "previous" with nothing before: treated as "background"
        m_saved = QImage(frame.rect.size(), QImage::Format_ARGB32_Premultiplied);
        m_saved.fill(Qt::transparent);
    }

    if (delayMs)
        *delayMs = frame.delayMs;
    return m_canvas.copy();
}
