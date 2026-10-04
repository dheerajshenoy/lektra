#include "HTTPClient.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QSet>

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

namespace
{
// A tool call as the API wants it back in the conversation: an id, the
// "function" type, and the arguments as a JSON string.
QJsonObject
normalizedToolCall(QJsonObject call, int fallbackNumber)
{
    call.remove("index");
    if (call.value("id").toString().isEmpty())
        call["id"] = QStringLiteral("call_%1").arg(fallbackNumber);
    call["type"] = QStringLiteral("function");
    QJsonObject function = call.value("function").toObject();
    if (function.value("arguments").isObject())
        function["arguments"] = QString::fromUtf8(
            QJsonDocument(function.value("arguments").toObject())
                .toJson(QJsonDocument::Compact));
    else if (function.value("arguments").toString().isEmpty())
        function["arguments"] = QStringLiteral("{}");
    call["function"] = function;
    return call;
}
} // namespace

void
HTTPClient::setMessages(const QJsonArray &messages)
{
    m_messages = messages;

    int assistant = -1;
    for (int i = static_cast<int>(m_messages.size()) - 1; i >= 0; --i)
    {
        const QJsonObject m = m_messages.at(i).toObject();
        if (m.value("role").toString() == QLatin1String("assistant")
            && m.contains("tool_calls"))
        {
            assistant = i;
            break;
        }
    }
    if (assistant < 0)
        return;

    QSet<QString> answered;
    for (int i = assistant + 1; i < m_messages.size(); ++i)
        answered.insert(
            m_messages.at(i).toObject().value("tool_call_id").toString());
    const QJsonArray calls
        = m_messages.at(assistant).toObject().value("tool_calls").toArray();
    for (const QJsonValue &call : calls)
    {
        const QString id = call.toObject().value("id").toString();
        if (!answered.contains(id))
            addToolResult(id, QStringLiteral("Skipped: not run."));
    }
}

void
HTTPClient::addToolResult(const QString &callId, const QString &content)
{
    m_messages.append(QJsonObject{{"role", "tool"},
                                  {"tool_call_id", callId},
                                  {"content", content}});
}

void
HTTPClient::resume()
{
    dispatch();
}

void
HTTPClient::mergeToolCallDelta(const QJsonObject &delta)
{
    // Servers number the fragments of one call with "index"; a few leave it
    // out and send each call whole.
    int index;
    if (delta.contains("index"))
        index = delta.value("index").toInt();
    else if (m_streamToolCalls.isEmpty()
             || !delta.value("id").toString().isEmpty())
        index = m_streamToolCalls.isEmpty() ? 0 : m_streamToolCalls.lastKey() + 1;
    else
        index = m_streamToolCalls.lastKey();

    QJsonObject call = m_streamToolCalls.value(index);
    for (auto it = delta.constBegin(); it != delta.constEnd(); ++it)
    {
        if (it.key() == QLatin1String("index"))
            continue;
        if (it.key() == QLatin1String("function"))
        {
            QJsonObject function = call.value("function").toObject();
            const QJsonObject part = it.value().toObject();
            for (auto jt = part.constBegin(); jt != part.constEnd(); ++jt)
            {
                if (jt.key() == QLatin1String("arguments"))
                    function["arguments"]
                        = function.value("arguments").toString()
                          + jt.value().toString();
                else if (!jt.value().toString().isEmpty() || !jt.value().isString())
                    function[jt.key()] = jt.value();
            }
            call["function"] = function;
        }
        else if (!it.value().isString() || !it.value().toString().isEmpty())
            call[it.key()] = it.value();
    }
    m_streamToolCalls[index] = call;
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
    dispatch();
}

void
HTTPClient::dispatch()
{
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
    if (!m_tools.isEmpty())
        body["tools"] = m_tools;

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
    m_streamToolCalls.clear();
    m_cancelled = false;

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
                    const QJsonArray toolDeltas
                        = chunk["choices"][0]["delta"]["tool_calls"].toArray();
                    for (const QJsonValue &toolDelta : toolDeltas)
                        mergeToolCallDelta(toolDelta.toObject());
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

        if (m_cancelled)
        {
            m_cancelled         = false;
            const QString partial = streaming ? m_streamedText : QString();
            if (!partial.isEmpty())
                m_messages.append(
                    QJsonObject{{"role", "assistant"}, {"content", partial}});
            else if (!m_messages.isEmpty()
                     && m_messages.last().toObject().value("role") == "user")
                m_messages.removeLast(); // no answer: the question is dropped
            emit cancelled(partial);
            return;
        }

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
        QJsonArray rawCalls;
        if (streaming)
        {
            text = m_streamedText;
            for (const QJsonObject &call : std::as_const(m_streamToolCalls))
                rawCalls.append(call);
        }
        else
        {
            const auto doc = QJsonDocument::fromJson(reply->readAll());
            text            = doc["choices"][0]["message"]["content"].toString();
            rawCalls = doc["choices"][0]["message"]["tool_calls"].toArray();
        }

        QJsonArray calls;
        QList<LLMToolCall> requested;
        for (int i = 0; i < rawCalls.size(); ++i)
        {
            const QJsonObject call = normalizedToolCall(
                rawCalls.at(i).toObject(), static_cast<int>(m_messages.size()) * 100 + i);
            const QJsonObject function = call.value("function").toObject();
            if (function.value("name").toString().isEmpty())
                continue;
            calls.append(call);
            requested.append({call.value("id").toString(),
                              function.value("name").toString(),
                              function.value("arguments").toString()});
        }

        QJsonObject assistant{{"role", "assistant"}};
        if (!text.isEmpty() || calls.isEmpty())
            assistant["content"] = text;
        if (!calls.isEmpty())
            assistant["tool_calls"] = calls;
        m_messages.append(assistant);

        emit replyReceived(text);
        if (!requested.isEmpty())
            emit toolCallsRequested(requested);
    });
}

void
HTTPClient::cancel() noexcept
{
    if (!m_activeReply)
        return;
    m_cancelled = true;
    m_activeReply->abort(); // runs the finished handler above
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
