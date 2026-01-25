#pragma once

#include "Config.hpp"

#include <QLineEdit>
#include <QStringList>
#include <QTableView>
#include <QWidget>
#include <qsortfilterproxymodel.h>
#include <utility>
#include <vector>

// Model for command palette
class CommandModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit CommandModel(
        const std::vector<std::pair<QString, QString>> &commands,
        QObject *parent = nullptr) noexcept;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index,
                  int role = Qt::DisplayRole) const override;

private:
    struct CommandEntry
    {
        QString name;
        QString shortcut;
        // QString description;
    };

    std::vector<CommandEntry> m_commands;
};

// Widget for command palette
class CommandPaletteWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CommandPaletteWidget(
        const Config &config,
        const std::vector<std::pair<QString, QString>> &commands,
        QWidget *parent = nullptr) noexcept;
    void selectFirstItem() noexcept;

protected:
    void showEvent(QShowEvent *event) override;

signals:
    void commandSelected(const QString &commandName,
                         const QStringList &args = {});

private:
    const Config &m_config;
    void initGui() noexcept;
    void initConnections() noexcept;
    QLineEdit *m_input_line{nullptr};
    QTableView *m_command_table{nullptr};
    CommandModel *m_command_model{nullptr};
    QSortFilterProxyModel *m_proxy_model{nullptr};
    QFrame *m_frame{nullptr};
};
