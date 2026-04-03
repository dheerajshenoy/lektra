#pragma once

#include "RecentFilesModel.hpp"
#include "RecentFilesStore.hpp"

#include <QFile>
#include <QHeaderView>
#include <QTableView>
#include <QVBoxLayout>
#include <QWidget>

class StartupWidget : public QWidget
{
    Q_OBJECT
public:
    StartupWidget(RecentFilesStore *store, QWidget *parent = nullptr);

signals:
    void openFileRequested(const QString &filename);

private:
    QTableView *m_table_view{nullptr};
    RecentFilesStore *m_store{nullptr};
    RecentFilesModel *m_model{nullptr};
};
