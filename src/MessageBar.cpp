#include "MessageBar.hpp"

#include <QTimer>

MessageBar::MessageBar(QWidget *parent) : QWidget(parent)
{
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0); // also remove margins
    layout->addWidget(m_label);
    setLayout(layout);
    setFixedHeight(0);
}

void
MessageBar::showMessage(const QString &msg, float sec) noexcept
{
    m_queue.enqueue({.message = msg, .duration = sec});
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
    const auto [msg, sec] = m_queue.dequeue();

    m_label->setText(msg);
    // show();
    setFixedHeight(30);
    QTimer::singleShot(sec * 1000, this, [this]()
    {
        // hide();
        setFixedHeight(0);
        m_label->clear();
        showNext(); // Show the next message after the current one
    });
}
