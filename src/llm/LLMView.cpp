#include "LLMView.hpp"

#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>

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
    connect(m_http_client, &HTTPClient::connectionStatusChanged, this,
            &LLMView::updateConnectionIndicator);

    // Poll periodically so the indicator reflects reality even when the
    // user isn't actively chatting — most relevant for a local server
    // (e.g. Ollama) that might not be running yet or gets stopped/restarted.
    m_connection_check_timer = new QTimer(this);
    m_connection_check_timer->setInterval(15000);
    connect(m_connection_check_timer, &QTimer::timeout, m_http_client,
            &HTTPClient::checkConnection);
    m_connection_check_timer->start();
    m_http_client->checkConnection();
}

void
LLMView::closeConnection() noexcept
{
    if (m_connection_check_timer)
        m_connection_check_timer->stop();
    if (m_http_client)
        m_http_client->closeConnection();
}

bool
LLMView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input_edit && event->type() == QEvent::KeyPress)
    {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if ((keyEvent->key() == Qt::Key_Return
             || keyEvent->key() == Qt::Key_Enter)
            && (keyEvent->modifiers() & Qt::ShiftModifier))
        {
            sendMessage();
            return true; // consume — don't insert a newline
        }
    }
    return QDockWidget::eventFilter(watched, event);
}

void
LLMView::initUI()
{
    m_container = new QWidget(this);
    setWidget(m_container);
    m_container->setMinimumWidth(300);

    m_connection_indicator = new QLabel(tr("● Checking..."), m_container);
    m_connection_indicator->setStyleSheet("color: gray;");

    m_messages_widget = new QWidget();
    m_messages_layout = new QVBoxLayout(m_messages_widget);
    m_messages_layout->addStretch();

    m_scroll_area = new QScrollArea(m_container);
    m_scroll_area->setWidgetResizable(true);
    m_scroll_area->setWidget(m_messages_widget);

    m_input_edit = new QTextEdit(m_container);
    m_input_edit->setMaximumHeight(80);
    m_input_edit->setPlaceholderText(tr("Type your message here..."));
    m_input_edit->installEventFilter(this);

    m_send_button = new QPushButton(tr("Send"), m_container);

    m_status_label = new QLabel(tr("Thinking..."), m_container);
    m_status_label->setStyleSheet("color: gray; font-style: italic;");
    m_status_label->hide();

    m_layout = new QVBoxLayout();
    m_layout->addWidget(m_connection_indicator);
    m_layout->addWidget(m_scroll_area);
    m_layout->addWidget(m_status_label);

    QHBoxLayout *input_layout = new QHBoxLayout();
    input_layout->addWidget(m_input_edit);
    input_layout->addWidget(m_send_button);
    m_layout->addLayout(input_layout);

    m_container->setLayout(m_layout);

    connect(m_send_button, &QPushButton::clicked, this, &LLMView::sendMessage);
}

void
LLMView::addBubble(ChatBubble *bubble) noexcept
{
    m_messages_layout->insertWidget(m_messages_layout->count() - 1, bubble);
    scrollToBottom();
}

void
LLMView::updateConnectionIndicator(bool connected) noexcept
{
    if (connected)
    {
        m_connection_indicator->setText(tr("● Connected"));
        m_connection_indicator->setStyleSheet("color: #2e9e44;");
    }
    else
    {
        m_connection_indicator->setText(tr("● Disconnected"));
        m_connection_indicator->setStyleSheet("color: #c0392b;");
    }
}

void
LLMView::scrollToBottom() noexcept
{
    QTimer::singleShot(0, this, [this]
    {
        QScrollBar *bar = m_scroll_area->verticalScrollBar();
        bar->setValue(bar->maximum());
    });
}

void
LLMView::sendMessage()
{
    if (m_awaiting_response)
        return; // a request is already in flight

    QString user_input = m_input_edit->toPlainText().trimmed();
    if (user_input.isEmpty())
        return;

    addBubble(new ChatBubble(ChatBubble::Role::User, user_input,
                             m_messages_widget));

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
        // Already shown progressively via appendStreamChunk() — nothing
        // left to do but reset for the next exchange.
        m_streaming_active         = false;
        m_active_assistant_bubble  = nullptr;
        m_streaming_markdown.clear();
        return;
    }

    addBubble(new ChatBubble(ChatBubble::Role::Assistant, response,
                             m_messages_widget));
}

void
LLMView::appendStreamChunk(const QString &deltaText)
{
    if (!m_streaming_active)
    {
        m_streaming_active = true;
        m_status_label->hide(); // first token arrived — no longer "thinking"
        m_streaming_markdown.clear();
        m_active_assistant_bubble
            = new ChatBubble(ChatBubble::Role::Assistant, QString(),
                             m_messages_widget);
        addBubble(m_active_assistant_bubble);
    }

    m_streaming_markdown += deltaText;
    m_active_assistant_bubble->setText(m_streaming_markdown);
    scrollToBottom();
}

void
LLMView::displayError(const QString &message)
{
    setAwaitingResponse(false);
    m_streaming_active        = false;
    m_active_assistant_bubble = nullptr;
    m_streaming_markdown.clear();

    addBubble(
        new ChatBubble(ChatBubble::Role::Error, message, m_messages_widget));
}
