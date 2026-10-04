#pragma once

#include <QJsonArray>
#include <functional>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <string>

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
    void send(const QString &text);
    // Aborts any in-flight request. Call this before the client is
    // destroyed (e.g. on application shutdown) so a pending request
    // doesn't keep the connection open past the app closing.
    void closeConnection() noexcept;
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
    void errorOccurred(const QString &message);
    // Emitted after every checkConnection() call completes.
    void connectionStatusChanged(bool connected);

private:
    QNetworkAccessManager m_networkManager;
    QUrl m_url;
    QString m_apiKey, m_model;
    QJsonArray m_messages;
    std::function<QString()> m_systemPrompt;
    QJsonObject m_extraBodyFields;
    QNetworkReply *m_activeReply = nullptr;
    // Server-Sent-Events parsing state for the in-flight streamed request.
    QByteArray m_sseBuffer;
    QString m_streamedText;
    // In-flight checkConnection() probe, if any — a fresh call aborts a
    // still-pending one rather than letting two race.
    QNetworkReply *m_probeReply = nullptr;
};
