#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QFrame;
class QLabel;
class QPushButton;
class QVBoxLayout;

// A single message in the LLM chat view — a messenger-style bubble, styled
// and aligned by role. One bubble per message, so a streamed update only
// touches its own widget instead of re-rendering the whole conversation.
//
// The message is shown as Markdown prose and code blocks: a fenced code block
// gets its own monospace box, and Lua blocks are syntax highlighted.
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

    // One run of prose or one fenced code block of a message.
    struct Segment
    {
        bool code = false;
        QString language; // lower-case fence language, code blocks only
        QString text;
        bool operator==(const Segment &) const = default;
    };
    // Splits Markdown on ``` fences. A fence that is not closed yet (a reply
    // still streaming) counts as a code block up to the end of the text.
    static QList<Segment> split(const QString &markdownText);

private:
    struct Piece
    {
        QWidget *widget = nullptr;
        QLabel *label   = nullptr;       // the text (prose, or the code itself)
        QPushButton *header = nullptr;   // code blocks: "▾ lua · 12 lines", click to collapse
        bool collapsed = false;
    };
    Piece makePiece(const Segment &segment);
    void updatePiece(Piece &piece, const Segment &segment);
    static void refreshHeader(const Piece &piece, const Segment &segment);

    QVBoxLayout *m_content = nullptr;
    QList<Segment> m_segments;
    QList<Piece> m_pieces;
};
