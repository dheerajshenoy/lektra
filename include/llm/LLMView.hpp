#pragma once

#include "ChatBubble.hpp"
#include "Config.hpp"
#include "HTTPClient.hpp"

#include <QDockWidget>
#include <QToolButton>
#include <QTextEdit>
#include <QVBoxLayout>

class QFrame;
class QLabel;
class QScrollArea;
class QTimer;

class LLMView : public QDockWidget
{
public:
    LLMView(const Config &config, QWidget *parent = nullptr);

    // Aborts any in-flight request. Call this before the application
    // closes so a pending request doesn't keep the connection open past
    // shutdown.
    void closeConnection() noexcept;

protected:
    // Installed on m_input_edit so Shift+Return sends the message instead
    // of inserting a newline (plain Return still inserts a newline, the
    // QTextEdit default). Also tracks focus to highlight the input frame.
    bool eventFilter(QObject *watched, QEvent *event) override;
    // Re-derives the input colours and send icon when the palette changes
    // (e.g. light/dark theme switch).
    void changeEvent(QEvent *event) override;

private:
    void initUI();
    void sendMessage();
    void displayResponse(const QString &response);
    // Appends one incremental streamed fragment (extra_body.stream = true)
    // to the display as it arrives, instead of waiting for the full reply.
    void appendStreamChunk(const QString &deltaText);
    void displayError(const QString &message);
    // Disables the Send button and shows/hides the "Thinking..." label for
    // the duration of one request/response exchange.
    void setAwaitingResponse(bool awaiting);
    // Adds a bubble as the last message in the transcript and scrolls to it.
    void addBubble(ChatBubble *bubble) noexcept;
    void scrollToBottom() noexcept;
    // Reflects an HTTPClient::connectionStatusChanged() result in
    // m_connection_indicator.
    void updateConnectionIndicator(bool connected) noexcept;
    // Colours of the input frame and send button, taken from the palette so
    // they follow the theme.
    void updateInputStyle() noexcept;
    // The send button is only enabled when there is text and no request in
    // flight.
    void updateSendEnabled() noexcept;
    // Grows the input with its text, from one line up to a few, then scrolls.
    void adjustInputHeight() noexcept;

    HTTPClient *m_http_client       = nullptr;
    QScrollArea *m_scroll_area      = nullptr;
    QWidget *m_messages_widget      = nullptr;
    QVBoxLayout *m_messages_layout  = nullptr;
    QFrame *m_input_frame           = nullptr;
    QTextEdit *m_input_edit         = nullptr;
    QToolButton *m_send_button      = nullptr;
    bool m_updating_style           = false;
    QLabel *m_status_label          = nullptr;
    // Shows "Connected"/"Disconnected" for the configured LLM endpoint,
    // refreshed by m_connection_check_timer.
    QLabel *m_connection_indicator      = nullptr;
    QTimer *m_connection_check_timer    = nullptr;
    QVBoxLayout *m_layout           = nullptr;
    QWidget *m_container            = nullptr;
    // The in-progress assistant bubble while a streamed reply is arriving —
    // nullptr when no stream is active. Only this one bubble is touched per
    // chunk, unlike the old whole-transcript re-render.
    ChatBubble *m_active_assistant_bubble = nullptr;
    // Markdown source accumulated for the in-progress streamed bubble only
    // (not the whole conversation) — reset per exchange.
    QString m_streaming_markdown;
    // True while a streamed response is being appended chunk-by-chunk, so
    // displayResponse() (which always fires once, streamed or not) knows
    // the bubble was already created/filled and doesn't add a second one.
    bool m_streaming_active  = false;
    // True from sendMessage() until the exchange fully completes (reply or
    // error) — kept disabled through the whole streamed response, not just
    // until the first chunk, because HTTPClient only tracks one in-flight
    // request: a second send() while one is still streaming would corrupt
    // its SSE parsing state.
    bool m_awaiting_response = false;

    const Config &m_config;
};
