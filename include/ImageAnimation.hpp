#pragma once

#include <QImage>
#include <QObject>
#include <QTimer>
#include <memory>

class ApngDecoder;
class QMovie;

// Plays an animated image, one frame at a time: GIF and animated WebP through
// QMovie, and APNG through ApngDecoder (Qt's own PNG reader only gives the
// first frame). Both are used the same way.
class ImageAnimation : public QObject
{
    Q_OBJECT
public:
    // Opens the file; null if it cannot be played.
    static ImageAnimation *open(const QString &path, QObject *parent = nullptr);
    // Whether the file is an APNG with several frames. (For GIF and WebP,
    // QImageReader::imageCount() says so.)
    static bool isApng(const QString &path);

    ~ImageAnimation() override;

    bool isRunning() const;
    void start();
    void stop();
    // The frame that is showing now.
    QImage currentImage() const;

signals:
    // A new frame is showing.
    void frameChanged();

private:
    explicit ImageAnimation(QObject *parent);
    void advance();

    QMovie *m_movie = nullptr;
    std::unique_ptr<ApngDecoder> m_apng;
    QTimer m_timer;
    QImage m_frame;
    int m_delayMs    = 100;
    int m_roundsDone = 0;
    bool m_running   = false;
    bool m_finished  = false; // an APNG that plays a set number of times has
                              // played them all
};
