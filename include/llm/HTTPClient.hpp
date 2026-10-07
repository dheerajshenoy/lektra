#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <functional>
#include <string>

// One tool call the model asked for (OpenAI "tool_calls" entry).
struct LLMToolCall
{
    QString id;
    QString name;
    QString arguments; // JSON text, as the model wrote it
};

class HTTPClient : public QObject
{
    Q_OBJECT
public:
    explicit HTTPClient(QObject *parent = nullptr);
    void setConfig(const QString &apiKey, const QString &model,
                   const QUrl &url);
    // Extra fields merged into the JSON request body on every send() (e.g.
    // Ollama's "think"/"stream"), from Config::LLMView::extra_body.
    void setExtraBodyFields(const QJsonObject &fields);
    // Text for a "system" message sent first with every request. Called on
    // each send(), so it can reflect the current state (e.g. the commands
    // that exist right now). It is not part of the stored conversation.
    void setSystemPromptProvider(std::function<QString()> provider);
    // The stored conversation (without the system prompt): what the model has
    // been sent and has answered. Replace or clear it to resume or restart a
    // chat; do not do that while a request is in flight.
    QJsonArray messages() const
    {
        return m_messages;
    }
    // Tool calls that were never answered (e.g. the chat was saved while
    // waiting for the user) are answered with "skipped" so the conversation
    // is valid to send again.
    void setMessages(const QJsonArray &messages);
    void clearMessages()
    {
        m_messages = QJsonArray();
    }
    // Sends a user message. `imageUrls` are `data:` URLs of attached images
    // (see encodeImageForModel); with images the message content is a list of
    // text and image parts, as vision models expect.
    void send(const QString &text, const QStringList &imageUrls = {});
    // Tools (OpenAI "tools" format) the model may call; empty means none.
    void setTools(const QJsonArray &tools)
    {
        m_tools = tools;
    }
    // After the model asked for tool calls (toolCallsRequested): records what
    // one of them returned. Every call needs a result before resume().
    void addToolResult(const QString &callId, const QString &content);
    // Sends the conversation again, with the tool results, so the model can
    // continue.
    void resume();
    // Aborts any in-flight request. Call this before the client is
    // destroyed (e.g. on application shutdown) so a pending request
    // doesn't keep the connection open past the app closing.
    void closeConnection() noexcept;
    // Stops the reply that is being received (the user pressed Stop). What
    // was streamed so far is kept as the model's answer; emits cancelled().
    // Does nothing if no request is in flight.
    void cancel() noexcept;
    // Fires a lightweight HEAD probe against the configured URL to check
    // whether the LLM server (local or remote) is currently reachable —
    // emits connectionStatusChanged() once it completes. Any real HTTP
    // response (even an error status like 404/405) counts as "connected";
    // only a network-layer failure (connection refused, host not found,
    // timed out) counts as "disconnected".
    void checkConnection() noexcept;

signals:
    // Full final reply text — always emitted exactly once per send(),
    // streaming or not, once the response is complete.
    void replyReceived(const QString &text);
    // Only emitted when extra_body.stream is true: one incremental token/
    // text fragment per Server-Sent-Events chunk, as it arrives.
    void streamChunkReceived(const QString &deltaText);
    // Emitted right after replyReceived() when the reply asks for tool calls
    // (the text may then be empty).
    void toolCallsRequested(const QList<LLMToolCall> &calls);
    void errorOccurred(const QString &message);
    // The request was stopped with cancel(); `partialText` is what had been
    // streamed (empty if nothing, or when not streaming).
    void cancelled(const QString &partialText);
    // Emitted after every checkConnection() call completes.
    void connectionStatusChanged(bool connected);
    // The details of the same check: the HTTP status (0 if there was no HTTP
    // reply), how long it took, and the error text when it failed.
    void connectionChecked(bool connected, int httpStatus, qint64 latencyMs,
                           const QString &error);

private:
    void dispatch();
    void mergeToolCallDelta(const QJsonObject &delta);

    QNetworkAccessManager m_networkManager;
    QUrl m_url;
    QString m_apiKey, m_model;
    QJsonArray m_messages;
    std::function<QString()> m_systemPrompt;
    QJsonObject m_extraBodyFields;
    QJsonArray m_tools;
    // Tool calls being assembled from streamed fragments, by their index.
    QMap<int, QJsonObject> m_streamToolCalls;
    QNetworkReply *m_activeReply = nullptr;
    bool m_cancelled             = false;
    // Server-Sent-Events parsing state for the in-flight streamed request.
    QByteArray m_sseBuffer;
    QString m_streamedText;
    // In-flight checkConnection() probe, if any — a fresh call aborts a
    // still-pending one rather than letting two race.
    QNetworkReply *m_probeReply = nullptr;
};
