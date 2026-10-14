#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <memory>

// JPEG XL decoding through libjxl, which is loaded at run time (see DynLib),
// so lektra neither links to it nor needs its headers to build.
class JxlAnimation
{
public:
    // Opens the file; null if libjxl is missing or the file is not JPEG XL.
    static std::unique_ptr<JxlAnimation> load(const QString &path);
    // Whether the file is an animation (reads only its header).
    static bool isAnimated(const QString &path);
    // The first frame as 8-bit RGBA; null on failure.
    static QImage decodeFirstFrame(const QString &path);

    ~JxlAnimation();
    JxlAnimation(const JxlAnimation &)            = delete;
    JxlAnimation &operator=(const JxlAnimation &) = delete;

    // The next frame, fully composited. After the last frame it starts again
    // from the first and sets *wrapped. *delayMs is how long to show it.
    QImage nextFrame(int *delayMs = nullptr, bool *wrapped = nullptr);
    // How many times the file asks to be played (0 is for ever).
    int playCount() const { return m_loops; }

private:
    JxlAnimation() = default;
    bool start() noexcept;
    void stop() noexcept;

    QByteArray m_data;
    void *m_dec    = nullptr;
    void *m_runner = nullptr;
    uint32_t m_w = 0, m_h = 0;
    uint32_t m_tps_num = 1, m_tps_den = 1;
    int m_loops = 0;
};
