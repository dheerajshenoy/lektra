#pragma once

#include <QFrame>
#include <QList>
#include <QQueue>
#include <functional>

class QHBoxLayout;
class QLabel;
class QWidget;

// A slim banner above the statusbar for things that should be seen but must
// not get in the way: one line of text, a few buttons and a close button. It
// is not modal and never takes focus. Several notices wait their turn.
class NoticeBar : public QFrame
{
public:
    struct Action
    {
        QString text;
        std::function<void()> callback;
    };
    struct Notice
    {
        QString text;
        QList<Action> actions;
        // Called when the notice is closed with the close button (not when one
        // of the actions was used).
        std::function<void()> onClose;
    };

    explicit NoticeBar(QWidget *parent = nullptr);

    // Shows the notice now, or after the ones before it.
    void post(const Notice &notice);
    // Whether a notice is on screen or waiting.
    bool busy() const noexcept
    {
        return m_showing || !m_queue.isEmpty();
    }

private:
    void showNext();
    void finish(bool closed);

    QLabel *m_label              = nullptr;
    QWidget *m_buttons           = nullptr;
    QHBoxLayout *m_button_layout = nullptr;
    QQueue<Notice> m_queue;
    Notice m_current;
    bool m_showing = false;
};
