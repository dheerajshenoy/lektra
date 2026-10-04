#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include <QWidget>

class QFrame;
class QLabel;
class QPushButton;
class QToolButton;
class QVBoxLayout;
class QEnterEvent;

// A single message in the LLM chat view — a messenger-style bubble, styled
// and aligned by role. One bubble per message, so a streamed update only
// touches its own widget instead of re-rendering the whole conversation.
//
// The message is shown as Markdown prose and code blocks: a fenced code block
// gets its own monospace box, and Lua blocks are syntax highlighted. LaTeX
// math is rendered: $$...$$ or \[...\] on its own, $...$ or \(...\) inside
// a line of text.
class ChatBubble : public QWidget
{
public:
    enum class Role
    {
        User,
        Assistant,
        Error,
        Result, // outcome of a script the user ran
    };

    explicit ChatBubble(Role role, const QString &markdownText,
                        QWidget *parent = nullptr);

    // Replaces this bubble's text. Used while streaming: cheap, since only the
    // parts that changed are touched.
    void setText(const QString &markdownText) noexcept;

    // Small previews of the images that were sent with this message, shown
    // above its text.
    void setThumbnails(const QList<QImage> &thumbnails);

    // One run of prose or one fenced code block of a message.
    struct Segment
    {
        bool code = false;
        bool math = false; // display math; text is its LaTeX source
        QString language; // lower-case fence language, code blocks only
        QString text;
        bool operator==(const Segment &) const = default;
    };
    // Splits Markdown on ``` fences, and prose on display math. A fence that
    // is not closed yet (a reply still streaming) counts as a code block up to
    // the end of the text; math that is not closed yet stays text.
    static QList<Segment> split(const QString &markdownText);

    // The message as written (Markdown, with its LaTeX and code blocks as
    // source): what the Copy button puts on the clipboard.
    QString sourceText() const { return m_source; }

protected:
    // The copy button beside the bubble is only shown while the pointer is
    // over it.
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Piece
    {
        QWidget *widget = nullptr;
        QLabel *label   = nullptr;       // the text (prose, or the code itself)
        QPushButton *header = nullptr;   // code blocks: "▾ lua · 12 lines", click to collapse
        bool collapsed = false;
    };
    Piece makePiece(const Segment &segment);
    // The prose as rich text with its inline math as images, or an empty
    // string if it has none (then it is shown as plain Markdown).
    QString proseWithMath(const QString &markdown) const;
    void updatePiece(Piece &piece, const Segment &segment);
    static void refreshHeader(const Piece &piece, const Segment &segment);

    QToolButton *m_copy_button = nullptr;
    QString m_source;
    QVBoxLayout *m_content = nullptr;
    QWidget *m_thumbnails  = nullptr;
    QList<Segment> m_segments;
    QList<Piece> m_pieces;
};
