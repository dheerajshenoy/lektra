#pragma once

#include "DocumentContainer.hpp"
#include "TabBar.hpp"

#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QMenu>
#include <QPainter>
#include <QStackedWidget>
#include <QTabBar>
#include <qnamespace.h>

class TabWidget : public QWidget
{
    Q_OBJECT

public:
    using TabId = uint32_t;
    TabWidget(QWidget *parent = nullptr);
    TabBar *tabBar() const noexcept;
    int addTab(QWidget *page, const QString &title) noexcept;
    int insertTab(const int index, QWidget *page,
                  const QString &title) noexcept;
    inline uint32_t id() const noexcept
    {
        return m_id;
    }

    inline int count() const noexcept
    {
        return m_tab_bar->count();
    }

    inline bool tabBarAutoHide() const noexcept
    {
        return m_tab_bar->autoHide();
    }

    inline int indexOf(QWidget *page) const noexcept
    {
        return m_stacked_widget->indexOf(page);
    }

    inline QWidget *widget(int index) const noexcept
    {
        if (index < 0 || index >= count())
            return nullptr;
        return m_stacked_widget->widget(index);
    }

    inline QWidget *currentWidget() const noexcept
    {
        return m_stacked_widget->currentWidget();
    }

    inline int currentIndex() const noexcept
    {
        return m_tab_bar->currentIndex();
    }

    inline void setMovable(bool movable) noexcept
    {
        m_tab_bar->setMovable(movable);
    }

    inline void setTabPosition(QTabWidget::TabPosition position) noexcept
    {
        if (position == QTabWidget::TabPosition::North
            || position == QTabWidget::TabPosition::South)
        {
            m_tab_bar->setShape(position == QTabWidget::TabPosition::North
                                    ? TabBar::Shape::RoundedNorth
                                    : TabBar::Shape::RoundedSouth);
        }
        else
        {
            m_tab_bar->setShape(position == QTabWidget::TabPosition::West
                                    ? TabBar::Shape::RoundedWest
                                    : TabBar::Shape::RoundedEast);
        }
    }

    inline void setTabsClosable(bool closable) noexcept
    {
        m_tab_bar->setTabsClosable(closable);
    }

    inline void setCurrentIndex(int index) noexcept
    {
        if (index < 0 || index >= count())
            return;
        m_stacked_widget->setCurrentIndex(index);
        m_tab_bar->setCurrentIndex(index);
    }

    inline void setTabBarAutoHide(bool hide) noexcept
    {
        m_tab_bar->setAutoHide(hide);
    }

    inline const QString tabText(const int index) const noexcept
    {
        return m_tab_bar->tabText(index);
    }

    inline DocumentContainer *rootContainer(int index) const noexcept
    {
        if (index < 0 || index >= count())
            return nullptr;
        // Safely cast the page widget back to your container
        return qobject_cast<DocumentContainer *>(
            m_stacked_widget->widget(index));
    }

    inline DocumentContainer *currentRootContainer() const noexcept
    {
        return qobject_cast<DocumentContainer *>(
            m_stacked_widget->currentWidget());
    }

    void removeTab(const int index) noexcept;
    void removeTab(QWidget *page) noexcept;

protected:
    void paintEvent(QPaintEvent *event) override;

signals:
    void tabAdded(int index);
    void tabRemoved(int index);
    void openInExplorerRequested(int index);
    void filePropertiesRequested(int index);
    void tabDataRequested(int index, TabBar::TabData *outData);
    void tabDropReceived(const TabBar::TabData &data);
    void tabDetached(int index, const QPoint &globalPos);
    void tabDetachedToNewWindow(int index, const TabBar::TabData &data);
    void currentChanged(const int index);
    void tabCloseRequested(const int index);

private:
    TabId m_id{0};
    QStackedWidget *m_stacked_widget{nullptr};
    TabBar *m_tab_bar{nullptr};
};
