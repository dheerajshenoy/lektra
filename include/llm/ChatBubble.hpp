#pragma once

#include <QWidget>

class QLabel;

// A single message in the LLM chat view — a messenger-style bubble, styled
// and aligned by role. One bubble per message, so a streamed update only
// touches its own widget instead of re-rendering the whole conversation.
class ChatBubble : public QWidget
{
public:
    enum class Role
    {
        User,
        Assistant,
        Error,
    };

    explicit ChatBubble(Role role, const QString &markdownText,
                        QWidget *parent = nullptr);

    // Replaces this bubble's text. Used while streaming: cheap, since it
    // only re-renders this one bubble, not the whole conversation.
    void setText(const QString &markdownText) noexcept;

private:
    QLabel *m_label = nullptr;
};
