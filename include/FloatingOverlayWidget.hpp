#pragma once

#include <QEvent>
#include <QFrame>
#include <QHideEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QWidget>

class FloatingOverlayWidget : public QWidget
{
    Q_OBJECT

public:
    struct FrameStyle
    {
        bool border{true};
        bool shadow{true};
        int shadow_blur_radius{18};
        int shadow_offset_x{0};
        int shadow_offset_y{6};
        int shadow_opacity{120};
    };

    explicit FloatingOverlayWidget(QWidget *parent = nullptr);
    void setContentWidget(QWidget *widget) noexcept;
    QWidget *contentWidget() const noexcept;
    void setFrameStyle(const FrameStyle &style) noexcept;

signals:
    void overlayHidden();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void applyFrameStyle() noexcept;

    QFrame *m_frame{nullptr};
    QWidget *m_content{nullptr};
    FrameStyle m_frame_style{};
    class QGraphicsDropShadowEffect *m_shadow_effect{nullptr};
};
