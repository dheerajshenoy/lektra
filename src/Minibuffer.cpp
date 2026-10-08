#include "Minibuffer.hpp"

Minibuffer::Minibuffer(const Config::Minibuffer &config, QWidget *parent)
    : QWidget(parent), m_config(config)
{
    m_layout     = new QVBoxLayout(this);
    m_input_line = new QLineEdit(this);

    m_input_line->installEventFilter(
        this); // Install event filter to intercept key events

    // Set up the completion model and proxy
    m_completion_model = new QStandardItemModel(0, 2, this);

    // Set up the proxy model for filtering
    m_completion_proxy = new QSortFilterProxyModel(this);
    m_completion_proxy->setSourceModel(m_completion_model);
    m_completion_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    // Set up the completion view
    m_completion_view = new QTableView(this);
    m_completion_view->setModel(m_completion_proxy);
    m_completion_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_completion_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_completion_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_completion_view->setFocusPolicy(Qt::NoFocus);
    m_layout->addWidget(m_input_line);
    m_layout->addWidget(m_completion_view);

    m_completion_view->hide(); // Initially hide the completion view

    initConnections();

    setContentsMargins(0, 0, 0, 0);

    setLayout(m_layout);
    setVisible(m_config.show);
}

void
Minibuffer::initConnections() noexcept
{

    connect(m_input_line, &QLineEdit::textChanged, this,
            [this](const QString &text)
    {
        if (text.isEmpty())
        {
            m_completion_proxy->setFilterFixedString("");
            m_completion_view->hide();
        }
        else
        {
            m_completion_proxy->setFilterFixedString(text);

            // Hide if there are no matching results; show if matches exist
            if (m_completion_proxy->rowCount() == 0)
            {
                m_completion_view->hide();
            }
            else
            {
                m_completion_view->show();
                // Automatically select the first row so
                // returnPressed/navigation works seamlessly
                m_completion_view->selectRow(0);
            }
        }
    });

    connect(m_completion_view, &QTableView::clicked, this,
            [this](const QModelIndex &index)
    {
        QString selected = m_completion_proxy->data(index).toString();
        m_input_line->setText(selected);
    });

    connect(m_input_line, &QLineEdit::returnPressed, this, [this]()
    {
        QString input = m_input_line->text();
        // Handle the input as needed
#ifndef NDEBUG
        qDebug() << "User input:" << input;
#endif
    });
}

void
Minibuffer::setCompletionEntries(
    const QStringList &headers,
    const std::vector<MinibufferCompletionItem> &items)
{
    m_completion_model->clear();
    m_completion_model->setHorizontalHeaderLabels(headers);

    const int columnCount = headers.size();

    for (const auto &item : items)
    {
        QList<QStandardItem *> rowItems;

        for (int i = 0; i < columnCount; ++i)
        {
            QString cellText
                = (i < (int)item.columns.size()) ? item.columns[i] : "";
            rowItems.append(new QStandardItem(cellText));
        }

        // Store the generic payload on column 0 item
        if (!rowItems.isEmpty())
        {
            rowItems.first()->setData(item.payload, PayloadRole);
        }

        m_completion_model->appendRow(rowItems);
    }

    if (m_completion_view)
    {
        m_completion_view->horizontalHeader()->setVisible(!headers.isEmpty());
        m_completion_view->horizontalHeader()->setSectionResizeMode(
            QHeaderView::Stretch);
    }
}

void
Minibuffer::handleSelection() noexcept
{
    QModelIndex currentIndex = m_completion_view->currentIndex();
    if (!currentIndex.isValid() && m_completion_proxy->rowCount() > 0)
    {
        currentIndex = m_completion_proxy->index(0, 0);
    }

    if (currentIndex.isValid())
    {
        QModelIndex sourceIndex = m_completion_proxy->mapToSource(
            m_completion_proxy->index(currentIndex.row(), 0));

        QVariant payload = m_completion_model->data(sourceIndex, PayloadRole);
        emit itemSelected(payload, m_input_line->text());
    }

    m_input_line->clear();
    m_completion_view->hide();
}

bool
Minibuffer::eventFilter(QObject *watched, QEvent *event)
{
    // Check if the event is coming from our input field
    if (watched == m_input_line)
    {

        if (event->type() == QEvent::KeyPress)
        {
            auto *keyEvent = static_cast<QKeyEvent *>(event);

            if (keyEvent->key() == Qt::Key_Tab)
            {
                event->accept(); // Accept the event to prevent further
                                 // propagation
                return true;
            }

            // 2. Intercept Escape to hide minibuffer or clear text
            if (keyEvent->key() == Qt::Key_Escape)
            {
                m_input_line->clear();
                m_completion_view->hide();
                this->hide();
                return true; // Consumed
            }

            // 3. Forward Up/Down arrows directly to navigation
            if (keyEvent->key() == Qt::Key_Down)
            {
                // Select next row in QTableView
                return true;
            }
            if (keyEvent->key() == Qt::Key_Up)
            {
                // Select previous row in QTableView
                return true;
            }
        }
    }

    // Pass all other events (normal typing, backspace, etc.) to base class
    // handling
    return QWidget::eventFilter(watched, event);
}
