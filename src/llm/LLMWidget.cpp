#include "LLMWidget.hpp"

#include "providers/ollama/OllamaProvider.hpp"

#include <QDir>
#include <QFile>
#include <QFont>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QKeySequence>
#include <QMessageBox>
#include <QShortcut>
#include <QTextCursor>

LLMWidget::LLMWidget(const Config &config, QWidget *parent) noexcept
    : QWidget(parent), m_config(config)
{
    initGui();
    initProvider();
}

void
LLMWidget::initProvider() noexcept
{
    if (m_config.llm.provider == "ollama")
    {
        m_provider = new OllamaProvider();

        std::string promptText;

#if defined(__linux__)
        // const QString rolePath
        //     = QDir(APP_INSTALL_PREFIX).filePath("share/lektra/role.txt");
        const QString rolePath = "/home/dheeraj/Gits/lektra/src/llm/role.txt";
#endif

        QFile roleFile(rolePath);

        if (!roleFile.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QMessageBox::critical(
                this, "Error",
                "Role error while instantiating LLM. Please contact support");

#ifndef NDEBUG
            qDebug() << "LLMWidget::getSystemPrompt() role.txt not found! "
                        "Exiting";
#endif
            return;
        }

        promptText = roleFile.readAll().toStdString();

        m_provider->setModel(m_config.llm.model);
        m_provider->setSystemPrompt(promptText);
    }
    else
    {
        m_chat_edit->append("<b>LLM error:</b> Unsupported provider: "
                            + QString::fromStdString(m_config.llm.provider));
        return;
    }

    connect(m_provider, &LLM::Provider::dataReceived, this,
            [this](const std::string &data)
    {
        if (data.empty())
            return;

        const QString token = QString::fromStdString(data);
        m_stream_buffer += token;
        auto cursor = m_chat_edit->textCursor();
        cursor.movePosition(QTextCursor::End);
        if (!m_stream_in_progress)
        {
            cursor.insertBlock();
            cursor.insertHtml("<b>LLM:</b> ");
            m_stream_in_progress = true;
        }
        cursor.insertText(token);
        m_chat_edit->setTextCursor(cursor);

        m_chat_edit->ensureCursorVisible();
    });

    connect(m_provider, &LLM::Provider::streamFinished, this, [this]()
    {
        const QString payload = m_stream_buffer.trimmed();
        m_stream_buffer.clear();
        if (!payload.isEmpty())
        {
            QJsonParseError error;
            const QJsonDocument doc
                = QJsonDocument::fromJson(payload.toUtf8(), &error);
            if (error.error == QJsonParseError::NoError && doc.isObject())
            {
                const QJsonObject obj = doc.object();
                const QString action  = obj.value("action").toString();
                if (!action.isEmpty() && action != QStringLiteral("noop"))
                {
                    QStringList args;
                    const QJsonValue argsValue = obj.value("args");
                    if (argsValue.isArray())
                    {
                        const QJsonArray arr = argsValue.toArray();
                        for (const auto &val : arr)
                        {
                            if (val.isDouble())
                                args.append(
                                    QString::number(val.toDouble(), 'g', 15));
                            else if (val.isString())
                                args.append(val.toString());
                        }
                    }
                    emit actionRequested(action, args);
                }
            }
            else
            {
                m_chat_edit->append("<b>LLM error:</b> Invalid JSON response.");
            }
        }
        if (m_stream_in_progress)
        {
            m_chat_edit->append("");
            m_stream_in_progress = false;
        }
    });

    connect(m_provider, &LLM::Provider::requestFailed, this,
            [this](const std::string &error)
    {
        m_stream_buffer.clear();
        m_chat_edit->append("<b>LLM error:</b> "
                            + QString::fromStdString(error));
        m_stream_in_progress = false;
    });
}

void
LLMWidget::sendQuery() noexcept
{
    const QString user_input = m_input_edit->toPlainText().trimmed();

    if (user_input.isEmpty())
        return;

    m_chat_edit->append("<b>User:</b> " + user_input);
    m_input_edit->clear();
    m_stream_buffer.clear();
    m_stream_in_progress = false;
    LLM::Request llm_request{.prompt     = user_input.toStdString(),
                             .max_tokens = m_config.llm.max_tokens};
    m_provider->chat_stream(llm_request);
}

void
LLMWidget::initGui() noexcept
{
    m_chat_edit  = new QTextEdit(this);
    m_input_edit = new QTextEdit(this);
    m_send_btn   = new QPushButton("Send", this);

    m_chat_edit->setAcceptRichText(true);
    m_chat_edit->setReadOnly(true);

    m_send_btn->setEnabled(false);
    m_input_edit->setPlaceholderText("Enter your message...");
    m_input_edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

    auto *layout = new QVBoxLayout(this);

    auto *input_layout = new QHBoxLayout();
    input_layout->addWidget(m_input_edit);
    input_layout->addWidget(m_send_btn);

    layout->addWidget(m_chat_edit);
    layout->addLayout(input_layout);

    setLayout(layout);

    // Ctrl + Return to send
    QShortcut *send
        = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), m_send_btn);
    connect(send, &QShortcut::activated, this, &LLMWidget::sendQuery);

    // Handle enable/disable of send button
    connect(m_input_edit, &QTextEdit::textChanged, this, [this]()
    {
        const QString text = m_input_edit->toPlainText();
        const bool enabled = !text.trimmed().isEmpty();
        if (m_send_btn->isEnabled() != enabled)
            m_send_btn->setEnabled(enabled);
    });

    connect(m_send_btn, &QPushButton::clicked, this, &LLMWidget::sendQuery);
}
