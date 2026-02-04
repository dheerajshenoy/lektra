#pragma once

// This class represents a clickable link area in the document view

#include <QBrush>
#include <QDesktopServices>
#include <QGraphicsRectItem>
#include <QGraphicsSceneHoverEvent>
#include <QMenu>
#include <QPen>
#include <QUrl>
#include <qbytearrayview.h>
#include <qevent.h>
#include <qgraphicsitem.h>
#include <qnamespace.h>

class BrowseLinkItem : public QObject, public QGraphicsRectItem
{
    Q_OBJECT
public:
    struct PageLocation
    {
        float x, y, zoom;
    };

    enum class LinkType
    {
        Page = 0,
        Section,
        FitV,
        FitH,
        Location,
        External,
    };

    BrowseLinkItem(const QRectF &rect, const QString &link, LinkType type,
                   bool boundary = false, QGraphicsItem *parent = nullptr)
        : QGraphicsRectItem(rect, parent), _link(link), _type(type)
    {
        if (!boundary)
            setPen(Qt::NoPen);
        // setBrush(Qt::transparent);
        setAcceptHoverEvents(true);
        setToolTip(link);
        setAcceptedMouseButtons(Qt::AllButtons);
        setFlags(QGraphicsItem::ItemIsSelectable
                 | QGraphicsItem::ItemIsFocusable);
        setData(0, "link");
    }

    inline void setGotoPageNo(int pageno) noexcept
    {
        _pageno = pageno;
    }
    inline int gotoPageNo() noexcept
    {
        return _pageno;
    }
    inline void setTargetLocation(const PageLocation &loc) noexcept
    {
        _loc = loc;
    }
    inline PageLocation location() noexcept
    {
        return _loc;
    }

    inline void setSourceLocation(const PageLocation &loc) noexcept
    {
        _source_loc = loc;
    }
    inline PageLocation sourceLocation() const noexcept
    {
        return _source_loc;
    }

    inline void setURI(char *uri) noexcept
    {
        _uri = uri;
    }

    inline const char *URI() noexcept
    {
        return _uri;
    }

    inline void setLinkType(LinkType type) noexcept
    {
        _type = type;
    }

    inline LinkType linkType() noexcept
    {
        return _type;
    }
    inline const QString &link() const noexcept
    {
        return _link;
    }

signals:
    void jumpToPageRequested(int pageno, const PageLocation &sourceLoc);
    void jumpToLocationRequested(int pageno, const PageLocation &targetLoc,
                                 const PageLocation &sourceLoc);
    void verticalFitRequested(int pageno, const PageLocation &loc);
    void horizontalFitRequested(int pageno, const PageLocation &loc);
    void linkCopyRequested(const QString &link);

protected:
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton)
        {
            switch (_type)
            {
                case LinkType::Page:
                    if (_pageno)
                        emit jumpToPageRequested(_pageno, _source_loc);
                    break;

                case LinkType::Section:
                    emit jumpToLocationRequested(_pageno, _loc, _source_loc);
                    break;

                case LinkType::FitV:
                    emit verticalFitRequested(_pageno, _loc);
                    break;

                case LinkType::FitH:
                    emit horizontalFitRequested(_pageno, _loc);
                    break;

                case LinkType::Location:
                    emit jumpToLocationRequested(_pageno, _loc, _source_loc);
                    break;

                case LinkType::External:
                    QDesktopServices::openUrl(QUrl(_link));
                    break;
            }

            // setBrush(Qt::transparent);
        }
    }

    void hoverEnterEvent(QGraphicsSceneHoverEvent *e) override
    {
        setBrush(QBrush(QColor(1.0, 1.0, 0.0, 125)));
        setCursor(Qt::PointingHandCursor);
        QGraphicsRectItem::hoverEnterEvent(e);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent *e) override
    {
        setBrush(Qt::transparent);
        unsetCursor();
        QGraphicsRectItem::hoverLeaveEvent(e);
    }

    void contextMenuEvent(QGraphicsSceneContextMenuEvent *e) override
    {
        QMenu menu;
        QAction *copyLinkLocationAction = menu.addAction("Copy Link Location");

        connect(copyLinkLocationAction, &QAction::triggered, this,
                [this]() { emit linkCopyRequested(_link); });

        menu.exec(e->screenPos());
        e->accept();
    }

private:
    PageLocation _loc{0, 0, 0};
    PageLocation _source_loc{0, 0, 0};
    int _pageno{-1};
    QString _link;
    LinkType _type;
    char *_uri{nullptr};
};
