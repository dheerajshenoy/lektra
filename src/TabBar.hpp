#pragma once

#include <QApplication>
#include <QDrag>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QStyle>
#include <QStyleOptionTab>
#include <QTabBar>

class TabBar : public QTabBar
{
    Q_OBJECT

public:
    static constexpr const char *MIME_TYPE = "application/lektra-tab";

    explicit TabBar(QWidget *parent = nullptr) : QTabBar(parent)
    {
        setAcceptDrops(true);
        setElideMode(Qt::TextElideMode::ElideRight);
        setDrawBase(false);
        setMovable(true);
    }

    struct TabData
    {
        QString filePath;
        int currentPage{1};
        double zoom{1.0};
        bool invertColor{false};
        int rotation{0};
        int fitMode{0};

        QByteArray serialize() const noexcept
        {
            QJsonObject obj;
            obj["file_path"]    = filePath;
            obj["current_page"] = currentPage;
            obj["zoom"]         = zoom;
            obj["invert_color"] = invertColor;
            obj["rotation"]     = rotation;
            obj["fit_mode"]     = fitMode;
            return QJsonDocument(obj).toJson(QJsonDocument::Compact);
        }

        static TabData deserialize(const QByteArray &data) noexcept
        {
            TabData result;
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isObject())
                return result;

            QJsonObject obj    = doc.object();
            result.filePath    = obj["file_path"].toString();
            result.currentPage = obj["current_page"].toInt(1);
            result.zoom        = obj["zoom"].toDouble(1.0);
            result.invertColor = obj["invert_color"].toBool(false);
            result.rotation    = obj["rotation"].toInt(0);
            result.fitMode     = obj["fit_mode"].toInt(0);
            return result;
        }
    };

signals:
    void tabDataRequested(int index, TabData *outData);
    void tabDropReceived(const TabData &data);
    void tabDetached(int index, const QPoint &globalPos);
    void tabDetachedToNewWindow(int index, const TabData &data);

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            m_drag_start_pos = event->pos();
            m_drag_tab_index = tabAt(event->pos());
        }
        QTabBar::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!(event->buttons() & Qt::LeftButton) || m_drag_tab_index < 0)
        {
            QTabBar::mouseMoveEvent(event);
            return;
        }

        // Check if we've moved enough to start a drag
        if ((event->pos() - m_drag_start_pos).manhattanLength()
            < QApplication::startDragDistance())
        {
            QTabBar::mouseMoveEvent(event);
            return;
        }

        // Request tab data from the parent widget
        TabData tabData;
        emit tabDataRequested(m_drag_tab_index, &tabData);
        if (tabData.filePath.isEmpty())
            return;

        m_drop_received  = false;
        int draggedIndex = m_drag_tab_index;

        // Create drag object
        QDrag *drag     = new QDrag(this);
        QMimeData *mime = new QMimeData();
        mime->setData(MIME_TYPE, tabData.serialize());
        mime->setUrls({QUrl::fromLocalFile(tabData.filePath)});
        drag->setMimeData(mime);

        // Create a pixmap of the tab for visual feedback
        QRect tabRect = this->tabRect(draggedIndex);
        QPixmap tabPixmap(tabRect.size());
        tabPixmap.fill(Qt::transparent);
        QPainter painter(&tabPixmap);
        painter.setOpacity(0.8);

        // Render the tab
        QStyleOptionTab opt;
        initStyleOption(&opt, draggedIndex);
        opt.rect = QRect(QPoint(0, 0), tabRect.size());
        style()->drawControl(QStyle::CE_TabBarTab, &opt, &painter, this);
        painter.end();

        drag->setPixmap(tabPixmap);
        drag->setHotSpot(event->pos() - tabRect.topLeft());

        Qt::DropAction result = drag->exec(Qt::MoveAction | Qt::CopyAction);
        m_drag_tab_index      = -1;

        // If the drag was accepted by another window, close this tab
        if (result == Qt::MoveAction && !m_drop_received)
        {
            emit tabDetached(draggedIndex, QCursor::pos());
        }
        else if (result == Qt::IgnoreAction)
        {
            // Tab was dropped outside any accepting window - detach to new
            // window
            emit tabDetachedToNewWindow(draggedIndex, tabData);
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        m_drag_tab_index = -1;
        QTabBar::mouseReleaseEvent(event);
    }

    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->mimeData()->hasFormat(MIME_TYPE))
        {
            event->setDropAction(Qt::MoveAction);
            event->accept();
        }
        else if (event->mimeData()->hasUrls())
        {
            // Accept file drops
            for (const QUrl &url : event->mimeData()->urls())
            {
                if (url.isLocalFile()
                    && url.toLocalFile().endsWith(".pdf", Qt::CaseInsensitive))
                {
                    event->acceptProposedAction();
                    return;
                }
            }
        }
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (event->mimeData()->hasFormat(MIME_TYPE))
        {
            event->setDropAction(Qt::MoveAction);
            event->accept();
        }
        else if (event->mimeData()->hasUrls())
        {
            event->acceptProposedAction();
        }
    }

    void dropEvent(QDropEvent *event) override
    {
        if (event->mimeData()->hasFormat(MIME_TYPE))
        {
            m_drop_received = true;

            // Handle Internal Move
            if (event->source() == this)
            {
                int toIndex   = tabAt(event->position().toPoint());
                int fromIndex = m_drag_tab_index; // This is valid because we
                                                  // haven't cleared it yet

                if (toIndex != -1 && fromIndex != -1 && fromIndex != toIndex)
                {
                    moveTab(fromIndex, toIndex);
                    setCurrentIndex(
                        toIndex); // Optional: keep the moved tab active
                    event->setDropAction(Qt::MoveAction);
                    event->accept();
                    return;
                }

                // If it was dropped on itself, still accept to prevent detachment
                event->acceptProposedAction();
                return;
            }

            TabData tabData
                = TabData::deserialize(event->mimeData()->data(MIME_TYPE));

            if (!tabData.filePath.isEmpty())
            {
                event->setDropAction(Qt::MoveAction);
                event->accept();
                emit tabDropReceived(tabData);
            }
        }
    }

private:
    QPoint m_drag_start_pos;
    int m_drag_tab_index{-1};
    bool m_drop_received{false};
};
