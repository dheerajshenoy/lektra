#pragma once

#include <QColor>
#include <QWidget>

// Color eyedropper. While active, a small round window follows the mouse
// pointer and shows the color under it (as seen in the view) and its hex
// value. A left click picks the color, Escape cancels. Like LaserTrail it
// never takes events itself: it watches the application's events and swallows
// the clicks on the document views.
class ColorEyedrop : public QWidget
{
    Q_OBJECT
public:
    explicit ColorEyedrop(QWidget *parent);

    void setActive(bool active);
    inline bool isActive() const noexcept
    {
        return m_active;
    }

signals:
    // The user clicked; the eyedropper is already off.
    void picked(const QColor &color);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    QColor sample(QWidget *viewport, const QPoint &globalPos) const;
    void follow(const QPoint &globalPos);

    bool m_active = false;
    QColor m_color;
};
