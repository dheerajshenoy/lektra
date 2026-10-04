#include "HTTPClient.hpp"

#include <QDateTime>

HTTPClient::HTTPClient(QObject *parent) : QObject(parent) {}

void
HTTPClient::setConfig(const QString &apiKey, const QString &model,
                      const QUrl &url)
{
    m_apiKey = apiKey;
    m_model  = model;
    m_url    = url;
}

void
HTTPClient::setExtraBodyFields(const QJsonObject &fields)
{
    m_extraBodyFields = fields;
}

void
HTTPClient::setSystemPromptProvider(std::function<QString()> provider)
{
    m_systemPrompt = std::move(provider);
}

void
HTTPClient::send(const QString &userText, const QStringList &imageUrls)
{
    if (imageUrls.isEmpty())
    {
        m_messages.append(QJsonObject{{"role", "user"}, {"content", userText}});
    }
    else
    {
        QJsonArray parts;
        if (!userText.isEmpty())
            parts.append(QJsonObject{{"type", "text"}, {"text", userText}});
        for (const QString &url : imageUrls)
            parts.append(QJsonObject{
                {"type", "image_url"},
                {"image_url", QJsonObject{{"url", url}}},
            });
        m_messages.append(QJsonObject{{"role", "user"}, {"content", parts}});
    }

    QNetworkRequest request(m_url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization",
                         QString("Bearer %1").arg(m_apiKey).toUtf8());

    QJsonArray messages;
    if (m_systemPrompt)
    {
        const QString system = m_systemPrompt();
        if (!system.isEmpty())
            messages.append(
                QJsonObject{{"role", "system"}, {"content", system}});
    }
    for (const QJsonValue &m : std::as_const(m_messages))
        messages.append(m);

    QJsonObject body{{"model", m_model}, {"messages", messages}};
    for (auto it = m_extraBodyFields.constBegin();
         it != m_extraBodyFields.constEnd(); ++it)
        body[it.key()] = it.value();

    // When extra_body.stream is true, the server responds with
    // Server-Sent-Events chunks (`data: {...}\n\n`, OpenAI format) instead
    // of one JSON document — those must be parsed incrementally as they
    // arrive, not concatenated and parsed as a single document at the end.
    const bool streaming = body.value("stream").toBool();

    QNetworkReply *reply
        = m_networkManager.post(request, QJsonDocument(body).toJson());
    m_activeReply = reply;
    m_sseBuffer.clear();
    m_streamedText.clear();

    if (streaming)
    {
        connect(reply, &QNetworkReply::readyRead, this, [this, reply]
        {
            m_sseBuffer += reply->readAll();

            int idx;
            while ((idx = m_sseBuffer.indexOf("\n\n")) != -1)
            {
                const QByteArray event = m_sseBuffer.left(idx);
                m_sseBuffer.remove(0, idx + 2);

                for (const QByteArray &line : event.split('\n'))
                {
                    if (!line.startsWith("data:"))
                        continue;

                    const QByteArray payload = line.mid(5).trimmed();
                    if (payload.isEmpty() || payload == "[DONE]")
                        continue;

                    const auto chunk = QJsonDocument::fromJson(payload);
                    const QString delta
                        = chunk["choices"][0]["delta"]["content"].toString();
                    if (!delta.isEmpty())
                    {
                        m_streamedText += delta;
                        emit streamChunkReceived(delta);
                    }
                }
            }
        });
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply, streaming]
    {
        if (reply == m_activeReply)
            m_activeReply = nullptr;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            // The question got no answer: drop it, so the stored
            // conversation stays in step with what the model has replied to.
            if (!m_messages.isEmpty()
                && m_messages.last().toObject().value("role") == "user")
                m_messages.removeLast();
            emit errorOccurred(reply->errorString() + "\n" + reply->readAll());
            return;
        }

        QString text;
        if (streaming)
        {
            text = m_streamedText;
        }
        else
        {
            const auto doc = QJsonDocument::fromJson(reply->readAll());
            text            = doc["choices"][0]["message"]["content"].toString();
        }

        m_messages.append(
            QJsonObject{{"role", "assistant"}, {"content", text}});
        emit replyReceived(text);
    });
}

void
HTTPClient::closeConnection() noexcept
{
    if (m_activeReply)
        m_activeReply->abort(); // triggers the finished lambda above, which
                                 // clears m_activeReply and deletes the reply
    if (m_probeReply)
        m_probeReply->abort();
}

void
HTTPClient::checkConnection() noexcept
{
    if (m_url.isEmpty())
    {
        emit connectionStatusChanged(false);
        return;
    }

    if (m_probeReply)
        m_probeReply->abort(); // superseded by this call

    QNetworkRequest request(m_url);
    request.setTransferTimeout(2000); // ms — don't let a dead host hang the
                                       // indicator

    QNetworkReply *reply = m_networkManager.head(request);
    m_probeReply         = reply;
    const qint64 started = QDateTime::currentMSecsSinceEpoch();

    connect(reply, &QNetworkReply::finished, this, [this, reply, started]
    {
        if (reply == m_probeReply)
            m_probeReply = nullptr;
        reply->deleteLater();

        const bool connected
            = reply->error() == QNetworkReply::NoError
              || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                     .isValid();
        const QVariant status
            = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        emit connectionChecked(
            connected, status.isValid() ? status.toInt() : 0,
            QDateTime::currentMSecsSinceEpoch() - started,
            reply->error() == QNetworkReply::NoError ? QString()
                                                     : reply->errorString());
        emit connectionStatusChanged(connected);
    });
}
