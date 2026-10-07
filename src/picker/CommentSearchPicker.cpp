#include "CommentSearchPicker.hpp"

#include "WaitingSpinnerWidget.hpp"

#include <QHBoxLayout>
#include <QShortcut>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>
#include <QtConcurrent>

CommentSearchPicker::CommentSearchPicker(const Config::Picker &config,
                                         QWidget *parent) noexcept
    : Picker(config, parent), m_config(config)
{
    setColumns(
        {{.header = "Annot Comments", .stretch = 1}}); // ← add this first

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
            &CommentSearchPicker::refresh);

    connect(&m_watcher,
            &QFutureWatcher<std::vector<Model::AnnotCommentInfo>>::finished,
            this, [this]()
    {
        m_entries = m_watcher.result();
        setLoading(false);
        // Re-run collectItems with current search term
        if (!m_entries.empty())
        {
            repopulate();
        }
    });

    setPrompt("Search Comments");
}

void
CommentSearchPicker::launch() noexcept
{
    Picker::launch();
    if (m_entries.empty() && !m_watcher.isRunning())
        refresh();
}

// Called by Picker::launch() — return all items, filtering happens in proxy
QList<Picker::Item>
CommentSearchPicker::collectItems()
{
    return buildItems({}); // proxy handles filtering
}

QList<Picker::Item>
CommentSearchPicker::buildItems(const QString & /*term*/) const noexcept
{
    QList<Item> items;
    items.reserve(static_cast<int>(m_entries.size()));

    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        const auto &e   = m_entries.at(i);
        const double cx = (e.rect.x0 + e.rect.x1) * 0.5;
        const double cy = (e.rect.y0 + e.rect.y1) * 0.5;

        items.push_back({
            .columns  = {QString("p%1: %2").arg(e.page + 1).arg(e.comment)},
            .data     = QVariantList{e.page, QPointF(cx, cy)},
            .children = {},
        });
    }
    return items;
}

void
CommentSearchPicker::onItemAccepted(const Item &item)
{
    const auto list = item.data.toList();
    if (list.size() < 2)
        return;

    const auto page  = list.at(0).toInt();
    const auto point = list.at(1).toPointF();
    emit gotoLocationRequested(page, point.x(), point.y());
}

void
CommentSearchPicker::refresh() noexcept
{
    if (!m_model || m_watcher.isRunning())
        return;

    setLoading(true);
    QPointer<Model> model = m_model;
    m_watcher.setFuture(QtConcurrent::run([model]()
    {
        if (!model)
            return std::vector<Model::AnnotCommentInfo>{};
        return model->collect_annot_comments();
    }));
}

void
CommentSearchPicker::setLoading(bool state) noexcept
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
