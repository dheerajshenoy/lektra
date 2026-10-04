#include "ChatBubble.hpp"

#include "LuaHighlight.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
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
    layout->setContentsMargins(8, 4, 8, 6);
    layout->setSpacing(2);

    // A flat header button: language and size, and a click collapses the block.
    piece.header = new QPushButton(frame);
    piece.header->setObjectName("chatCodeToggle");
    piece.header->setFlat(true);
    piece.header->setCursor(Qt::PointingHandCursor);
    piece.header->setFocusPolicy(Qt::NoFocus);
    piece.header->setStyleSheet(
        "QPushButton#chatCodeToggle { text-align: left; border: none; "
        "padding: 1px 0; color: gray; }");
    layout->addWidget(piece.header);

    piece.label = new QLabel(frame);
    piece.label->setTextFormat(Qt::RichText);
    piece.label->setWordWrap(true);
    piece.label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    piece.label->setMaximumWidth(kMaxBubbleWidth - 20);
    layout->addWidget(piece.label);
    piece.widget = frame;

    // The collapsed state lives on the header so the button can toggle it
    // without knowing which piece it belongs to.
    QLabel *body = piece.label;
    QPushButton *header = piece.header;
    connect(header, &QPushButton::clicked, header, [header, body]
    {
        const bool collapse = !header->property("collapsed").toBool();
        body->setVisible(!collapse);
        header->setProperty("collapsed", collapse);
        const QString title = header->property("title").toString();
        header->setText((collapse ? QStringLiteral("\u25B8 ") : QStringLiteral("\u25BE "))
                        + title);
    });
    return piece;
}

void
ChatBubble::refreshHeader(const Piece &piece, const Segment &segment)
{
    const int lines = segment.text.count(QLatin1Char('\n')) + 1;
    const QString language = segment.language.isEmpty() ? QStringLiteral("code")
                                                        : segment.language;
    const QString size = lines == 1 ? tr("1 line") : tr("%1 lines").arg(lines);
    const QString title = QStringLiteral("%1 \u00B7 %2").arg(language, size);
    piece.header->setProperty("title", title);
    const bool collapsed = piece.header->property("collapsed").toBool();
    piece.header->setText((collapsed ? QStringLiteral("\u25B8 ") : QStringLiteral("\u25BE "))
                          + title);
}

void
ChatBubble::updatePiece(Piece &piece, const Segment &segment)
{
    if (!segment.code)
    {
        piece.label->setText(segment.text);
        return;
    }
    if (segment.language == QLatin1String("lua"))
        piece.label->setText(luaToHtml(segment.text, palette()));
    else
        piece.label->setText(plainCodeToHtml(segment.text));
    refreshHeader(piece, segment);
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

void
ChatBubble::setThumbnails(const QList<QImage> &thumbnails)
{
    delete m_thumbnails;
    m_thumbnails = nullptr;
    if (thumbnails.isEmpty())
        return;

    m_thumbnails = new QWidget(this);
    auto *row    = new QHBoxLayout(m_thumbnails);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    for (const QImage &image : thumbnails)
    {
        auto *label = new QLabel(m_thumbnails);
        label->setPixmap(QPixmap::fromImage(image));
        label->setStyleSheet("border-radius: 4px;");
        row->addWidget(label);
    }
    row->addStretch();
    m_content->insertWidget(0, m_thumbnails);

    // An image-only message has no text: do not leave an empty gap under it.
    if (m_segments.size() == 1 && !m_segments[0].code && m_segments[0].text.isEmpty()
        && !m_pieces.isEmpty())
        m_pieces[0].widget->hide();
}
