#pragma once

#include "RecentFilesModel.hpp"
#include "RecentFilesStore.hpp"

#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QTableView>
#include <QVBoxLayout>

class EditLastPagesWidget : public QDialog
{
public:
    EditLastPagesWidget(RecentFilesStore *store, QWidget *parent = nullptr);

private:
    void autoRemoveFiles() noexcept;
    void initConnections() noexcept;
    void revertChanges() noexcept;
    void deleteRows() noexcept;

    RecentFilesModel *m_model{nullptr};
    QTableView *m_tableView{new QTableView()};
    RecentFilesStore *m_store{nullptr};

    QPushButton *m_autoremove_btn;
    QPushButton *m_revert_changes_btn;
    QPushButton *m_delete_row_btn;
    QPushButton *m_apply_btn;
    QPushButton *m_close_btn;
};
