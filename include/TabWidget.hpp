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

    int id(int index) const noexcept
    {
        return m_tab_bar->tabData(index).toUInt();
    }

    int count() const noexcept
    {
        return m_tab_bar->count();
    }

    bool tabBarAutoHide() const noexcept
    {
        return m_tab_bar->autoHide();
    }

    int indexOf(QWidget *page) const noexcept
    {
        return m_stacked_widget->indexOf(page);
    }

    QWidget *widget(int index) const noexcept
    {
        if (index < 0 || index >= count())
            return nullptr;
        return m_stacked_widget->widget(index);
    }

    QWidget *currentWidget() const noexcept
    {
        return m_stacked_widget->currentWidget();
    }

    int currentIndex() const noexcept
    {
        return m_tab_bar->currentIndex();
    }

    void setMovable(bool movable) noexcept
    {
        m_tab_bar->setMovable(movable);
    }

    void setCloseButtonMode(TabBar::CloseButtonMode mode) noexcept
    {
        m_tab_bar->setCloseButtonMode(mode);
    }

    void setCurrentIndex(int index) noexcept
    {
        if (index < 0 || index >= count())
            return;
        m_stacked_widget->setCurrentIndex(index);
        m_tab_bar->setCurrentIndex(index);
    }

    void setTabBarAutoHide(bool hide) noexcept
    {
        m_tab_bar->setAutoHide(hide);
    }

    const QString tabText(const int index) const noexcept
    {
        return m_tab_bar->tabText(index);
    }

    DocumentContainer *currentContainer() const noexcept
    {
        return qobject_cast<DocumentContainer *>(
            m_stacked_widget->currentWidget());
    }

    QString tabTitle(const int index) const noexcept
    {
        return m_tab_bar->tabText(index);
    }

    void setTabTitle(const int index, const QString &title) noexcept
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
