#include "ChatBubble.hpp"

#include "LuaHighlight.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QVBoxLayout>

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

    m_content = new QVBoxLayout(frame);
    m_content->setContentsMargins(10, 7, 10, 7);
    m_content->setSpacing(6);

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

    setText(markdownText);
}

QList<ChatBubble::Segment>
ChatBubble::split(const QString &markdownText)
{
    QList<Segment> out;
    Segment current;
    bool inFence = false;

    auto flush = [&]
    {
        if (current.code || !current.text.trimmed().isEmpty())
        {
            if (!current.code)
                current.text = current.text.trimmed();
            else
                while (current.text.endsWith(QLatin1Char('\n')))
                    current.text.chop(1);
            out.append(current);
        }
        current = {};
    };

    for (const QString &line : markdownText.split(QLatin1Char('\n')))
    {
        const QString trimmed = line.trimmed();
        if (!inFence && trimmed.startsWith(QLatin1String("```")))
        {
            flush();
            current.code     = true;
            current.language = trimmed.mid(3).trimmed().section(QLatin1Char(' '), 0, 0).toLower();
            inFence = true;
            continue;
        }
        if (inFence && trimmed == QLatin1String("```"))
        {
            flush();
            inFence = false;
            continue;
        }
        current.text += line + QLatin1Char('\n');
    }
    flush();
    return out;
}

ChatBubble::Piece
ChatBubble::makePiece(const Segment &segment)
{
    Piece piece;
    if (!segment.code)
    {
        piece.label = new QLabel(this);
        piece.label->setTextFormat(Qt::MarkdownText);
        piece.label->setWordWrap(true);
        piece.label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        piece.label->setMaximumWidth(kMaxBubbleWidth);
        piece.widget = piece.label;
        return piece;
    }

    // Code box: darker than the bubble on a dark theme, lighter-gray on a
    // light one, so the code stands out either way.
    const QColor base = palette().color(QPalette::Base);
    const QColor box  = base.lightness() < 128 ? base.darker(135) : base.darker(104);
    auto *frame = new QFrame(this);
    frame->setObjectName("chatCodeBlock");
    frame->setStyleSheet(QString("QFrame#chatCodeBlock { background-color: %1; "
                                 "border-radius: 6px; }")
                             .arg(box.name()));
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(8, 6, 8, 6);

    piece.label = new QLabel(frame);
    piece.label->setTextFormat(Qt::RichText);
    piece.label->setWordWrap(true);
    piece.label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    piece.label->setMaximumWidth(kMaxBubbleWidth - 20);
    layout->addWidget(piece.label);
    piece.widget = frame;
    return piece;
}

void
ChatBubble::updatePiece(const Piece &piece, const Segment &segment)
{
    if (!segment.code)
        piece.label->setText(segment.text);
    else if (segment.language == QLatin1String("lua"))
        piece.label->setText(luaToHtml(segment.text, palette()));
    else
        piece.label->setText(plainCodeToHtml(segment.text));
}

void
ChatBubble::setText(const QString &markdownText) noexcept
{
    QList<Segment> segments = split(markdownText);
    if (segments.isEmpty())
        segments.append(Segment{}); // keep a (empty) label so the bubble has a body

    // Reuse the pieces that still match (most of them, while streaming),
    // replace the ones whose kind changed, drop the extra ones.
    int keep = 0;
    while (keep < m_pieces.size() && keep < segments.size()
           && m_segments[keep].code == segments[keep].code)
        ++keep;

    while (m_pieces.size() > keep)
    {
        Piece piece = m_pieces.takeLast();
        m_content->removeWidget(piece.widget);
        delete piece.widget;
    }
    for (int i = keep; i < segments.size(); ++i)
    {
        Piece piece = makePiece(segments[i]);
        m_content->addWidget(piece.widget);
        m_pieces.append(piece);
    }
    for (int i = 0; i < segments.size(); ++i)
        if (i >= keep || m_segments.value(i) != segments[i])
            updatePiece(m_pieces[i], segments[i]);

    m_segments = segments;
}
