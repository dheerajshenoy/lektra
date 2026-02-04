#include "TabWidget.hpp"

#include <QVBoxLayout>

static TabWidget::TabId nextTabId{0};

static TabWidget::TabId
g_newTabId() noexcept
{
    return nextTabId++;
}

TabWidget::TabWidget(QWidget *parent) : QWidget(parent), m_id(g_newTabId())
{
    m_tab_bar        = new TabBar();
    m_stacked_widget = new QStackedWidget();

    QVBoxLayout *main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);
    main_layout->addWidget(m_tab_bar);
    main_layout->addWidget(m_stacked_widget);

    setLayout(main_layout);
    setMovable(true);

    // setElideMode(Qt::TextElideMode::ElideRight);
    setTabsClosable(true);
    setAcceptDrops(true);
    setStyleSheet("border: 0");
    setTabPosition(QTabWidget::TabPosition::North);

    // Forward signals from the draggable tab bar
    connect(m_tab_bar, &TabBar::tabDataRequested, this,
            &TabWidget::tabDataRequested);
    connect(m_tab_bar, &TabBar::tabDropReceived, this,
            &TabWidget::tabDropReceived);
    connect(m_tab_bar, &TabBar::tabDetached, this, &TabWidget::tabDetached);
    connect(m_tab_bar, &TabBar::tabDetachedToNewWindow, this,
            &TabWidget::tabDetachedToNewWindow);

    connect(m_tab_bar, &QTabBar::currentChanged, this, [this](int i)
    {
        m_stacked_widget->setCurrentIndex(i);
        emit currentChanged(i);
    });

    connect(m_tab_bar, &QTabBar::tabCloseRequested, this,
            &TabWidget::tabCloseRequested);
}

TabBar *
TabWidget::tabBar() const noexcept
{
    return m_tab_bar;
}

int
TabWidget::addTab(QWidget *page, const QString &title) noexcept
{
    m_stacked_widget->addWidget(page);
    const int tabIndex = m_tab_bar->addTab(title);

    emit tabAdded(tabIndex);
    return tabIndex;
}

int
TabWidget::insertTab(const int index, QWidget *page,
                     const QString &title) noexcept
{
    m_stacked_widget->insertWidget(index, page);
    const int tabIndex = m_tab_bar->insertTab(index, title);
    emit tabAdded(tabIndex);
    return tabIndex;
}

void
TabWidget::removeTab(const int index) noexcept
{
    if (index < 0 || index >= count())
        return;

    QWidget *page = m_stacked_widget->widget(index);
    m_stacked_widget->removeWidget(page);
    page->deleteLater();

    m_tab_bar->removeTab(index);
    emit tabRemoved(index);
}

void
TabWidget::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);

    if (count() == 0)
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().color(QPalette::Window));
        painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));

        const char *logoText = "lektra";

        // Setup logo font - load from resources
        const int fontId = QFontDatabase::addApplicationFont(
            ":/resources/fonts/Major-Mono-Display.ttf");

        const QString fontFamily
            = QFontDatabase::applicationFontFamilies(fontId).value(0,
                                                                   QString());
        QFont logoFont;
        if (!fontFamily.isEmpty())
            logoFont.setFamily(fontFamily);
        logoFont.setPointSize(50);
        logoFont.setBold(true);

        QFontMetrics logoFm(logoFont);
        const int logoHeight = logoFm.height();

        painter.setFont(logoFont);
        QRect logoRect(0, rect().height() / 2.0f, rect().width(), logoHeight);
        painter.drawText(logoRect, Qt::AlignHCenter | Qt::AlignTop, logoText);
    }
}
