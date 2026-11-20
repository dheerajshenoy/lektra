#pragma once

#include "MessageLog.hpp"

#include <QWidget>

class QLabel;
class QListWidget;

// The messages tab (the `messages` command): every entry of the MessageLog,
// updated as new messages arrive.
class MessagesWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MessagesWidget(QWidget *parent = nullptr);

private:
    void rebuild() noexcept;
    void append(const MessageLog::Entry &entry) noexcept;
    void updateCount() noexcept;
    void copyAll() const noexcept;
    void copySelected(bool withTimeAndLevel) const noexcept;
    void showContextMenu(const QPoint &pos) noexcept;

    QListWidget *m_list{nullptr};
    QLabel *m_count{nullptr};
};
