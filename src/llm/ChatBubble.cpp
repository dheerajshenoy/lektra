#include "ChatBubble.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>

namespace
{
constexpr int kMaxBubbleWidth = 360;

QString
bubbleStyleSheet(ChatBubble::Role role, const QPalette &palette)
{
    QColor bg;
    switch (role)
    {
        case ChatBubble::Role::User:
            bg = palette.color(QPalette::Highlight);
            bg.setAlpha(60);
            break;
        case ChatBubble::Role::Assistant:
            bg = palette.color(QPalette::AlternateBase);
            break;
        case ChatBubble::Role::Error:
            bg = QColor(220, 60, 60, 60);
            break;
        case ChatBubble::Role::Result:
            bg = palette.color(QPalette::Mid);
            bg.setAlpha(45);
            break;
    }

    return QString("QFrame#chatBubbleFrame { background-color: rgba(%1, %2, "
                   "%3, %4); border-radius: 10px; }")
        .arg(bg.red())
        .arg(bg.green())
        .arg(bg.blue())
        .arg(bg.alpha());
}
} // namespace

ChatBubble::ChatBubble(Role role, const QString &markdownText,
                       QWidget *parent)
    : QWidget(parent)
{
    auto *frame = new QFrame(this);
    frame->setObjectName("chatBubbleFrame");
    frame->setStyleSheet(bubbleStyleSheet(role, palette()));

    m_label = new QLabel(frame);
    m_label->setTextFormat(Qt::MarkdownText);
    m_label->setWordWrap(true);
    m_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_label->setMaximumWidth(kMaxBubbleWidth);
    m_label->setText(markdownText);

    auto *frameLayout = new QHBoxLayout(frame);
    frameLayout->addWidget(m_label);

    auto *outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    if (role == Role::User)
    {
        outerLayout->addStretch();
        outerLayout->addWidget(frame);
    }
    else
    {
        outerLayout->addWidget(frame);
        outerLayout->addStretch();
    }
}

void
ChatBubble::setText(const QString &markdownText) noexcept
{
    m_label->setText(markdownText);
}
