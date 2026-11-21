#include "GrepPicker.hpp"

#include "WaitingSpinnerWidget.hpp"

#include <QHBoxLayout>
#include <QtConcurrent>

GrepPicker::GrepPicker(const Config::Picker &config, QWidget *parent) noexcept
    : Picker(config, parent)
{
    setStructureMode(StructureMode::Flat);
    setColumns({{.header = "Line", .stretch = 1}});
    setPrompt("Grep");

    m_spinner = new WaitingSpinnerWidget(this, false, false);
    m_spinner->setInnerRadius(5);
    m_spinner->setColor(palette().color(QPalette::Text));
    m_spinner->hide();

    m_countLabel = new QLabel("0 lines", this);

    auto *footer = new QHBoxLayout();
    footer->addWidget(m_countLabel);
    footer->addStretch();
    footer->addWidget(m_spinner);
    layout()->addItem(footer);

    // The typed text is the pattern, not a filter of the list: the list is
    // made of the lines that match it, so the picker's own filtering is
    // switched off again once it has seen the text.
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(150);
    connect(&m_debounce, &QTimer::timeout, this,
            [this]() { run(m_searchBox->text()); });

    connect(m_searchBox, &QLineEdit::textChanged, this, [this](const QString &)
    {
        m_proxy->setFilterText({}, Qt::CaseInsensitive);
        if (m_searchBox->text().isEmpty())
        {
            cancel();
            m_hits.clear();
            repopulate();
        }
        else
        {
            m_debounce.start();
        }
    });

    connect(m_listView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex &current)
    {
        if (!current.isValid() || !isVisible())
            return;
        const auto list
            = current.siblingAtColumn(0).data(Qt::UserRole + 2)
                  .value<Item>().data.toList();
        if (list.size() < 2)
            return;
        const QPointF pt = list[1].toPointF();
        emit previewRequested(list[0].toInt(), pt.x(), pt.y());
    });

    connect(&m_watcher, &QFutureWatcher<std::vector<Model::GrepHit>>::finished,
            this, [this]()
    {
        // A run that was replaced or cancelled leaves an empty future behind
        if (m_watcher.future().resultCount() == 0)
            return;
        m_hits = m_watcher.result();
        m_spinner->stop();
        m_spinner->hide();
        repopulate();
    });
}

void
GrepPicker::launch() noexcept
{
    m_accepted = false;
    Picker::launch();
}

void
GrepPicker::hideEvent(QHideEvent *event)
{
    Picker::hideEvent(event);
    if (!event->spontaneous() && !m_accepted)
    {
        cancel();
        emit dismissed();
    }
}

void
GrepPicker::setPattern(const QString &pattern) noexcept
{
    m_searchBox->setText(pattern);
}

void
GrepPicker::cancel() noexcept
{
    m_debounce.stop();
    if (m_cancel)
        m_cancel->store(true);
    m_watcher.setFuture({}); // the result of a stale run is dropped
    m_spinner->stop();
    m_spinner->hide();
}

void
GrepPicker::run(const QString &pattern) noexcept
{
    cancel();
    if (!m_model || pattern.isEmpty())
        return;

    const bool caseSensitive
        = caseSensitivity(pattern) == Qt::CaseSensitive;
    m_cancel = std::make_shared<std::atomic_bool>(false);

    m_spinner->show();
    m_spinner->start();
    QPointer<Model> model = m_model;
    auto flag             = m_cancel;
    m_watcher.setFuture(QtConcurrent::run([model, pattern, caseSensitive, flag]()
    {
        if (!model)
            return std::vector<Model::GrepHit>{};
        return model->grep(pattern, caseSensitive, flag.get());
    }));
}

QList<Picker::Item>
GrepPicker::collectItems()
{
    QList<Item> items;
    items.reserve(static_cast<int>(m_hits.size()));
    for (const auto &h : m_hits)
    {
        const double cx
            = (h.quad.ul.x + h.quad.ur.x + h.quad.ll.x + h.quad.lr.x) * 0.25;
        const double cy
            = (h.quad.ul.y + h.quad.ur.y + h.quad.ll.y + h.quad.lr.y) * 0.25;
        items.push_back({.columns = {QString("p%1: %2").arg(h.page + 1).arg(
                             h.line)},
                         .data    = QVariantList{h.page, QPointF(cx, cy)},
                         .children = {}});
    }
    return items;
}

void
GrepPicker::onFilterChanged(int visibleCount)
{
    m_countLabel->setText(QString("%1 lines").arg(visibleCount));
}

void
GrepPicker::onItemAccepted(const Item &item)
{
    const auto list = item.data.toList();
    if (list.size() < 2)
        return;
    m_accepted       = true;
    const QPointF pt = list[1].toPointF();
    emit gotoLocationRequested(list[0].toInt(), pt.x(), pt.y());
}
