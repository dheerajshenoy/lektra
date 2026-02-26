#include "HighlightSearchPicker.hpp"

#include "WaitingSpinnerWidget.hpp"

#include <QHBoxLayout>
#include <QShortcut>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>
#include <QtConcurrent>

HighlightSearchPicker::HighlightSearchPicker(QWidget *parent) noexcept
    : Picker(parent)
{
    // --- Extra controls ---
    m_spinner = new WaitingSpinnerWidget(this, false, false);
    m_spinner->setInnerRadius(5);
    m_spinner->setColor(palette().color(QPalette::Text));
    m_spinner->hide();

    m_refreshButton = new QPushButton("Refresh", this);
    m_countLabel    = new QLabel("0 results", this);

    // Inject a footer row into Picker's layout
    auto *footer = new QHBoxLayout();
    footer->addWidget(m_countLabel);
    footer->addStretch();
    footer->addWidget(m_spinner);
    footer->addWidget(m_refreshButton);

    // Picker exposes its outer layout for extension
    layout()->addItem(footer);

    // --- Connections ---
    connect(m_refreshButton, &QPushButton::clicked, this,
            &HighlightSearchPicker::refresh);

    connect(&m_watcher,
            &QFutureWatcher<std::vector<Model::HighlightText>>::finished, this,
            [this]()
    {
        m_entries = m_watcher.result();
        setLoading(false);
        // Re-run collectItems with current search term
        repopulate();
    });
}

// highlightsearchpicker.cpp
void
HighlightSearchPicker::launch() noexcept
{
    Picker::launch();
    if (m_entries.empty() && !m_watcher.isRunning())
        refresh();
}

// Called by Picker::launch() — return all items, filtering happens in proxy
QList<Picker::Item>
HighlightSearchPicker::collectItems()
{
    return buildItems({}); // proxy handles filtering
}

QList<Picker::Item>
HighlightSearchPicker::buildItems(const QString & /*term*/) const noexcept
{
    QList<Item> items;
    items.reserve(static_cast<int>(m_entries.size()));

    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        const auto &e = m_entries[i];
        const double cx
            = (e.quad.ul.x + e.quad.ur.x + e.quad.ll.x + e.quad.lr.x) * 0.25;
        const double cy
            = (e.quad.ul.y + e.quad.ur.y + e.quad.ll.y + e.quad.lr.y) * 0.25;

        items.push_back({
            .columns = {QString("p%1: %2").arg(e.page + 1).arg(e.text)},
            .data    = QVariant::fromValue(
                QPersistentModelIndex{}), // unused, we store below
        });

        // Store page + position as a QVariantList for retrieval
        items.back().data = QVariantList{e.page, QPointF(cx, cy)};
    }
    return items;
}

void
HighlightSearchPicker::onItemAccepted(const Item &item)
{
    const auto list = item.data.toList();
    if (list.size() < 2)
        return;

    auto page  = list[0].toInt();
    auto point = list[1].toPointF();
    emit gotoLocationRequested(page, point.x(), point.y());
}

void
HighlightSearchPicker::refresh() noexcept
{
    if (!m_model || m_watcher.isRunning())
        return;

    setLoading(true);
    QPointer<Model> model = m_model;
    m_watcher.setFuture(QtConcurrent::run([model]()
    {
        if (!model)
            return std::vector<Model::HighlightText>{};
        return model->collectHighlightTexts(true);
    }));
}

void
HighlightSearchPicker::setLoading(bool state) noexcept
{
    m_refreshButton->setEnabled(!state);
    if (state)
    {
        m_spinner->show();
        m_spinner->start();
    }
    else
    {
        m_spinner->stop();
        m_spinner->hide();
    }
}
