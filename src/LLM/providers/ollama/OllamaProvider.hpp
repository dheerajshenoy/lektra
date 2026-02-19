#pragma once

#include "../../http/HttpStreamClient.hpp"
#include "../../providers/LLMProvider.hpp"

#include <QObject>
#include <string>
#include <vector>

class OllamaProvider : public LLM::Provider
{
    Q_OBJECT
public:
    OllamaProvider();
    ~OllamaProvider();
    void chat_stream(const LLM::Request &request) override;

private:
    struct Message
    {
        std::string role;
        std::string content;
    };

    void trackSystemPrompt() noexcept;
    bool checkServerAvailable() noexcept;

    HttpStreamClient *m_client{nullptr};
    std::string m_base_url{"http://localhost:11434"};
    std::string m_health_url{"http://localhost:11434/api/tags"};
    std::string m_current_assistant{};
    std::string m_last_system_prompt{};
    std::vector<Message> m_history{};
    bool m_system_prompt_sent{false};
};
