#include "EditLastPagesWidget.hpp"

#include <QFile>
#include <algorithm>

EditLastPagesWidget::EditLastPagesWidget(RecentFilesStore *store,
                                         QWidget *parent)
    : QDialog(parent), m_store(store)
{
    m_model = new RecentFilesModel(this);
    if (m_store)
        m_model->setEntries(m_store->entries());
    else
        m_model->setEntries({});

    m_autoremove_btn     = new QPushButton(tr("Remove unfound files"));
    m_revert_changes_btn = new QPushButton(tr("Revert Changes"));
    m_delete_row_btn     = new QPushButton(tr("Delete row"));
    m_apply_btn          = new QPushButton(tr("Apply"));
    m_close_btn          = new QPushButton(tr("Close"));

    initConnections();

    m_tableView->setModel(m_model);
    m_tableView->horizontalHeader()->setSectionResizeMode(0,
                                                          QHeaderView::Stretch);
    m_tableView->setSelectionBehavior(
        QAbstractItemView::SelectionBehavior::SelectRows);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_tableView);

    QHBoxLayout *btnLayout = new QHBoxLayout();

    btnLayout->addStretch(1);
    btnLayout->addWidget(m_autoremove_btn);
    btnLayout->addWidget(m_revert_changes_btn);
    btnLayout->addWidget(m_delete_row_btn);
    btnLayout->addWidget(m_apply_btn);
    btnLayout->addWidget(m_close_btn);
    layout->addLayout(btnLayout);

    if (m_model->rowCount() == 0)
    {
        m_autoremove_btn->setDisabled(true);
        m_revert_changes_btn->setDisabled(true);
        m_delete_row_btn->setDisabled(true);
        m_apply_btn->setDisabled(true);
    }
}

void
EditLastPagesWidget::initConnections() noexcept
{

    connect(m_autoremove_btn, &QPushButton::clicked, this,
            &EditLastPagesWidget::autoRemoveFiles);

    connect(m_revert_changes_btn, &QPushButton::clicked, this,
            &EditLastPagesWidget::revertChanges);

    connect(m_delete_row_btn, &QPushButton::clicked, this,
            &EditLastPagesWidget::deleteRows);

    connect(m_apply_btn, &QPushButton::clicked, this, [&]()
    {
        auto confirm = QMessageBox::question(
            this, tr("Apply Changes"), tr("Do you want to apply the changes ?"));
        if (confirm == QMessageBox::Yes)
        {
            if (m_store)
            {
                m_store->setEntries(m_model->entries());
                if (!m_store->save())
                {
                    QMessageBox::warning(this, tr("Apply Changes"),
                                         tr("Failed to save recent files"));
                    return;
                }
            }
            m_model->markClean();
        }
    });

    connect(m_close_btn, &QPushButton::clicked, this,
            &EditLastPagesWidget::close);
}

void
EditLastPagesWidget::autoRemoveFiles() noexcept
{
    int nrows = m_model->rowCount();
    QList<int> removableFileIndexes;

    if (nrows == 0)
        return;

    for (int i = 0; i < nrows; i++)
    {
        QModelIndex index = m_model->index(i, 0);
        QString filePath  = m_model->data(index).toString();
        if (!QFile::exists(filePath))
            removableFileIndexes.append(i);
    }

    // Remove rows in reverse order to avoid index shifting
    std::sort(removableFileIndexes.begin(), removableFileIndexes.end(),
              std::greater<int>());
    for (int row : removableFileIndexes)
    {
        m_model->removeRow(row);
    }
    // Remove removableFileIndexes
}

void
EditLastPagesWidget::revertChanges() noexcept
{
    if (!m_model->isDirty())
    {
        QMessageBox::information(this, tr("Revert Changes"),
                                 tr("There are no changes to revert to"));
        return;
    }
    auto confirm = QMessageBox::question(
        this, tr("Revert Changes"), tr("Do you really want to revert the changes ?"));
    if (confirm == QMessageBox::Yes)
        m_model->revertAll();
}

void
EditLastPagesWidget::deleteRows() noexcept
{

    if (!m_tableView->selectionModel())
        return;

    auto rows = m_tableView->selectionModel()->selectedRows();

    std::sort(rows.begin(), rows.end(),
              [](const QModelIndex &a, const QModelIndex &b)
    { return a.row() > b.row(); });

    for (const auto &index : rows)
    {
        m_model->removeRow(index.row());
    }
}
