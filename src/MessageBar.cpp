#include "MessageBar.hpp"

#include <QColor>
#include <QTimer>

MessageBar::MessageBar(QWidget *parent) : QFrame(parent)
{
    setObjectName("messageBar");
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0); // also remove margins
    layout->addWidget(m_label);
    setLayout(layout);
    setFixedHeight(0);
}

void
MessageBar::showMessage(const QString &msg, float sec,
                        MessageLog::Level level) noexcept
{
    m_queue.enqueue({.message = msg, .duration = sec, .level = level});
    if (!m_showing)
        showNext();
}

void
MessageBar::showNext() noexcept
{
    if (m_queue.isEmpty())
    {
        m_showing = false;
        return;
    }

    m_showing             = true;
    const auto [msg, sec, level] = m_queue.dequeue();

    applyLevel(level);
    m_label->setText(msg);
    setFixedHeight(30);
    QTimer::singleShot(sec * 1000, this, [this]()
    {
        setFixedHeight(0);
        setStyleSheet({});
        m_label->clear();
        showNext(); // Show the next message after the current one
    });
}

void
MessageBar::applyLevel(MessageLog::Level level) noexcept
{
    QColor color;
    switch (level)
    {
        case MessageLog::Level::Warning:
            color = QColor(230, 160, 20);
            break;
        case MessageLog::Level::Error:
            color = QColor(220, 60, 60);
            break;
        case MessageLog::Level::Info:
            color = palette().color(QPalette::Highlight);
            break;
    }
    setStyleSheet(
        QString("QFrame#messageBar { background: rgba(%1, %2, %3, 45); "
                "border-left: 3px solid rgb(%1, %2, %3); }")
            .arg(color.red())
            .arg(color.green())
            .arg(color.blue()));
}
