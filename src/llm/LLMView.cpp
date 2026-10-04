#include "LLMView.hpp"

#include "ImageEncode.hpp"
#include "ProviderInfo.hpp"

#include <QAbstractTextDocumentLayout>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeData>
#include <QAction>
#include <QHelpEvent>
#include <QToolTip>
#include <QDateTime>
#include <QJsonArray>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
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
    setAcceptDrops(true);
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
    connect(m_http_client, &HTTPClient::connectionChecked, this,
            [this](bool ok, int status, qint64 latency, const QString &error)
    {
        m_conn_known   = true;
        m_conn_ok      = ok;
        m_conn_status  = status;
        m_conn_latency = latency;
        m_conn_error   = error;
        m_conn_time    = QDateTime::currentDateTime();
    });

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
LLMView::setHistoryFolder(const QString &folder)
{
    m_store = ChatStore(m_config.llm_view.save_history ? folder : QString());
    updateChatButtons();
}

void
LLMView::updateChatButtons()
{
    // Switching chats while a reply is on its way would mix the two up.
    m_history_button->setVisible(m_store.isEnabled());
    m_history_button->setEnabled(!m_awaiting_response);
    m_new_chat_button->setEnabled(!m_awaiting_response);
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
    if (watched == m_connection_indicator && event->type() == QEvent::ToolTip)
    {
        auto *help = static_cast<QHelpEvent *>(event);
        QToolTip::showText(help->globalPos(), connectionTooltip(),
                           m_connection_indicator);
        return true;
    }

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
    m_connection_indicator->setObjectName("llmConnectionIndicator");
    // The tooltip is built when it is shown so it is always current.
    m_connection_indicator->installEventFilter(this);

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

    m_input_edit = new ChatInput(m_input_frame);
    m_input_edit->setObjectName("llmInputEdit");
    m_input_edit->setAcceptRichText(false);
    m_input_edit->setFrameShape(QFrame::NoFrame);
    m_input_edit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_input_edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_input_edit->document()->setDocumentMargin(2);
    m_input_edit->setPlaceholderText(
        tr("Ask something...  (Shift+Enter to send)"));
    m_input_edit->installEventFilter(this);
    m_input_edit->setHandlers([this](const QImage &image) { attachImage(image); },
                              [this](const QString &path) { attachFile(path); });

    m_send_button = new QToolButton(m_input_frame);
    m_send_button->setObjectName("llmSendButton");
    m_send_button->setCursor(Qt::PointingHandCursor);
    m_send_button->setToolTip(tr("Send (Shift+Enter)"));
    m_send_button->setFixedSize(kSendButtonSize, kSendButtonSize);
    m_send_button->setIconSize(QSize(18, 18));
    m_send_button->setFocusPolicy(Qt::NoFocus);
    m_send_button->setEnabled(false);

    // "+" opens the menu of things that can be attached.
    m_attach_button = new QToolButton(m_input_frame);
    m_attach_button->setObjectName("llmAttachButton");
    m_attach_button->setText(QStringLiteral("+"));
    m_attach_button->setToolTip(tr("Attach an image (you can also paste or drop one)"));
    m_attach_button->setCursor(Qt::PointingHandCursor);
    m_attach_button->setFixedSize(kSendButtonSize, kSendButtonSize);
    m_attach_button->setPopupMode(QToolButton::InstantPopup);
    m_attach_button->setFocusPolicy(Qt::NoFocus);
    m_attach_menu = new QMenu(m_attach_button);
    m_attach_button->setMenu(m_attach_menu);
    connect(m_attach_menu, &QMenu::aboutToShow, this, [this]
    {
        m_attach_menu->clear();
        m_attach_menu->addAction(tr("Image file..."), this, [this]
        {
            const QStringList files = QFileDialog::getOpenFileNames(
                this, tr("Attach image"), QString(),
                tr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif *.tif *.tiff);;All files (*)"));
            for (const QString &f : files)
                attachFile(f);
        });
        QAction *page = m_attach_menu->addAction(tr("Current page"), this, [this]
        {
            const QImage image = m_image_sources.currentPage();
            if (!attachImage(image) && image.isNull())
                flashNote(tr("Could not get an image of the page"));
        });
        page->setEnabled(static_cast<bool>(m_image_sources.currentPage));
        QAction *region = m_attach_menu->addAction(tr("Region of the page..."), this, [this]
        {
            flashNote(tr("Drag a box over the part of the page to attach"));
            m_image_sources.pickRegion([this](const QImage &image)
            {
                if (!attachImage(image))
                    flashNote(tr("Nothing was selected"));
            });
        });
        region->setEnabled(static_cast<bool>(m_image_sources.pickRegion));
    });

    // Thumbnails of what is attached, above the input.
    m_attachment_bar    = new QWidget(m_container);
    m_attachment_layout = new QHBoxLayout(m_attachment_bar);
    m_attachment_layout->setContentsMargins(4, 0, 4, 0);
    m_attachment_layout->setSpacing(6);
    m_attachment_bar->hide();

    auto *input_row = new QHBoxLayout(m_input_frame);
    input_row->setContentsMargins(6, 6, 6, 6);
    input_row->setSpacing(6);
    input_row->addWidget(m_attach_button, 0, Qt::AlignBottom);
    input_row->addWidget(m_input_edit, 1);
    input_row->addWidget(m_send_button, 0, Qt::AlignBottom);

    m_status_label = new QLabel(tr("Thinking..."), m_container);
    m_status_label->setStyleSheet("color: gray; font-style: italic;");
    m_status_label->hide();

    m_layout = new QVBoxLayout();
    m_layout->setContentsMargins(8, 8, 8, 8);
    m_layout->setSpacing(8);
    // Top bar: connection state on the left, chat controls on the right.
    m_history_button = new QToolButton(m_container);
    m_history_button->setObjectName("llmHistoryButton");
    m_history_button->setText(tr("History"));
    m_history_button->setToolTip(tr("Earlier chats"));
    m_history_button->setPopupMode(QToolButton::InstantPopup);
    m_history_button->setAutoRaise(true);
    m_history_menu = new QMenu(m_history_button);
    m_history_button->setMenu(m_history_menu);
    connect(m_history_menu, &QMenu::aboutToShow, this,
            &LLMView::refreshHistoryMenu);

    m_new_chat_button = new QToolButton(m_container);
    m_new_chat_button->setObjectName("llmNewChatButton");
    m_new_chat_button->setText(tr("New chat"));
    m_new_chat_button->setToolTip(tr("Start a new chat"));
    m_new_chat_button->setAutoRaise(true);
    connect(m_new_chat_button, &QToolButton::clicked, this, &LLMView::newChat);

    auto *top_bar = new QHBoxLayout();
    top_bar->setContentsMargins(0, 0, 0, 0);
    top_bar->addWidget(m_connection_indicator, 1);
    top_bar->addWidget(m_history_button);
    top_bar->addWidget(m_new_chat_button);
    m_layout->addLayout(top_bar);
    m_layout->addWidget(m_scroll_area);
    m_layout->addWidget(m_status_label);
    m_layout->addWidget(m_attachment_bar);
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
        && (!m_input_edit->toPlainText().trimmed().isEmpty()
            || !m_attachments.isEmpty()));
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
                "QToolButton#llmSendButton:disabled { background: %6; }"
                "QToolButton#llmAttachButton { background: transparent; "
                "border: none; border-radius: %7px; font-size: 20px; "
                "color: %8; }"
                "QToolButton#llmAttachButton:hover { background: %9; }"
                "QToolButton#llmAttachButton::menu-indicator { image: none; }")
            .arg(rgba(pal.color(QPalette::Base)), rgba(border), rgba(accent),
                 rgba(accent.lighter(115)), rgba(accent.darker(115)), rgba(off))
            .arg(kSendButtonSize / 2)
            .arg(pal.color(QPalette::Text).name(), rgba(off)));

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

QString
LLMView::connectionTooltip() const
{
    const auto &c = m_config.llm_view;
    const QUrl url(c.api_url);
    // never show credentials or query strings that may be part of a URL
    const QString endpoint = url.adjusted(QUrl::RemoveUserInfo | QUrl::RemoveQuery
                                          | QUrl::RemoveFragment)
                                 .toString();

    QString status;
    if (!m_conn_known)
        status = tr("checking...");
    else if (m_conn_ok)
        status = tr("connected") + (m_conn_status > 0 ? QStringLiteral(" (HTTP %1, %2 ms)")
                                                            .arg(m_conn_status)
                                                            .arg(m_conn_latency)
                                                      : QStringLiteral(" (%1 ms)").arg(m_conn_latency));
    else
        status = tr("not reachable") + (m_conn_error.isEmpty() ? QString()
                                                               : QStringLiteral(": ") + m_conn_error.toHtmlEscaped());
    if (m_conn_known)
        status += QStringLiteral("<br><span style='color:gray'>%1 %2</span>")
                      .arg(tr("checked"), QLocale().toString(m_conn_time.time(), QLocale::ShortFormat));

    const bool streaming = c.extra_body.value(QStringLiteral("stream")).toBool();
    const int messages   = static_cast<int>(m_http_client->messages().size());

    auto row = [](const QString &label, const QString &value)
    { return QStringLiteral("<tr><td style='color:gray; padding-right:10px'>%1</td><td>%2</td></tr>").arg(label, value); };

    QString html = QStringLiteral("<table>");
    html += row(tr("Model"), c.model.toHtmlEscaped());
    html += row(tr("Provider"), providerName(url).toHtmlEscaped());
    html += row(tr("Endpoint"), endpoint.toHtmlEscaped());
    html += row(tr("API key"), c.api_key.isEmpty() ? tr("not set") : tr("set"));
    html += row(tr("Streaming"), streaming ? tr("on") : tr("off"));
    html += row(tr("Status"), status);
    html += row(tr("This chat"),
                messages == 0 ? tr("empty")
                              : tr("%1 messages sent to the model").arg(messages));
    html += row(tr("Run scripts"), c.auto_run ? tr("automatically") : tr("on request (Run button)"));
    html += row(tr("Save chats"), m_store.isEnabled() ? tr("on") : tr("off"));
    html += QStringLiteral("</table>");
    return html;
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
    if (user_input.isEmpty() && m_attachments.isEmpty())
        return;

    // Attached images are scaled and encoded for the model now; the bubble
    // shows small previews of them.
    QStringList image_urls;
    QList<QImage> previews;
    for (const QImage &image : std::as_const(m_attachments))
    {
        const QString url = encodeImageForModel(image);
        if (url.isEmpty())
            continue;
        image_urls << url;
        previews << thumbnailFor(image, 120);
    }

    auto *bubble = new ChatBubble(ChatBubble::Role::User, user_input,
                                  m_messages_widget);
    bubble->setThumbnails(previews);
    addBubble(bubble);
    // Saved together with the reply (or its error), so a stored chat never
    // has a question the stored conversation has not seen.
    record(QStringLiteral("user"), user_input, /*save=*/false,
           static_cast<int>(image_urls.size()));

    // Tell the model what its last script did. The transcript shows only what
    // the user typed.
    QString to_send = user_input;
    if (!m_pending_result.isEmpty())
    {
        to_send = m_pending_result + QStringLiteral("\n\n") + user_input;
        m_pending_result.clear();
    }

    setAwaitingResponse(true);
    m_http_client->send(to_send, image_urls);

    // Clear the input edit for the next message
    m_input_edit->clear();
    clearAttachments();
}

void
LLMView::setAwaitingResponse(bool awaiting)
{
    m_awaiting_response = awaiting;
    updateSendEnabled();
    updateChatButtons();
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

    record(QStringLiteral("assistant"), response);
    addScriptActions(response);
}

void
LLMView::addScriptActions(const QString &reply, bool allowAutoRun)
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
        auto *label = new QLabel(
            lines == 1 ? tr("Lua script, 1 line")
                       : tr("Lua script, %1 lines").arg(lines),
            bar);
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

        connect(copy, &QPushButton::clicked, this, [this, code, copy]
        {
            QApplication::clipboard()->setText(code);
            // Show that the click did something, then go back to "Copy".
            copy->setText(tr("\u2713 Copied"));
            QTimer::singleShot(1500, copy, [this, copy]
            { copy->setText(tr("Copy")); });
        });
        connect(run, &QPushButton::clicked, this,
                [this, code, run] { runScript(code, run); });

        addBubble(bar);
        scripts.append({code, run});
    }

    if (allowAutoRun && m_config.llm_view.auto_run)
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
    record(r.ok ? QStringLiteral("result") : QStringLiteral("error"), shown);
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
    record(QStringLiteral("error"), message);
}

// ---------------------------------------------------------------------------
// chat history
// ---------------------------------------------------------------------------

void
LLMView::record(const QString &kind, const QString &text, bool save, int images)
{
    QJsonObject entry{{"kind", kind}, {"text", text}};
    if (images > 0)
        entry["images"] = images;
    m_transcript.append(entry);
    if (save)
        saveChat();
}

void
LLMView::saveChat()
{
    if (!m_store.isEnabled() || m_transcript.isEmpty())
        return;

    if (m_chat_id.isEmpty())
    {
        m_chat_id      = ChatStore::newId();
        m_chat_created = QDateTime::currentDateTime();
    }
    if (m_chat_title.isEmpty())
    {
        for (const QJsonValue &v : std::as_const(m_transcript))
            if (v.toObject().value("kind").toString() == QLatin1String("user"))
            {
                m_chat_title = ChatStore::titleFrom(v.toObject().value("text").toString());
                break;
            }
    }

    ChatStore::Chat chat;
    chat.id         = m_chat_id;
    chat.title      = m_chat_title;
    chat.created    = m_chat_created;
    chat.updated    = QDateTime::currentDateTime();
    chat.messages   = withoutImages(m_http_client->messages());
    chat.transcript = m_transcript;
    m_store.save(chat);
}

void
LLMView::clearTranscriptWidgets()
{
    // the last item of the layout is the stretch that keeps messages at the top
    while (m_messages_layout->count() > 1)
    {
        QLayoutItem *item = m_messages_layout->takeAt(0);
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_active_assistant_bubble = nullptr;
    m_streaming_active        = false;
    m_streaming_markdown.clear();
}

void
LLMView::newChat()
{
    if (m_awaiting_response)
        return;
    clearTranscriptWidgets();
    m_http_client->clearMessages();
    m_transcript = QJsonArray();
    m_chat_id.clear();
    m_chat_title.clear();
    m_pending_result.clear();
    clearAttachments();
    m_input_edit->setFocus();
}

void
LLMView::loadChat(const QString &id)
{
    if (m_awaiting_response || id == m_chat_id)
        return;
    const auto chat = m_store.load(id);
    if (!chat)
        return;

    clearTranscriptWidgets();
    m_http_client->setMessages(chat->messages);
    m_transcript   = chat->transcript;
    m_chat_id      = chat->id;
    m_chat_title   = chat->title;
    m_chat_created = chat->created;
    m_pending_result.clear();

    for (const QJsonValue &v : std::as_const(m_transcript))
    {
        const QString kind = v.toObject().value("kind").toString();
        const QString text = v.toObject().value("text").toString();
        if (kind == QLatin1String("user"))
        {
            // the images themselves are not saved, only that there were some
            const int images = v.toObject().value("images").toInt();
            QString shown = text;
            if (images > 0)
            {
                const QString note = images == 1 ? tr("1 image attached")
                                                 : tr("%1 images attached").arg(images);
                shown = QStringLiteral("*") + note + QStringLiteral("*")
                        + (text.isEmpty() ? QString() : QStringLiteral("\n\n") + text);
            }
            addBubble(new ChatBubble(ChatBubble::Role::User, shown, m_messages_widget));
        }
        else if (kind == QLatin1String("assistant"))
        {
            addBubble(new ChatBubble(ChatBubble::Role::Assistant, text, m_messages_widget));
            addScriptActions(text, /*allowAutoRun=*/false); // never re-run old scripts
        }
        else if (kind == QLatin1String("result"))
            addBubble(new ChatBubble(ChatBubble::Role::Result, text, m_messages_widget));
        else
            addBubble(new ChatBubble(ChatBubble::Role::Error, text, m_messages_widget));
    }
    scrollToBottom();
}

void
LLMView::deleteCurrentChat()
{
    if (m_chat_id.isEmpty())
        return;
    m_store.remove(m_chat_id);
    newChat();
}

void
LLMView::refreshHistoryMenu()
{
    m_history_menu->clear();

    const QList<ChatStore::Summary> chats = m_store.list();
    if (chats.isEmpty())
        m_history_menu->addAction(tr("No saved chats"))->setEnabled(false);
    for (const ChatStore::Summary &c : chats)
    {
        const QString when = QLocale().toString(c.updated, QLocale::ShortFormat);
        auto *action = m_history_menu->addAction(
            QStringLiteral("%1   \u00B7   %2").arg(c.title, when));
        action->setCheckable(true);
        action->setChecked(c.id == m_chat_id);
        const QString id = c.id;
        connect(action, &QAction::triggered, this, [this, id] { loadChat(id); });
    }

    m_history_menu->addSeparator();
    QAction *del = m_history_menu->addAction(tr("Delete this chat"));
    del->setEnabled(!m_chat_id.isEmpty());
    connect(del, &QAction::triggered, this, [this] { deleteCurrentChat(); });
    QAction *all = m_history_menu->addAction(tr("Delete all chats..."));
    all->setEnabled(!chats.isEmpty());
    connect(all, &QAction::triggered, this, [this]
    {
        if (QMessageBox::question(this, tr("Delete all chats"),
                                  tr("Delete every saved chat? This cannot be undone."))
            != QMessageBox::Yes)
            return;
        m_store.removeAll();
        newChat();
    });
}

// ---------------------------------------------------------------------------
// images
// ---------------------------------------------------------------------------

void
LLMView::setImageSources(ImageSources sources)
{
    m_image_sources = std::move(sources);
}

bool
LLMView::attachImage(const QImage &image)
{
    constexpr int kMaxAttachments = 6;
    if (image.isNull())
        return false;
    if (m_attachments.size() >= kMaxAttachments)
    {
        flashNote(tr("At most %1 images per message").arg(kMaxAttachments));
        return false;
    }
    m_attachments.append(image);
    rebuildAttachmentBar();
    updateSendEnabled();
    return true;
}

bool
LLMView::attachFile(const QString &path)
{
    constexpr qint64 kMaxFileBytes = 25 * 1024 * 1024;
    if (QFileInfo(path).size() > kMaxFileBytes)
    {
        flashNote(tr("That file is too large to attach"));
        return false;
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull())
    {
        flashNote(tr("Could not read that image"));
        return false;
    }
    return attachImage(image);
}

void
LLMView::clearAttachments()
{
    m_attachments.clear();
    rebuildAttachmentBar();
    updateSendEnabled();
}

void
LLMView::rebuildAttachmentBar()
{
    while (QLayoutItem *item = m_attachment_layout->takeAt(0))
    {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_attachment_bar->setVisible(!m_attachments.isEmpty());

    for (int i = 0; i < m_attachments.size(); ++i)
    {
        auto *chip = new QFrame(m_attachment_bar);
        chip->setObjectName("llmAttachment");
        chip->setStyleSheet(
            QString("QFrame#llmAttachment { border: 1px solid %1; border-radius: 8px; }")
                .arg(palette().color(QPalette::Mid).name()));
        auto *row = new QHBoxLayout(chip);
        row->setContentsMargins(3, 3, 3, 3);
        row->setSpacing(2);

        auto *thumb = new QLabel(chip);
        thumb->setPixmap(QPixmap::fromImage(thumbnailFor(m_attachments[i], 48)));
        auto *remove = new QToolButton(chip);
        remove->setObjectName("llmAttachmentRemove");
        remove->setText(QStringLiteral("\u00D7"));
        remove->setToolTip(tr("Remove"));
        remove->setAutoRaise(true);
        remove->setCursor(Qt::PointingHandCursor);
        connect(remove, &QToolButton::clicked, this, [this, i]
        {
            if (i < m_attachments.size())
                m_attachments.removeAt(i);
            rebuildAttachmentBar();
            updateSendEnabled();
        });
        row->addWidget(thumb);
        row->addWidget(remove, 0, Qt::AlignTop);
        m_attachment_layout->addWidget(chip);
    }
    m_attachment_layout->addStretch();
}

void
LLMView::flashNote(const QString &note)
{
    m_status_label->setText(note);
    m_status_label->show();
    QTimer::singleShot(2500, this, [this]
    {
        m_status_label->setText(tr("Thinking..."));
        m_status_label->setVisible(m_awaiting_response);
    });
}

QJsonArray
LLMView::withoutImages(const QJsonArray &messages)
{
    QJsonArray out;
    for (const QJsonValue &m : messages)
    {
        QJsonObject message = m.toObject();
        if (message.value("content").isArray())
        {
            QJsonArray parts;
            for (const QJsonValue &part : message.value("content").toArray())
            {
                if (part.toObject().value("type").toString() == QLatin1String("image_url"))
                    parts.append(QJsonObject{{"type", "text"},
                                             {"text", "[image omitted]"}});
                else
                    parts.append(part);
            }
            message["content"] = parts;
        }
        out.append(message);
    }
    return out;
}

void
LLMView::dragEnterEvent(QDragEnterEvent *event)
{
    const QMimeData *mime = event->mimeData();
    if (mime->hasImage() || !ChatInput::imageFiles(mime).isEmpty())
        event->acceptProposedAction();
    else
        QDockWidget::dragEnterEvent(event);
}

void
LLMView::dropEvent(QDropEvent *event)
{
    const QMimeData *mime = event->mimeData();
    if (mime->hasImage())
    {
        attachImage(qvariant_cast<QImage>(mime->imageData()));
        event->acceptProposedAction();
        return;
    }
    const QStringList files = ChatInput::imageFiles(mime);
    if (!files.isEmpty())
    {
        for (const QString &f : files)
            attachFile(f);
        event->acceptProposedAction();
        return;
    }
    QDockWidget::dropEvent(event);
}
