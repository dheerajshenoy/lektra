#pragma once

#include "MessageLog.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QQueue>
#include <QWidget>

class MessageBar : public QFrame
{
public:
    MessageBar(QWidget *parent = nullptr);
    // The bar is tinted according to `level`.
    void showMessage(const QString &msg, float sec = 2.0f,
                     MessageLog::Level level = MessageLog::Level::Info) noexcept;

    struct MessageItem
    {
        QString message;
        float duration;
        MessageLog::Level level;
    };

private:
    void showNext() noexcept;
    void applyLevel(MessageLog::Level level) noexcept;
    bool m_showing = false;
    QQueue<MessageItem> m_queue;
    QLabel *m_label = new QLabel();
};
