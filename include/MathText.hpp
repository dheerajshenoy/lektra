#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QString>

// Finding LaTeX math in text and putting rendered formulas into rich text.
// Shared by the annotation comments and the LLM panel. Formulas are drawn by
// renderMath() (MicroTeX), so this needs WITH_MICROTEX.
namespace mathtext
{
// One formula of a text.
struct Span
{
    qsizetype start = 0, end = 0; // the whole thing, signs included
    QString latex;                // what is between the signs
    bool display    = false;      // $$...$$ or \[...\], on a line of its own
};

// Length of the backtick run at `i`.
qsizetype
backtickRun(const QString &text, qsizetype i);

// The end of an inline code span that starts at `i` (a backtick), so math
// signs inside code are left alone; `i` itself if the span is not closed.
qsizetype
skipCodeSpan(const QString &text, qsizetype i);

// Whether the character at `i` is escaped with a backslash.
bool
escaped(const QString &text, qsizetype i);

// The display formulas ($$...$$ and \[...\]) of `text`. One that is not
// closed (yet), or is empty, is not a formula. With `skipCode` the signs in
// `...` code spans are left alone.
QList<Span>
findDisplay(const QString &text, bool skipCode);

// The inline formulas ($...$ and \(...\)) of `text`. Like Pandoc, an opening
// $ must be followed by something other than a space and a closing $ must not
// follow a space or come before a digit, so "costs $5 or $6" is not math.
QList<Span>
findInline(const QString &text, bool skipCode);

// An image of a formula as an inline <img>, centred on the text line.
QString
imageHtml(const QImage &image);

// Plain text with formulas as HTML: the text escaped, newlines as <br>, each
// formula an image (display formulas on a line of their own, centred). A
// formula that does not parse is left as it was written. Returns an empty
// string if the text has no formula, so the caller can keep showing it as
// plain text.
QString
commentToHtml(const QString &text, int pixelSize, const QColor &color,
              int maxWidth, qreal dpr);
} // namespace mathtext
