#pragma once

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QRect>
#include <QString>
#include <memory>

// Decodes animated PNG (APNG). Qt's PNG reader only gives the first frame, so
// the animation chunks (acTL, fcTL, fdAT) are read here. Every frame is turned
// into an ordinary PNG and decoded by Qt, then drawn onto a canvas following
// the file's dispose and blend rules, one frame at a time: only the compressed
// data of all frames is kept in memory.
class ApngDecoder
{
public:
    // Reads the file. Returns null if it is not an APNG with at least two
    // frames.
    static std::unique_ptr<ApngDecoder> load(const QString &path);
    // The same for data in memory.
    static std::unique_ptr<ApngDecoder> fromData(const QByteArray &data);
    // Whether the file is an APNG with at least two frames, without decoding
    // anything (reads only the chunk headers).
    static bool isAnimated(const QString &path);

    int frameCount() const
    {
        return static_cast<int>(m_frames.size());
    }
    // How many times the animation plays; 0 means for ever.
    int playCount() const
    {
        return m_plays;
    }
    QSize size() const
    {
        return m_size;
    }

    // Starts again from the first frame.
    void reset();
    // Draws the next frame onto the canvas and returns the whole canvas (a
    // copy). After the last frame it goes back to the first. `delayMs` is how
    // long this frame is shown; `wrapped` is set when this is the first frame
    // of another round. Returns a null image if a frame cannot be decoded.
    QImage nextFrame(int *delayMs = nullptr, bool *wrapped = nullptr);

private:
    struct Frame
    {
        QRect rect;
        int delayMs = 100;
        int dispose = 0; // 0 none, 1 background, 2 previous
        int blend   = 0; // 0 source, 1 over
        QByteArray data; // the compressed image data
    };

    ApngDecoder() = default;
    QImage decode(const Frame &frame) const;

    QSize m_size;
    int m_plays = 0;
    QByteArray m_ihdr; // the 13 bytes of the header
    QByteArray
        m_prelude; // chunks between IHDR and the image data (palette, ...)
    QList<Frame> m_frames;

    QImage m_canvas;
    QImage m_saved; // the part of the canvas to bring back (dispose "previous")
    int m_index = -1;
};
