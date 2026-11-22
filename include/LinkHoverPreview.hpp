#pragma once

#include <QApplication>
#include <QFrame>
#include <QGuiApplication>
#include <QLabel>
#include <QPixmap>
#include <QScreen>
#include <QVBoxLayout>

// Small floating preview shown next to the cursor while hovering an internal
// link: a picture of what the link points at, with a caption. It never takes
// the mouse or the focus, so showing it doesn't end the hover that opened it.
class LinkHoverPreview : public QFrame
{
public:
    explicit LinkHoverPreview(QWidget *parent = nullptr)
        : QFrame(parent, Qt::ToolTip | Qt::FramelessWindowHint)
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(2, 2, 2, 2);
        layout->setSpacing(2);

        m_image = new QLabel(this);
        m_image->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_image);

        m_caption = new QLabel(this);
        m_caption->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_caption);

        setFrameShape(QFrame::StyledPanel);
        applyStyle(8, 1.0);
    }

    // Border radius in pixels, opacity 0..1.
    void applyStyle(int borderRadius, qreal opacity) noexcept
    {
        setStyleSheet(QString("LinkHoverPreview { background: palette(window); "
                              "border: 1px solid palette(mid); "
                              "border-radius: %1px; }")
                          .arg(borderRadius));
        setWindowOpacity(opacity);
    }

    void showPreview(const QPixmap &pixmap, const QString &caption,
                     const QPoint &globalPos) noexcept
    {
        m_image->setPixmap(pixmap);
        m_caption->setText(caption);
        m_caption->setVisible(!caption.isEmpty());
        adjustSize();

        // The screen the pointer is on.
        QRect area;
        if (QScreen *screen = QGuiApplication::screenAt(globalPos))
            area = screen->availableGeometry();
        else if (QScreen *primary = QGuiApplication::primaryScreen())
            area = primary->availableGeometry();

        // Centred under the pointer, clear of it (the hand cursor reaches
        // about 24 px below its hot spot) and of the link text: a gap below,
        // or above when there is no room below. It only moves sideways as far
        // as the screen needs.
        constexpr int kGap = 34;
        QPoint pos(globalPos.x() - width() / 2, globalPos.y() + kGap);
        if (!area.isNull())
        {
            if (pos.y() + height() > area.bottom())
                pos.setY(globalPos.y() - height() - kGap);
            pos.setX(qBound(area.left(), pos.x(),
                            qMax(area.left(), area.right() - width())));
            pos.setY(qBound(area.top(), pos.y(),
                            qMax(area.top(), area.bottom() - height())));
        }
        move(pos);
        show();
        raise();
    }

private:
    QLabel *m_image   = nullptr;
    QLabel *m_caption = nullptr;
};
