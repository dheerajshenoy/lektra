#include "TabWidget.hpp"

#include <QVBoxLayout>

static TabWidget::TabId nextId{0};

static TabWidget::TabId
g_newId() noexcept
{
    return nextId++;
}

TabWidget::TabWidget(QWidget *parent) : QWidget(parent), m_id(g_newId())
{
    m_tab_bar        = new TabBar();
    m_stacked_widget = new QStackedWidget();

    setTabPosition(QTabWidget::TabPosition::North);
    setTabsClosable(true);
    setAcceptDrops(true);
    setStyleSheet("border: 0");
    setMovable(true);
    // m_tab_bar->setElideMode(Qt::TextElideMode::ElideLeft);

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

void
TabWidget::setTabPosition(QTabWidget::TabPosition position) noexcept
{
    m_tab_position = position;

    // Remove both widgets from layout first
    if (m_main_layout)
    {
        m_main_layout->removeWidget(m_tab_bar);
        m_main_layout->removeWidget(m_stacked_widget);
    }

    // Delete old layout and create appropriate one
    delete m_main_layout;

    switch (position)
    {
        case QTabWidget::North:
        {
            auto *layout = new QVBoxLayout(this);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(0);
            layout->addWidget(m_tab_bar);
            layout->addWidget(m_stacked_widget);

            m_main_layout = layout;
            m_tab_bar->setShape(QTabBar::RoundedNorth);
            break;
        }
        case QTabWidget::South:
        {
            auto *layout = new QVBoxLayout(this);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(0);
            layout->addWidget(m_stacked_widget);
            layout->addWidget(m_tab_bar);

            m_main_layout = layout;
            m_tab_bar->setShape(QTabBar::RoundedSouth);
            break;
        }
        case QTabWidget::West:
        {
            auto *layout = new QHBoxLayout(this);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(0);
            layout->addWidget(m_tab_bar);
            layout->addWidget(m_stacked_widget);

            m_main_layout = layout;
            m_tab_bar->setShape(QTabBar::RoundedWest);
            break;
        }
        case QTabWidget::East:
        {
            auto *layout = new QHBoxLayout(this);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(0);
            layout->addWidget(m_stacked_widget);
            layout->addWidget(m_tab_bar);

            m_main_layout = layout;
            m_tab_bar->setShape(QTabBar::RoundedEast);
            break;
        }
    }

    setLayout(m_main_layout);
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
    m_tab_bar->set_split_count(tabIndex, 1);
    emit tabAdded(tabIndex);
    return tabIndex;
}

int
TabWidget::insertTab(const int index, QWidget *page,
                     const QString &title) noexcept
{
    m_stacked_widget->insertWidget(index, page);
    const int tabIndex = m_tab_bar->insertTab(index, title);
    m_tab_bar->set_split_count(tabIndex, 1);
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
TabWidget::removeTab(QWidget *page) noexcept
{
    const int index = m_stacked_widget->indexOf(page);
    if (index != -1)
        removeTab(index);
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

        constexpr const char *logoText = "lektra";

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
