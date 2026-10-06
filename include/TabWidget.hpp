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
    using TabId = int;
    TabWidget(QWidget *parent = nullptr);
    TabBar *tabBar() const noexcept;
    int addTab(QWidget *page, const QString &title) noexcept;
    int insertTab(const int index, QWidget *page,
                  const QString &title) noexcept;

    inline int id(int index) const noexcept
    {
        return m_tab_bar->tabData(index).toUInt();
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

    inline void setCloseButtonMode(TabBar::CloseButtonMode mode) noexcept
    {
        m_tab_bar->setCloseButtonMode(mode);
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

    inline DocumentContainer *currentContainer() const noexcept
    {
        return qobject_cast<DocumentContainer *>(
            m_stacked_widget->currentWidget());
    }

    inline QString tabTitle(const int index) const noexcept
    {
        return m_tab_bar->tabText(index);
    }

    inline void setTabTitle(const int index, const QString &title) noexcept
    {
        m_tab_bar->setTabText(index, title);
    }

    DocumentContainer *container(int index) const noexcept;
    void removeTab(const int index) noexcept;
    void removeTab(QWidget *page) noexcept;
    void setTabPosition(QTabWidget::TabPosition position) noexcept;

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
    void contextMenuRequested(int index, const QPoint &globalPos);

private:
    TabId m_id                             = 0;
    QStackedWidget *m_stacked_widget       = nullptr;
    TabBar *m_tab_bar                      = nullptr;
    QLayout *m_main_layout                 = nullptr;
    QTabWidget::TabPosition m_tab_position = QTabWidget::North;
};
