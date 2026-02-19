#include "OllamaProvider.hpp"

#include "../../../json.hpp"

#include <QDebug>

OllamaProvider::OllamaProvider() : LLM::Provider()
{
    m_client = new HttpStreamClient();
    m_client->setURL(m_base_url + "/api/chat");

    connect(m_client, &HttpStreamClient::dataReceived, this,
            [this](const std::string &data)
    {
        auto j = nlohmann::json::parse(data, nullptr, false);
        if (j.is_discarded())
            return;

        if (j.contains("error"))
        {
            emit requestFailed(j["error"].get<std::string>());
            return;
        }

        if (j.contains("done") && j["done"].get<bool>())
        {
            if (!m_current_assistant.empty())
            {
                m_history.push_back({"assistant", m_current_assistant});
                m_current_assistant.clear();
            }
            emit streamFinished();
            return;
        }

        if (j.contains("message") && j["message"].contains("content"))
        {
            const std::string token
                = j["message"]["content"].get<std::string>();
            m_current_assistant += token;
            emit dataReceived(j["message"]["content"].get<std::string>());
        }
    });
}

OllamaProvider::~OllamaProvider()
{
    delete m_client;
}

void
OllamaProvider::trackSystemPrompt() noexcept
{
    if (m_system_prompt != m_last_system_prompt)
    {
        m_history.clear();
        m_system_prompt_sent = false;
        m_last_system_prompt = m_system_prompt;
    }
}

bool
OllamaProvider::checkServerAvailable() noexcept
{
    std::string error;
    if (m_client->probe(m_health_url, &error))
        return true;

    emit requestFailed("Ollama server not reachable: " + error);
    return false;
}

void
OllamaProvider::chat_stream(const LLM::Request &request)
{
    trackSystemPrompt();

    if (!checkServerAvailable())
        return;

    if (m_system_prompt.empty())
    {
        qDebug() << "------------------------------------------";
        qDebug() << "The System Prompt is Empty!";
        qDebug() << "------------------------------------------";
    }

    if (m_model_name.empty())
    {
        qDebug() << "------------------------------------------";
        qDebug() << "Mode name is empty";
        qDebug() << "------------------------------------------";
    }

    nlohmann::json j;
    j["model"]    = m_model_name;
    j["stream"]   = true;
    j["messages"] = nlohmann::json::array();

    if (!m_system_prompt.empty() && !m_system_prompt_sent)
    {
        m_history.push_back({"system", m_system_prompt});
        m_system_prompt_sent = true;
    }

    m_history.push_back({"user", request.prompt});

    for (const auto &msg : m_history)
        j["messages"].push_back({{"role", msg.role}, {"content", msg.content}});

    j["max_tokens"] = request.max_tokens;
    // j["temperature"] = request.temperature;
    const std::string data = j.dump();

    m_current_assistant.clear();
    m_client->sendRequest(data);
}
