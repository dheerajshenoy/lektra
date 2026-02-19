#include "HttpStreamClient.hpp"

#include <QDebug>
#include <QMetaObject>

static size_t
curlCallback(char *buffer, size_t size, size_t nitems, void *ptr)
{
    size_t total_size = size * nitems;
    std::string data(buffer, total_size);

    HttpStreamClient *client = static_cast<HttpStreamClient *>(ptr);
    QMetaObject::invokeMethod(client, [client, data]()
    { client->handleDataReceived(data); }, Qt::QueuedConnection);

    return total_size;
}

static size_t
discardCallback(char *buffer, size_t size, size_t nitems, void *ptr)
{
    (void)buffer;
    (void)ptr;
    return size * nitems;
}

HttpStreamClient::HttpStreamClient()
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
    m_headers = curl_slist_append(m_headers, "Content-Type: application/json");
}

HttpStreamClient::~HttpStreamClient()
{
    if (m_worker.joinable())
        m_worker.join();
    curl_slist_free_all(m_headers);
    curl_global_cleanup();
}

bool
HttpStreamClient::probe(const std::string &url, std::string *error) const
{
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        if (error)
            *error = "Failed to initialize CURL";
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discardCallback);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 500L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 300L);

    CURLcode res = curl_easy_perform(curl);
    long status  = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
    {
        if (error)
            *error = curl_easy_strerror(res);
        return false;
    }

    if (status < 200 || status >= 300)
    {
        if (error)
            *error = "HTTP " + std::to_string(status);
        return false;
    }

    return true;
}

void
HttpStreamClient::sendRequest(const std::string &data)
{
    if (m_in_flight.exchange(true))
    {
        emit requestFailed("Request already in flight");
        return;
    }

    if (m_url.empty())
    {
        m_in_flight.store(false);
        emit requestFailed("URL is not set");
        return;
    }

    if (m_worker.joinable())
        m_worker.join();

    m_buffer.clear();
    const std::string url     = m_url;
    const std::string payload = data;

    m_worker = std::thread([this, url, payload]()
    {
        CURL *curl = curl_easy_init();
        if (!curl)
        {
            QMetaObject::invokeMethod(this, [this]() {
                emit requestFailed("Failed to initialize CURL");
            }, Qt::QueuedConnection);
            m_in_flight.store(false);
            return;
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, m_headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)payload.size());

        // Helpful error text
        char errbuf[CURL_ERROR_SIZE] = {0};
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
        {
            QMetaObject::invokeMethod(this, [this, res]() {
                emit requestFailed(curl_easy_strerror(res));
            }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(this, [this]()
        {
            if (!m_buffer.empty())
            {
                emit dataReceived(m_buffer);
                m_buffer.clear();
            }
        }, Qt::QueuedConnection);
        curl_easy_cleanup(curl);
        m_in_flight.store(false);
    });
}

void
HttpStreamClient::handleDataReceived(const std::string &data) noexcept
{
    m_buffer.append(data);
    size_t pos = 0;
    while ((pos = m_buffer.find('\n')) != std::string::npos)
    {
        std::string line = m_buffer.substr(0, pos);
        m_buffer.erase(0, pos + 1);
        if (!line.empty())
            emit dataReceived(line);
    }
}
