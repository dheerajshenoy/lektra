#pragma once

#include "DraggableTabBar.hpp"

#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QMenu>
#include <QPainter>
#include <QTabBar>
#include <QTabWidget>

namespace
{
using TabId = uint32_t;

static TabId nextTabId{0};

inline TabId
newTabId() noexcept
{
    return nextTabId++;
}

} // namespace

class TabWidget : public QTabWidget
{
    Q_OBJECT
    TabId m_id{0};

public:
    TabWidget(QWidget *parent = nullptr) : QTabWidget(parent), m_id(newTabId())
    {
        m_tab_bar = new DraggableTabBar(this);
        setTabBar(m_tab_bar);

        setElideMode(Qt::TextElideMode::ElideRight);
        setDocumentMode(true);
        setMovable(true);
        setTabsClosable(true);
        setAcceptDrops(true);
        setStyleSheet("border: 0");
        setTabPosition(QTabWidget::TabPosition::North);

        // Forward signals from the draggable tab bar
        connect(m_tab_bar, &DraggableTabBar::tabDataRequested, this,
                &TabWidget::tabDataRequested);
        connect(m_tab_bar, &DraggableTabBar::tabDropReceived, this,
                &TabWidget::tabDropReceived);
        connect(m_tab_bar, &DraggableTabBar::tabDetached, this,
                &TabWidget::tabDetached);
        connect(m_tab_bar, &DraggableTabBar::tabDetachedToNewWindow, this,
                &TabWidget::tabDetachedToNewWindow);
    }

    DraggableTabBar *draggableTabBar() const noexcept
    {
        return m_tab_bar;
    }

    inline int addTab(QWidget *page, const QString &title) noexcept
    {
        int result = QTabWidget::addTab(page, title);
        emit tabAdded(result);
        return result;
    }

    inline uint32_t id() const noexcept
    {
        return m_id;
    }

    inline int insertTab(const int index, QWidget *page,
                         const QString &title) noexcept
    {
        const int result = QTabWidget::insertTab(index, page, title);
        emit tabAdded(result);
        return result;
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QTabWidget::paintEvent(event);

        if (count() == 0)
        {
            QPainter painter(this);
            painter.fillRect(rect(), palette().color(QPalette::Window));
            painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));

            QString logoText = "lektra";

            // Setup logo font - load from resources
            int fontId = QFontDatabase::addApplicationFont(
                ":/resources/fonts/Major-Mono-Display.ttf");
            QString fontFamily
                = QFontDatabase::applicationFontFamilies(fontId).value(
                    0, QString());
            QFont logoFont;
            if (!fontFamily.isEmpty())
                logoFont.setFamily(fontFamily);
            logoFont.setPointSize(50);
            logoFont.setBold(true);
            QFontMetrics logoFm(logoFont);
            int logoHeight = logoFm.height();

            // Calculate total height and starting Y position
            int spacing     = 20;

            // Draw logo text
            painter.setFont(logoFont);
            QRect logoRect(0, rect().height() / 2.0f, rect().width(), logoHeight);
            painter.drawText(logoRect, Qt::AlignHCenter | Qt::AlignTop,
                             logoText);
        }
    }

signals:
    void tabAdded(int index);
    void openInExplorerRequested(int index);
    void filePropertiesRequested(int index);
    void tabDataRequested(int index, DraggableTabBar::TabData *outData);
    void tabDropReceived(const DraggableTabBar::TabData &data);
    void tabDetached(int index, const QPoint &globalPos);
    void tabDetachedToNewWindow(int index,
                                const DraggableTabBar::TabData &data);

private:
    DraggableTabBar *m_tab_bar{nullptr};
};
