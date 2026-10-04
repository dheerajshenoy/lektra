#pragma once

#include <QString>
#include <QUrl>

// A friendly provider name for an API endpoint, for display. Unknown hosts are
// shown as their host name.
inline QString
providerName(const QUrl &url)
{
    const QString host = url.host().toLower();
    if (host.isEmpty())
        return QStringLiteral("unknown");

    struct Known
    {
        const char *needle;
        const char *name;
    };
    static const Known known[] = {
        {"generativelanguage.googleapis.com", "Google Gemini"},
        {"aiplatform.googleapis.com", "Google Vertex AI"},
        {"api.openai.com", "OpenAI"},
        {"openrouter.ai", "OpenRouter"},
        {"api.groq.com", "Groq"},
        {"api.mistral.ai", "Mistral"},
        {"api.together.xyz", "Together AI"},
        {"api.deepseek.com", "DeepSeek"},
        {"api.anthropic.com", "Anthropic"},
        {"api.x.ai", "xAI"},
        {"api.perplexity.ai", "Perplexity"},
    };
    for (const Known &k : known)
        if (host == QLatin1String(k.needle) || host.endsWith(QLatin1Char('.') + QLatin1String(k.needle)))
            return QString::fromLatin1(k.name);

    const bool local = host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1")
                       || host == QLatin1String("::1") || host == QLatin1String("0.0.0.0");
    if (local)
    {
        switch (url.port())
        {
            case 11434:
                return QStringLiteral("Ollama (local)");
            case 1234:
                return QStringLiteral("LM Studio (local)");
            case 8080:
                return QStringLiteral("llama.cpp server (local)");
            default:
                return QStringLiteral("Local server");
        }
    }
    return host;
}
