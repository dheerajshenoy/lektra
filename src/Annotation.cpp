#include "Annotation.hpp"

#include "utils.hpp"

#include <QGuiApplication>
#include <QMenu>
#include <QPainter>
#include <QScreen>

Annotation::Annotation(const int index, const QColor &color,
                       QGraphicsItem *parent)
    : QObject(), QGraphicsItem(parent), m_index(index), m_brush(color),
      m_originalBrush(m_brush), m_originalPen(m_pen)
{
    setAcceptHoverEvents(true);
    setFlags(QGraphicsItem::ItemIsSelectable);
}

Annotation::~Annotation()
{
    // m_tooltip has no Qt parent so we own it — delete explicitly.
    delete m_tooltip;
}

void
Annotation::restoreBrushPen() noexcept
{
    m_brush = m_originalBrush;
    m_pen   = m_originalPen;
    update();
}

void
Annotation::setGlowEnabled(bool enable) noexcept
{
    if (m_glow_enabled == enable)
        return;
    m_glow_enabled = enable;
    update();
}

void
Annotation::setGlowWidth(int width) noexcept
{
    if (m_glow_width == width)
        return;
    m_glow_width = width;
    update();
}

void
Annotation::setGlowColor(uint32_t rgba) noexcept
{
    m_glow_color = rgbaToQColor(rgba);
    if (m_glow_enabled)
        update();
}

void
Annotation::setComment(const QString &comment)
{
    m_comment = comment;
    if (m_tooltip)
        m_tooltip->setText(comment);
}

QRectF
Annotation::boundingRect() const
{
    return QRectF{};
}

void
Annotation::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                  QWidget * /*widget*/)
{
    const QRectF rect = boundingRect();

    // Use m_hovered rather than State_MouseOver. The style flag is cleared
    // the moment a context menu opens (Qt steals the mouse), but m_hovered
    // stays true for the duration of the menu — see contextMenuEvent.
    if (m_hovered && m_glow_enabled)
    {
        painter->save();
        drawGlow(painter, rect, m_glow_width);
        painter->restore();
    }

    painter->setPen(m_pen);
    painter->setBrush(m_brush);
    painter->drawRect(rect);

    Q_UNUSED(option);
}

void
Annotation::drawGlow(QPainter *painter, const QRectF &rect,
                     qreal width) const noexcept
{
    // Outer glow: expand the rect outward by the full width so the stroke
    // sits entirely outside the annotation border.
    // A single wide, semi-transparent pen with antialiasing gives a smooth
    // halo in one draw call — no loops, no layering.
    QColor glowColor = m_glow_color;
    glowColor.setAlphaF(glowColor.alphaF() * 0.75);

    QPen pen(glowColor, width);
    pen.setJoinStyle(Qt::MiterJoin);

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    // Expand outward by half the pen width so the outer edge of the stroke
    // aligns with rect + width, keeping it fully outside the annotation.
    const qreal half = width * 0.5;
    painter->drawRect(rect.adjusted(-half, -half, half, half));
}

void
Annotation::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
    m_hovered = true;
    update();
    showTooltip(event->screenPos());
    QGraphicsItem::hoverEnterEvent(event);
}

void
Annotation::hoverMoveEvent(QGraphicsSceneHoverEvent *event)
{
    moveTooltip(event->screenPos());
    QGraphicsItem::hoverMoveEvent(event);
}

void
Annotation::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    // If a context menu is open, Qt fires hoverLeaveEvent because the menu
    // window grabs the mouse. We must not clear m_hovered in that case —
    // the glow should remain visible while the user interacts with the menu.
    if (!m_context_menu_open)
    {
        m_hovered = false;
        update();
        hideTooltip();
    }

    QGraphicsItem::hoverLeaveEvent(event);
}

void
Annotation::contextMenuEvent(QGraphicsSceneContextMenuEvent *event)
{
    // Signal that we are inside a menu so hoverLeaveEvent does not clear
    // m_hovered. menu.exec() is synchronous and blocks until the menu closes,
    // so clearing the flag immediately after is safe.
    m_context_menu_open = true;

    // Base implementation is a no-op — subclasses build and exec their menus
    // here, or call this and let HighlightAnnotation etc. override fully.
    // The flag bracketing must wrap whatever exec() call is made, so subclasses
    // that override this should call Annotation::contextMenuEvent or manage
    // m_context_menu_open themselves.

    event->ignore(); // let subclass handle if it overrides

    m_context_menu_open = false;

    // If the cursor left the item while the menu was open, honour that now.
    if (!isUnderMouse())
    {
        m_hovered = false;
        hideTooltip();
        update();
    }
}

void
Annotation::showTooltip(const QPoint &screenPos)
{
    if (m_comment.isEmpty())
        return;

    if (!m_tooltip)
    {
        m_tooltip = new QLabel(m_comment);
        m_tooltip->setWordWrap(true);
        m_tooltip->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
        m_tooltip->setAttribute(Qt::WA_TranslucentBackground, false);
        m_tooltip->setStyleSheet("QLabel {"
                                 "  padding: 4px 6px;"
                                 "  border-radius: 3px;"
                                 "}");
        m_tooltip->adjustSize();

        if (m_tooltip_font_size > 0)
        {
            QFont font = m_tooltip->font();
            font.setPointSize(m_tooltip_font_size);
            m_tooltip->setFont(font);
            m_tooltip->adjustSize();
        }
    }

    QPoint pos          = screenPos + QPoint(12, 12);
    const QSize tipSize = m_tooltip->sizeHint();
    const QRect screen
        = QGuiApplication::screenAt(screenPos)
              ? QGuiApplication::screenAt(screenPos)->availableGeometry()
              : QGuiApplication::primaryScreen()->availableGeometry();

    // Flip horizontally if it would overflow the right edge
    if (pos.x() + tipSize.width() > screen.right())
        pos.setX(screenPos.x() - tipSize.width() - 12);

    // Flip vertically if it would overflow the bottom edge
    if (pos.y() + tipSize.height() > screen.bottom())
        pos.setY(screenPos.y() - tipSize.height() - 12);

    m_tooltip->move(screenPos + QPoint(12, 12));
    m_tooltip->show();
}

void
Annotation::moveTooltip(const QPoint &screenPos)
{
    if (m_tooltip && m_tooltip->isVisible())
        m_tooltip->move(screenPos + QPoint(12, 12));
}

void
Annotation::setTooltipFontSize(int pointSize)
{
    m_tooltip_font_size = pointSize;
    if (m_tooltip)
    {
        QFont font = m_tooltip->font();
        font.setPointSize(pointSize);
        m_tooltip->setFont(font);
        m_tooltip->adjustSize();
    }
}

void
Annotation::hideTooltip()
{
    if (m_tooltip)
        m_tooltip->hide();
}

void
Annotation::mousePressEvent(QGraphicsSceneMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
    {
        setSelected(true);
        emit annotSelectRequested();
    }
    QGraphicsItem::mousePressEvent(e);
}

void
Annotation::setCommentMarkerVisible(bool visible) noexcept
{
    if (visible && !m_comment_marker)
    {
        m_comment_marker = new CommentPopupButton(this);
        m_comment_marker->setAnnotationRect(boundingRect());
        connect(m_comment_marker, &CommentPopupButton::clicked, this,
                [this] { emit annotCommentRequested(); });
    }
    m_comment_marker_visible = visible;
    updateCommentMarker();
}
