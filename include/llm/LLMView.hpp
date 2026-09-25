#pragma once

#include "Config.hpp"
#include "HTTPClient.hpp"

#include <QDockWidget>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

class QLabel;

class LLMView : public QDockWidget
{
public:
    LLMView(const Config &config, QWidget *parent = nullptr);

    // Aborts any in-flight request. Call this before the application
    // closes so a pending request doesn't keep the connection open past
    // shutdown.
    void closeConnection() noexcept;

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

    HTTPClient *m_http_client  = nullptr;
    QTextEdit *m_response_edit = nullptr;
    QTextEdit *m_input_edit    = nullptr;
    QPushButton *m_send_button = nullptr;
    QLabel *m_status_label     = nullptr;
    QVBoxLayout *m_layout      = nullptr;
    QWidget *m_container       = nullptr;
    // Accumulated conversation as Markdown source, re-rendered into
    // m_response_edit on every update via setMarkdown() — append() only
    // auto-detects actual HTML, not Markdown syntax, so LLM output would
    // otherwise show up as literal "**bold**"/"# heading" text.
    QString m_response_markdown;
    // True while a streamed response is being appended chunk-by-chunk, so
    // displayResponse() (which always fires once, streamed or not) knows
    // the text was already shown and doesn't append it a second time.
    bool m_streaming_active  = false;
    // True from sendMessage() until the exchange fully completes (reply or
    // error) — kept disabled through the whole streamed response, not just
    // until the first chunk, because HTTPClient only tracks one in-flight
    // request: a second send() while one is still streaming would corrupt
    // its SSE parsing state.
    bool m_awaiting_response = false;

    const Config &m_config;
};
