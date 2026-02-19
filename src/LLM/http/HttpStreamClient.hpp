#pragma once

#include <QObject>
#include <atomic>
#include <curl/curl.h>
#include <string>
#include <thread>

class HttpStreamClient : public QObject
{
    Q_OBJECT
public:
    HttpStreamClient();
    ~HttpStreamClient();

    inline void setURL(const std::string &url)
    {
        m_url = url;
    }

    inline const std::string &getURL() const
    {
        return m_url;
    }

    bool probe(const std::string &url, std::string *error) const;
    void sendRequest(const std::string &data);
    void handleDataReceived(const std::string &data) noexcept;

signals:
    void dataReceived(const std::string &data);
    void requestFailed(const std::string &error);

private:
    std::string m_url;
    struct curl_slist *m_headers{nullptr};
    std::string m_buffer;
    std::thread m_worker;
    std::atomic<bool> m_in_flight{false};
};
