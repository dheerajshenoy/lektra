#include "ImageAnimation.hpp"

#include "ApngDecoder.hpp"

#include <QMovie>

ImageAnimation::ImageAnimation(QObject *parent) : QObject(parent) {}

ImageAnimation *
ImageAnimation::open(const QString &path, QObject *parent)
{
    auto *animation = new ImageAnimation(parent);

    if (auto apng = ApngDecoder::load(path))
    {
        animation->m_apng = std::move(apng);
        animation->m_timer.setSingleShot(true);
        connect(&animation->m_timer, &QTimer::timeout, animation,
                &ImageAnimation::advance);
        // The first frame is there from the start, so it can be drawn before
        // the animation is started.
        animation->m_frame
            = animation->m_apng->nextFrame(&animation->m_delayMs);
        if (animation->m_frame.isNull())
        {
            delete animation;
            return nullptr;
        }
        return animation;
    }

    animation->m_movie = new QMovie(path, QByteArray(), animation);
    if (!animation->m_movie->isValid())
    {
        delete animation;
        return nullptr;
    }
    // Decode one frame at a time instead of keeping them all.
    animation->m_movie->setCacheMode(QMovie::CacheNone);
    connect(animation->m_movie, &QMovie::frameChanged, animation,
            [animation](int) { emit animation->frameChanged(); });
    return animation;
}

bool
ImageAnimation::isApng(const QString &path)
{
    return ApngDecoder::isAnimated(path);
}

ImageAnimation::~ImageAnimation() = default;

bool
ImageAnimation::isRunning() const
{
    return m_movie ? m_movie->state() == QMovie::Running : m_running;
}

void
ImageAnimation::start()
{
    if (m_movie)
    {
        m_movie->start();
        return;
    }
    if (m_running || m_finished)
        return;
    m_running = true;
    m_timer.start(m_delayMs);
}

void
ImageAnimation::stop()
{
    if (m_movie)
    {
        m_movie->stop();
        return;
    }
    m_running = false;
    m_timer.stop();
}

QImage
ImageAnimation::currentImage() const
{
    return m_movie ? m_movie->currentImage() : m_frame;
}

// The timer of an APNG: show the next frame and wait for as long as it asks.
void
ImageAnimation::advance()
{
    if (!m_running || !m_apng)
        return;

    bool wrapped       = false;
    int delay          = m_delayMs;
    const QImage frame = m_apng->nextFrame(&delay, &wrapped);
    if (frame.isNull())
    {
        stop();
        return;
    }

    if (wrapped)
    {
        // A round has been played. A file can ask for a set number of rounds
        // (0 is for ever); after the last one the animation stays on its last
        // frame, which is still the one showing.
        ++m_roundsDone;
        if (m_apng->playCount() > 0 && m_roundsDone >= m_apng->playCount())
        {
            m_finished = true;
            stop();
            return;
        }
    }

    m_frame   = frame;
    m_delayMs = delay;
    emit frameChanged();
    if (m_running)
        m_timer.start(m_delayMs);
}
