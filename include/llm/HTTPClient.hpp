#pragma once

#include <QJsonArray>
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
    void send(const QString &text);
    // Aborts any in-flight request. Call this before the client is
    // destroyed (e.g. on application shutdown) so a pending request
    // doesn't keep the connection open past the app closing.
    void closeConnection() noexcept;

signals:
    // Full final reply text — always emitted exactly once per send(),
    // streaming or not, once the response is complete.
    void replyReceived(const QString &text);
    // Only emitted when extra_body.stream is true: one incremental token/
    // text fragment per Server-Sent-Events chunk, as it arrives.
    void streamChunkReceived(const QString &deltaText);
    void errorOccurred(const QString &message);

private:
    QNetworkAccessManager m_networkManager;
    QUrl m_url;
    QString m_apiKey, m_model;
    QJsonArray m_messages;
    QJsonObject m_extraBodyFields;
    QNetworkReply *m_activeReply = nullptr;
    // Server-Sent-Events parsing state for the in-flight streamed request.
    QByteArray m_sseBuffer;
    QString m_streamedText;
};
