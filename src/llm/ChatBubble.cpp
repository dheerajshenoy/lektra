#include "ChatBubble.hpp"

#include "LuaHighlight.hpp"
#include "MathRender.hpp"

#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QEnterEvent>
#include <QFontInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace
{
constexpr int kMaxBubbleWidth = 360;
constexpr int kCopyIconSize   = 16;

// A clipboard icon (or, once copied, a check mark), drawn so it is crisp at any
// scale and takes its colour from the palette.
QIcon
copyIcon(const QColor &color, bool done)
{
    QIcon icon;
    for (const qreal dpr : {1.0, 2.0})
    {
        QPixmap pm(QSize(kCopyIconSize, kCopyIconSize) * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        if (done)
        {
            QPainterPath tick;
            tick.moveTo(3.5, 8.5);
            tick.lineTo(6.8, 11.8);
            tick.lineTo(12.5, 4.8);
            p.drawPath(tick);
        }
        else
        {
            p.drawRoundedRect(QRectF(3.2, 3.6, 9.6, 11), 1.6, 1.6); // the board
            p.setBrush(color);
            p.drawRoundedRect(QRectF(5.8, 1.6, 4.4, 3.2), 1, 1); // the clip
        }
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

bool
sameKind(const ChatBubble::Segment &a, const ChatBubble::Segment &b)
{
    return a.code == b.code && a.math == b.math;
}

// Length of the backtick run at `i`.
qsizetype
backtickRun(const QString &text, qsizetype i)
{
    qsizetype n = 0;
    while (i + n < text.size() && text[i + n] == QLatin1Char('`'))
        ++n;
    return n;
}

// The end of an inline code span that starts at `i` (a backtick), so math
// signs inside code are left alone; `i` itself if the span is not closed.
qsizetype
skipCodeSpan(const QString &text, qsizetype i)
{
    const qsizetype run = backtickRun(text, i);
    for (qsizetype j = i + run; j < text.size(); ++j)
        if (text[j] == QLatin1Char('`') && backtickRun(text, j) == run)
            return j + run;
        else if (text[j] == QLatin1Char('`'))
            j += backtickRun(text, j) - 1;
    return i;
}

bool
escaped(const QString &text, qsizetype i)
{
    return i > 0 && text[i - 1] == QLatin1Char('\\');
}

// Splits prose at its display math ($$...$$ and \[...\]).
QList<ChatBubble::Segment>
splitDisplayMath(const QString &prose)
{
    QList<ChatBubble::Segment> out;
    auto addProse = [&](const QString &text)
    {
        if (!text.trimmed().isEmpty())
            out.append(
                ChatBubble::Segment{false, false, QString(), text.trimmed()});
    };

    qsizetype start = 0;
    for (qsizetype i = 0; i < prose.size(); ++i)
    {
        if (prose[i] == QLatin1Char('`'))
        {
            const qsizetype end = skipCodeSpan(prose, i);
            i = end > i ? end - 1 : i + backtickRun(prose, i) - 1;
            continue;
        }
        if (escaped(prose, i))
            continue;

        QString close;
        qsizetype open = 0;
        if (prose.mid(i, 2) == QLatin1String("$$"))
            close = QStringLiteral("$$"), open = 2;
        else if (prose.mid(i, 2) == QLatin1String("\\["))
            close = QStringLiteral("\\]"), open = 2;
        else
            continue;
        const qsizetype end = prose.indexOf(close, i + open);
        if (end < 0)
            continue; // not closed (yet): stays text
        const QString body = prose.mid(i + open, end - i - open).trimmed();
        if (body.isEmpty())
            continue;
        addProse(prose.mid(start, i - start));
        out.append(ChatBubble::Segment{false, true, QString(), body});
        i     = end + close.size() - 1;
        start = end + close.size();
    }
    addProse(prose.mid(start));
    return out;
}

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

ChatBubble::ChatBubble(Role role, const QString &markdownText, QWidget *parent)
    : QWidget(parent), m_user(role == Role::User)
{
    auto *frame = new QFrame(this);
    frame->setObjectName("chatBubbleFrame");
    frame->setStyleSheet(bubbleStyleSheet(role, palette()));

    m_content = new QVBoxLayout(frame);
    m_content->setContentsMargins(10, 7, 10, 7);
    m_content->setSpacing(6);

    // Copy button beside the bubble (on the side facing the middle of the
    // panel), shown while hovering. It keeps its space when hidden so the
    // bubble does not jump.
    m_copy_button = new QToolButton(this);
    m_copy_button->setObjectName("chatCopyButton");
    m_copy_button->setToolTip(tr("Copy this message"));
    m_copy_button->setCursor(Qt::PointingHandCursor);
    m_copy_button->setFocusPolicy(Qt::NoFocus);
    m_copy_button->setAutoRaise(true);
    m_copy_button->setIconSize(QSize(kCopyIconSize, kCopyIconSize));
    QColor iconColor = palette().color(QPalette::WindowText);
    iconColor.setAlpha(150);
    m_copy_button->setIcon(copyIcon(iconColor, false));
    QSizePolicy keep = m_copy_button->sizePolicy();
    keep.setRetainSizeWhenHidden(true);
    m_copy_button->setSizePolicy(keep);
    m_copy_button->hide();
    connect(m_copy_button, &QToolButton::clicked, this, [this, iconColor]
    {
        QApplication::clipboard()->setText(m_source);
        m_copy_button->setIcon(copyIcon(iconColor, true));
        QTimer::singleShot(1500, this, [this, iconColor]
        { m_copy_button->setIcon(copyIcon(iconColor, false)); });
    });

    auto *outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(4);
    if (role == Role::User)
    {
        frame->setMaximumWidth(kMaxBubbleWidth);
        outerLayout->addStretch();
        outerLayout->addWidget(m_copy_button, 0, Qt::AlignTop);
        outerLayout->addWidget(frame);
    }
    else
    {
        // Replies use the whole width of the panel.
        outerLayout->addWidget(frame, 1);
        outerLayout->addWidget(m_copy_button, 0, Qt::AlignTop);
    }

    setText(markdownText);
}

int
ChatBubble::mathWidth() const
{
    // Formulas wrap at the width the bubble has (replies fill the panel).
    const int panel = parentWidget() ? parentWidget()->width() - 60 : 0;
    return m_user ? kMaxBubbleWidth : std::max(kMaxBubbleWidth, panel);
}

QFont
ChatBubble::contentFont() const
{
    QFont f = font();
    if (s_font_size > 0.0f)
        f.setPointSizeF(s_font_size);
    return f;
}

void
ChatBubble::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    if (!m_source.trimmed().isEmpty())
        m_copy_button->setVisible(true);
}

void
ChatBubble::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    m_copy_button->setVisible(false);
}

QList<ChatBubble::Segment>
ChatBubble::split(const QString &markdownText)
{
    QList<Segment> out;
    Segment current;
    bool inFence = false;

    auto flush = [&]
    {
        if (current.code)
        {
            while (current.text.endsWith(QLatin1Char('\n')))
                current.text.chop(1);
            out.append(current);
        }
        else if (!current.text.trimmed().isEmpty())
        {
            out.append(splitDisplayMath(current.text));
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
            current.language = trimmed.mid(3)
                                   .trimmed()
                                   .section(QLatin1Char(' '), 0, 0)
                                   .toLower();
            inFence          = true;
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
    if (segment.math)
    {
        piece.label = new QLabel(this);
        piece.label->setAlignment(Qt::AlignCenter);
        piece.label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        piece.widget = piece.label;
        return piece;
    }
    if (!segment.code)
    {
        piece.label = new QLabel(this);
        piece.label->setFont(contentFont());
        piece.label->setTextFormat(Qt::MarkdownText);
        piece.label->setWordWrap(true);
        piece.label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        piece.widget = piece.label;
        return piece;
    }

    // Code box: darker than the bubble on a dark theme, lighter-gray on a
    // light one, so the code stands out either way.
    const QColor base = palette().color(QPalette::Base);
    const QColor box
        = base.lightness() < 128 ? base.darker(135) : base.darker(104);
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
    piece.header->setFont(contentFont());
    piece.header->setStyleSheet(
        "QPushButton#chatCodeToggle { text-align: left; border: none; "
        "padding: 1px 0; color: gray; }");
    layout->addWidget(piece.header);

    piece.label = new QLabel(frame);
    piece.label->setFont(contentFont());
    piece.label->setTextFormat(Qt::RichText);
    piece.label->setWordWrap(true);
    piece.label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(piece.label);
    piece.widget = frame;

    // The collapsed state lives on the header so the button can toggle it
    // without knowing which piece it belongs to.
    QLabel *body        = piece.label;
    QPushButton *header = piece.header;
    connect(header, &QPushButton::clicked, header, [header, body]
    {
        const bool collapse = !header->property("collapsed").toBool();
        body->setVisible(!collapse);
        header->setProperty("collapsed", collapse);
        const QString title = header->property("title").toString();
        header->setText(
            (collapse ? QStringLiteral("\u25B8 ") : QStringLiteral("\u25BE "))
            + title);
    });
    return piece;
}

void
ChatBubble::refreshHeader(const Piece &piece, const Segment &segment)
{
    const int lines        = segment.text.count(QLatin1Char('\n')) + 1;
    const QString language = segment.language.isEmpty() ? QStringLiteral("code")
                                                        : segment.language;
    const QString size  = lines == 1 ? tr("1 line") : tr("%1 lines").arg(lines);
    const QString title = QStringLiteral("%1 \u00B7 %2").arg(language, size);
    piece.header->setProperty("title", title);
    const bool collapsed = piece.header->property("collapsed").toBool();
    piece.header->setText(
        (collapsed ? QStringLiteral("\u25B8 ") : QStringLiteral("\u25BE "))
        + title);
}

// An image of a formula as an inline <img>, centred on the text line.
static QString
inlineMathImage(const QImage &image)
{
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QStringLiteral("<img src=\"data:image/png;base64,%1\" width=\"%2\" "
                          "height=\"%3\" style=\"vertical-align: middle;\"/>")
        .arg(QString::fromLatin1(png.toBase64()))
        .arg(qRound(image.width() / image.devicePixelRatio()))
        .arg(qRound(image.height() / image.devicePixelRatio()));
}

QString
ChatBubble::proseWithMath(const QString &markdown) const
{
    // Find the inline formulas ($...$ and \(...\)), leaving code spans and
    // escaped signs alone. Like Pandoc, an opening $ must be followed by
    // something other than a space and a closing $ must not follow a space or
    // come before a digit, so "costs $5 or $6" is not math.
    struct Formula
    {
        qsizetype start, end; // the whole thing, signs included
        QString latex;
    };
    QList<Formula> found;
    const QString &t = markdown;
    for (qsizetype i = 0; i < t.size(); ++i)
    {
        if (t[i] == QLatin1Char('`'))
        {
            const qsizetype end = skipCodeSpan(t, i);
            i                   = end > i ? end - 1 : i + backtickRun(t, i) - 1;
            continue;
        }
        if (escaped(t, i))
            continue;

        if (t.mid(i, 2) == QLatin1String("\\("))
        {
            const qsizetype end = t.indexOf(QLatin1String("\\)"), i + 2);
            if (end > i + 2 && end - i < 400)
            {
                found.append({i, end + 2, t.mid(i + 2, end - i - 2).trimmed()});
                i = end + 1;
            }
        }
        else if (t[i] == QLatin1Char('$') && i + 1 < t.size()
                 && t[i + 1] != QLatin1Char('$') && !t[i + 1].isSpace())
        {
            for (qsizetype j = i + 1; j < t.size() && j - i < 400; ++j)
            {
                if (t[j] == QLatin1Char('\n'))
                    break;
                if (t[j] != QLatin1Char('$') || escaped(t, j))
                    continue;
                const bool valid
                    = !t[j - 1].isSpace()
                      && (j + 1 >= t.size() || !t[j + 1].isDigit());
                if (valid)
                {
                    found.append({i, j + 1, t.mid(i + 1, j - i - 1)});
                    i = j;
                }
                break;
            }
        }
    }
    if (found.isEmpty())
        return {};

    // Markdown cannot hold the images, so they replace markers that go
    // through it unchanged, and are put back in the HTML it produces.
    const int pixelSize = QFontInfo(contentFont()).pixelSize() * 115 / 100;
    const QColor color  = palette().color(QPalette::WindowText);
    QString marked;
    qsizetype last = 0;
    for (int n = 0; n < found.size(); ++n)
    {
        marked += t.mid(last, found[n].start - last);
        marked += QChar(0xE000) + QString::number(n) + QChar(0xE001);
        last = found[n].end;
    }
    marked += t.mid(last);

    QTextDocument doc;
    doc.setMarkdown(marked);
    QString html = doc.toHtml();
    for (int n = 0; n < found.size(); ++n)
    {
        const QImage image = renderMath(found[n].latex, pixelSize, color,
                                        mathWidth(), devicePixelRatioF());
        const QString source
            = t.mid(found[n].start, found[n].end - found[n].start);
        html.replace(QChar(0xE000) + QString::number(n) + QChar(0xE001),
                     image.isNull() ? source.toHtmlEscaped()
                                    : inlineMathImage(image));
    }
    return html;
}

void
ChatBubble::updatePiece(Piece &piece, const Segment &segment)
{
    if (segment.math)
    {
        const QImage image = renderMath(
            segment.text, QFontInfo(contentFont()).pixelSize() * 115 / 100,
            palette().color(QPalette::WindowText), mathWidth() - 20,
            devicePixelRatioF());
        if (image.isNull())
        {
            // not valid LaTeX: show what the model wrote
            piece.label->setTextFormat(Qt::PlainText);
            piece.label->setAlignment(Qt::AlignLeft);
            piece.label->setText(QStringLiteral("$$ ") + segment.text
                                 + QStringLiteral(" $$"));
        }
        else
        {
            piece.label->setPixmap(QPixmap::fromImage(image));
            piece.label->setToolTip(segment.text);
        }
        return;
    }
    if (!segment.code)
    {
        const QString html = proseWithMath(segment.text);
        piece.label->setTextFormat(html.isEmpty() ? Qt::MarkdownText
                                                  : Qt::RichText);
        piece.label->setText(html.isEmpty() ? segment.text : html);
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
    m_source                = markdownText;
    QList<Segment> segments = split(markdownText);
    if (segments.isEmpty())
        segments.append(
            Segment{}); // keep a (empty) label so the bubble has a body

    // Reuse the pieces that still match (most of them, while streaming),
    // replace the ones whose kind changed, drop the extra ones.
    int keep = 0;
    while (keep < m_pieces.size() && keep < segments.size()
           && sameKind(m_segments[keep], segments[keep]))
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
    if (m_segments.size() == 1 && !m_segments[0].code && !m_segments[0].math
        && m_segments[0].text.isEmpty() && !m_pieces.isEmpty())
        m_pieces[0].widget->hide();
}
