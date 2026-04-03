#pragma once

#include <QObject>
#include <string>

namespace LLM
{

struct Request
{
    std::string prompt;
    int max_tokens{512};
    // float temperature{0.7f}; //
};

class Provider : public QObject
{
    Q_OBJECT
public:
    explicit Provider(QObject *parent = nullptr);
    ~Provider() override;

    inline void setSystemPrompt(const std::string &prompt) noexcept
    {
        m_system_prompt = prompt;
    }

    inline void setModel(const std::string &name) noexcept
    {
        m_model_name = name;
    }

    virtual void chat_stream(const Request &request) = 0;

protected:
    std::string m_system_prompt{};
    std::string m_model_name;

signals:
    void dataReceived(const std::string &data);
    void requestFailed(const std::string &error);
    void streamFinished();
}; // namespace LLM
}; // namespace LLM
