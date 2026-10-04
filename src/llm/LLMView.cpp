#include "LLMView.hpp"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QTextDocument>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace
{
constexpr int kSendButtonSize = 32;
constexpr int kInputMaxLines  = 6;

// An "arrow up" glyph, drawn so it is crisp at any scale and takes its colour
// from the palette.
QPixmap
sendGlyph(const QColor &color, qreal dpr)
{
    constexpr int size = 18;
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.drawLine(QPointF(9, 15), QPointF(9, 4));
    QPainterPath head;
    head.moveTo(4.5, 8.5);
    head.lineTo(9, 4);
    head.lineTo(13.5, 8.5);
    p.drawPath(head);
    return pm;
}

QString
rgba(const QColor &c)
{
    return QString("rgba(%1, %2, %3, %4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(c.alpha());
}
} // namespace

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
LLMView::setSystemPromptProvider(std::function<QString()> provider)
{
    m_http_client->setSystemPromptProvider(std::move(provider));
}

void
LLMView::setScriptRunner(LLMScriptRunner runner)
{
    m_script_runner = std::move(runner);
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
    if (watched == m_input_edit
        && (event->type() == QEvent::FocusIn
            || event->type() == QEvent::FocusOut))
    {
        // Highlight the frame, not the bare text field, while typing.
        m_input_frame->setProperty("focused", event->type() == QEvent::FocusIn);
        m_input_frame->style()->unpolish(m_input_frame);
        m_input_frame->style()->polish(m_input_frame);
        return false;
    }

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

    // m_scroll_area->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::);

    // The input is one rounded frame holding a borderless text field and a
    // round send button, like a chat app's composer.
    m_input_frame = new QFrame(m_container);
    m_input_frame->setObjectName("llmInputFrame");
    m_input_frame->setProperty("focused", false);

    m_input_frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

    m_input_edit = new QTextEdit(m_input_frame);
    m_input_edit->setObjectName("llmInputEdit");
    m_input_edit->setAcceptRichText(false);
    m_input_edit->setFrameShape(QFrame::NoFrame);
    m_input_edit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_input_edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_input_edit->document()->setDocumentMargin(2);
    m_input_edit->setPlaceholderText(
        tr("Ask something...  (Shift+Enter to send)"));
    m_input_edit->installEventFilter(this);

    m_send_button = new QToolButton(m_input_frame);
    m_send_button->setObjectName("llmSendButton");
    m_send_button->setCursor(Qt::PointingHandCursor);
    m_send_button->setToolTip(tr("Send (Shift+Enter)"));
    m_send_button->setFixedSize(kSendButtonSize, kSendButtonSize);
    m_send_button->setIconSize(QSize(18, 18));
    m_send_button->setFocusPolicy(Qt::NoFocus);
    m_send_button->setEnabled(false);

    auto *input_row = new QHBoxLayout(m_input_frame);
    input_row->setContentsMargins(12, 6, 6, 6);
    input_row->setSpacing(8);
    input_row->addWidget(m_input_edit, 1);
    input_row->addWidget(m_send_button, 0, Qt::AlignBottom);

    m_status_label = new QLabel(tr("Thinking..."), m_container);
    m_status_label->setStyleSheet("color: gray; font-style: italic;");
    m_status_label->hide();

    m_layout = new QVBoxLayout();
    m_layout->setContentsMargins(8, 8, 8, 8);
    m_layout->setSpacing(8);
    m_layout->addWidget(m_connection_indicator);
    m_layout->addWidget(m_scroll_area);
    m_layout->addWidget(m_status_label);
    m_layout->addWidget(m_input_frame);

    m_container->setLayout(m_layout);

    updateInputStyle();
    adjustInputHeight();

    connect(m_input_edit, &QTextEdit::textChanged, this,
            &LLMView::updateSendEnabled);
    connect(m_input_edit->document()->documentLayout(),
            &QAbstractTextDocumentLayout::documentSizeChanged, this,
            [this] { adjustInputHeight(); });
    connect(m_send_button, &QToolButton::clicked, this, &LLMView::sendMessage);
}

void
LLMView::updateSendEnabled() noexcept
{
    m_send_button->setEnabled(
        !m_awaiting_response
        && !m_input_edit->toPlainText().trimmed().isEmpty());
}

void
LLMView::adjustInputHeight() noexcept
{
    QTextDocument *doc = m_input_edit->document();
    doc->setTextWidth(std::max(1, m_input_edit->viewport()->width()));

    const int line   = QFontMetrics(m_input_edit->font()).lineSpacing();
    const int pad    = 2 * static_cast<int>(doc->documentMargin());
    const int minH   = line + pad;
    const int maxH   = line * kInputMaxLines + pad;
    const int wanted = static_cast<int>(std::ceil(doc->size().height()));
    const int height = std::clamp(wanted, minH, maxH);

    // The send button is taller than one line; keep the frame from shrinking
    // below it so a single line sits centred next to the button.
    m_input_edit->setFixedHeight(std::max(height, kSendButtonSize - 12));
}

void
LLMView::updateInputStyle() noexcept
{
    if (m_updating_style)
        return;
    m_updating_style = true;

    const QPalette pal  = palette();
    const QColor accent = pal.color(QPalette::Highlight);
    QColor border       = pal.color(QPalette::Mid);
    border.setAlpha(170);
    QColor off = pal.color(QPalette::Mid);
    off.setAlpha(120);

    m_input_frame->setStyleSheet(
        QString("QFrame#llmInputFrame { background: %1; border: 1px solid %2; "
                "border-radius: 16px; }"
                "QFrame#llmInputFrame[focused=\"true\"] { border: 1px solid "
                "%3; }"
                "QTextEdit#llmInputEdit { background: transparent; border: "
                "none; selection-background-color: %3; }"
                "QToolButton#llmSendButton { background: %3; border: none; "
                "border-radius: %7px; }"
                "QToolButton#llmSendButton:hover { background: %4; }"
                "QToolButton#llmSendButton:pressed { background: %5; }"
                "QToolButton#llmSendButton:disabled { background: %6; }")
            .arg(rgba(pal.color(QPalette::Base)), rgba(border), rgba(accent),
                 rgba(accent.lighter(115)), rgba(accent.darker(115)), rgba(off))
            .arg(kSendButtonSize / 2));

    const qreal dpr = devicePixelRatioF();
    QIcon icon;
    icon.addPixmap(sendGlyph(pal.color(QPalette::HighlightedText), dpr),
                   QIcon::Normal);
    icon.addPixmap(sendGlyph(pal.color(QPalette::Base), dpr), QIcon::Disabled);
    m_send_button->setIcon(icon);

    m_updating_style = false;
}

void
LLMView::changeEvent(QEvent *event)
{
    QDockWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange && m_input_frame)
        updateInputStyle();
}

void
LLMView::addBubble(QWidget *bubble) noexcept
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

    addBubble(
        new ChatBubble(ChatBubble::Role::User, user_input, m_messages_widget));

    // Tell the model what its last script did. The transcript shows only what
    // the user typed.
    QString to_send = user_input;
    if (!m_pending_result.isEmpty())
    {
        to_send = m_pending_result + QStringLiteral("\n\n") + user_input;
        m_pending_result.clear();
    }

    setAwaitingResponse(true);
    m_http_client->send(to_send);

    // Clear the input edit for the next message
    m_input_edit->clear();
}

void
LLMView::setAwaitingResponse(bool awaiting)
{
    m_awaiting_response = awaiting;
    updateSendEnabled();
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
    }
    else
    {
        addBubble(new ChatBubble(ChatBubble::Role::Assistant, response,
                                 m_messages_widget));
    }

    addScriptActions(response);
}

void
LLMView::addScriptActions(const QString &reply)
{
    if (!m_script_runner)
        return;

    static const QRegularExpression block(
        QStringLiteral("```[ \\t]*lua[^\\n]*\\n(.*?)```"),
        QRegularExpression::DotMatchesEverythingOption
            | QRegularExpression::CaseInsensitiveOption);

    QList<QPair<QString, QPushButton *>> scripts;
    auto it = block.globalMatch(reply);
    while (it.hasNext())
    {
        const QString code = it.next().captured(1).trimmed();
        if (code.isEmpty())
            continue;

        auto *bar = new QFrame(m_messages_widget);
        bar->setObjectName("llmScriptBar");
        auto *row = new QHBoxLayout(bar);
        row->setContentsMargins(4, 0, 4, 0);

        const int lines = code.count(QLatin1Char('\n')) + 1;
        auto *label = new QLabel(tr("Lua script, %n line(s)", nullptr, lines), bar);
        label->setStyleSheet("color: gray;");
        auto *copy = new QPushButton(tr("Copy"), bar);
        copy->setFlat(true);
        copy->setCursor(Qt::PointingHandCursor);
        auto *run = new QPushButton(tr("Run"), bar);
        run->setObjectName("llmRunButton");
        run->setCursor(Qt::PointingHandCursor);
        run->setToolTip(tr("Run this script in Lektra"));

        const QPalette pal = palette();
        const QColor accent = pal.color(QPalette::Highlight);
        run->setStyleSheet(
            QString("QPushButton#llmRunButton { background: %1; color: %2; "
                    "border: none; border-radius: 10px; padding: 3px 14px; }"
                    "QPushButton#llmRunButton:hover { background: %3; }"
                    "QPushButton#llmRunButton:disabled { background: %4; }")
                .arg(accent.name(),
                     pal.color(QPalette::HighlightedText).name(),
                     accent.lighter(115).name(),
                     pal.color(QPalette::Mid).name()));

        row->addWidget(label);
        row->addStretch();
        row->addWidget(copy);
        row->addWidget(run);

        connect(copy, &QPushButton::clicked, this,
                [code] { QApplication::clipboard()->setText(code); });
        connect(run, &QPushButton::clicked, this,
                [this, code, run] { runScript(code, run); });

        addBubble(bar);
        scripts.append({code, run});
    }

    if (m_config.llm_view.auto_run)
    {
        for (const auto &[code, button] : scripts)
        {
            runScript(code, button);
            if (!button->property("lastRunOk").toBool())
                break; // do not run later scripts after a failure
        }
    }
}

void
LLMView::runScript(const QString &code, QPushButton *runButton)
{
    if (!m_script_runner)
        return;

    runButton->setEnabled(false);
    const LLMScriptResult r = m_script_runner(code);
    runButton->setProperty("lastRunOk", r.ok);
    runButton->setText(tr("Run again"));
    runButton->setEnabled(true);

    // What the user sees, and what the model is told next time.
    QString shown;
    QString told;
    if (r.ok)
    {
        if (!r.output.isEmpty())
            shown += QStringLiteral("```\n") + r.output.trimmed()
                     + QStringLiteral("\n```\n");
        if (!r.value.isEmpty())
            shown += tr("Returned:") + QStringLiteral("\n```\n") + r.value
                     + QStringLiteral("\n```\n");
        if (shown.isEmpty())
            shown = tr("Done.");
        told = QStringLiteral("[The script you wrote ran without errors.");
        if (!r.output.isEmpty())
            told += QStringLiteral(" It printed: ") + r.output.trimmed().left(1500);
        if (!r.value.isEmpty())
            told += QStringLiteral(" It returned: ") + r.value.left(1500);
        told += QStringLiteral("]");
    }
    else
    {
        if (!r.output.isEmpty())
            shown += QStringLiteral("```\n") + r.output.trimmed()
                     + QStringLiteral("\n```\n");
        shown += QStringLiteral("**") + tr("Error:") + QStringLiteral("** `")
                 + r.error + QStringLiteral("`");
        told = QStringLiteral("[The script you wrote failed: ") + r.error.left(1500)
               + QStringLiteral("]");
    }

    addBubble(new ChatBubble(r.ok ? ChatBubble::Role::Result
                                  : ChatBubble::Role::Error,
                             shown, m_messages_widget));
    m_pending_result = told;
}

void
LLMView::appendStreamChunk(const QString &deltaText)
{
    if (!m_streaming_active)
    {
        m_streaming_active = true;
        m_status_label->hide(); // first token arrived — no longer "thinking"
        m_streaming_markdown.clear();
        m_active_assistant_bubble = new ChatBubble(
            ChatBubble::Role::Assistant, QString(), m_messages_widget);
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
