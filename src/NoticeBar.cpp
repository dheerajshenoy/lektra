#include "NoticeBar.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

NoticeBar::NoticeBar(QWidget *parent) : QFrame(parent)
{
    setObjectName("noticeBar");
    setFocusPolicy(Qt::NoFocus);

    QColor accent = palette().color(QPalette::Highlight);
    accent.setAlpha(40);
    QColor line = palette().color(QPalette::Mid);
    setStyleSheet(QString("QFrame#noticeBar { background: rgba(%1, %2, %3, %4); "
                          "border-top: 1px solid %5; }")
                      .arg(accent.red())
                      .arg(accent.green())
                      .arg(accent.blue())
                      .arg(accent.alpha())
                      .arg(line.name()));

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(10, 4, 6, 4);
    row->setSpacing(8);

    m_label = new QLabel(this);
    m_label->setWordWrap(true);
    row->addWidget(m_label, 1);

    m_buttons       = new QWidget(this);
    m_button_layout = new QHBoxLayout(m_buttons);
    m_button_layout->setContentsMargins(0, 0, 0, 0);
    m_button_layout->setSpacing(6);
    row->addWidget(m_buttons);

    auto *close = new QPushButton(QStringLiteral("×"), this);
    close->setObjectName("noticeClose");
    close->setFlat(true);
    close->setFocusPolicy(Qt::NoFocus);
    close->setCursor(Qt::PointingHandCursor);
    close->setToolTip(tr("Close"));
    close->setFixedSize(22, 22);
    connect(close, &QPushButton::clicked, this, [this] { finish(true); });
    row->addWidget(close);

    hide();
}

void
NoticeBar::post(const Notice &notice)
{
    m_queue.enqueue(notice);
    if (!m_showing)
        showNext();
}

void
NoticeBar::showNext()
{
    if (m_queue.isEmpty())
    {
        m_showing = false;
        hide();
        return;
    }

    m_showing = true;
    m_current = m_queue.dequeue();
    m_label->setText(m_current.text);

    while (QLayoutItem *item = m_button_layout->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
    for (const Action &action : std::as_const(m_current.actions))
    {
        auto *button = new QPushButton(action.text, m_buttons);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &QPushButton::clicked, this, [this, callback = action.callback]
        {
            // The notice is gone before the action runs, so an action can post
            // another one.
            finish(false);
            if (callback)
                callback();
        });
        m_button_layout->addWidget(button);
    }
    show();
}

void
NoticeBar::finish(bool closed)
{
    const auto onClose = m_current.onClose;
    m_current          = {};
    m_showing          = false;
    if (closed && onClose)
        onClose();
    showNext();
}
