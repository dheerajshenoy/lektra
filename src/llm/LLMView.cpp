#include "LLMView.hpp"

#include <QJsonObject>
#include <QLabel>
#include <QTextCursor>
#include <QTextDocument>

LLMView::LLMView(const Config &config, QWidget *parent)
    : QDockWidget(parent), m_config(config)
{
    initUI();
    m_http_client = new HTTPClient(this);
    m_http_client->setConfig(m_config.llm_view.api_key, m_config.llm_view.model,
                             QUrl(m_config.llm_view.api_url));
    m_http_client->setExtraBodyFields(
        QJsonObject::fromVariantMap(m_config.llm_view.extra_body));

    connect(m_http_client, &HTTPClient::replyReceived, this,
            &LLMView::displayResponse);
    connect(m_http_client, &HTTPClient::streamChunkReceived, this,
            &LLMView::appendStreamChunk);
    connect(m_http_client, &HTTPClient::errorOccurred, this,
            &LLMView::displayError);
}

void
LLMView::closeConnection() noexcept
{
    if (m_http_client)
        m_http_client->closeConnection();
}

void
LLMView::initUI()
{
    m_container = new QWidget(this);
    setWidget(m_container);
    m_container->setMinimumWidth(300);

    m_response_edit = new QTextEdit(m_container);
    m_response_edit->setReadOnly(true);
    m_response_edit->setAcceptRichText(true);

    m_input_edit = new QTextEdit(m_container);
    m_input_edit->setMaximumHeight(80);
    m_input_edit->setPlaceholderText(tr("Type your message here..."));

    m_send_button = new QPushButton(tr("Send"), m_container);

    m_status_label = new QLabel(tr("Thinking..."), m_container);
    m_status_label->setStyleSheet("color: gray; font-style: italic;");
    m_status_label->hide();

    m_layout = new QVBoxLayout();
    m_layout->addWidget(m_response_edit);
    m_layout->addWidget(m_status_label);

    QHBoxLayout *input_layout = new QHBoxLayout();
    input_layout->addWidget(m_input_edit);
    input_layout->addWidget(m_send_button);
    m_layout->addLayout(input_layout);

    m_container->setLayout(m_layout);

    connect(m_send_button, &QPushButton::clicked, this, &LLMView::sendMessage);
}

void
LLMView::sendMessage()
{
    if (m_awaiting_response)
        return; // a request is already in flight

    QString user_input = m_input_edit->toPlainText().trimmed();
    if (user_input.isEmpty())
        return;

    // Display the user's message in the response edit. The label is on its
    // own line (blank line after it) so that if `user_input` opens with a
    // fenced code block, the ``` delimiter is still alone on its own line
    // and gets recognized as a fence rather than swallowed into a paragraph.
    m_response_markdown += tr("**User:**\n\n%1\n\n").arg(user_input);
    m_response_edit->document()->setMarkdown(m_response_markdown);
    m_response_edit->moveCursor(QTextCursor::End);
    m_response_edit->ensureCursorVisible();

    setAwaitingResponse(true);
    m_http_client->send(user_input);

    // Clear the input edit for the next message
    m_input_edit->clear();
}

void
LLMView::setAwaitingResponse(bool awaiting)
{
    m_awaiting_response = awaiting;
    m_send_button->setEnabled(!awaiting);
    m_status_label->setVisible(awaiting);
}

void
LLMView::displayResponse(const QString &response)
{
    setAwaitingResponse(false);

    if (m_streaming_active)
    {
        // Already shown progressively via appendStreamChunk() — just close
        // out the paragraph and reset for the next exchange.
        m_streaming_active = false;
        m_response_markdown += "\n\n";
        m_response_edit->document()->setMarkdown(m_response_markdown);
        return;
    }

    m_response_markdown += tr("**LLM:**\n\n%1\n\n").arg(response);
    m_response_edit->document()->setMarkdown(m_response_markdown);
    m_response_edit->moveCursor(QTextCursor::End);
    m_response_edit->ensureCursorVisible();
}

void
LLMView::appendStreamChunk(const QString &deltaText)
{
    if (!m_streaming_active)
    {
        m_streaming_active = true;
        m_status_label->hide(); // first token arrived — no longer "thinking"
        m_response_markdown += tr("**LLM:**\n\n");
    }

    m_response_markdown += deltaText;
    m_response_edit->document()->setMarkdown(m_response_markdown);
    m_response_edit->moveCursor(QTextCursor::End);
    m_response_edit->ensureCursorVisible();
}

void
LLMView::displayError(const QString &message)
{
    setAwaitingResponse(false);
    m_streaming_active = false;

    m_response_markdown += tr("**Error:** %1\n\n").arg(message);
    m_response_edit->document()->setMarkdown(m_response_markdown);
    m_response_edit->moveCursor(QTextCursor::End);
    m_response_edit->ensureCursorVisible();
}
