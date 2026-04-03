#pragma once

#include <functional>
#include <list>
#include <unordered_map>

template <typename K, typename V> class LRUCache
{
public:
    using EvictCallback = std::function<void(V &)>;

    LRUCache() {}

    inline void setCapacity(const size_t capacity)
    {
        m_capacity = capacity;
    }

    inline void setCallback(EvictCallback onEvict)
    {
        m_onEvict = onEvict;
    }

    V *get(const K &key)
    {
        auto it = m_map.find(key);
        if (it == m_map.end())
            return nullptr;

        m_list.splice(m_list.begin(), m_list, it->second.second);
        return &(it->second.first);
    }

    V *find(const K &key)
    {
        auto it = m_map.find(key);
        if (it == m_map.end())
            return nullptr;
        return &(it->second.first);
    }

    inline bool has(const K &key) const
    {
        return m_map.find(key) != m_map.end();
    }

    void put(const K &key, V value)
    {
        if (m_capacity == 0)
            return;
        remove(key);
        if (m_map.size() >= m_capacity)
            evict();
        m_list.push_front(key);
        m_map[key] = {std::move(value), m_list.begin()};
    }

    inline size_t size() const
    {
        return m_list.size();
    }

    void remove(const K &key)
    {
        auto it = m_map.find(key);
        if (it == m_map.end())
            return;

        if (m_onEvict)
            m_onEvict(it->second.first);

        m_list.erase(it->second.second);
        m_map.erase(it);
    }

    void clear()
    {
        if (m_onEvict)
        {
            for (auto &entry : m_map)
                m_onEvict(entry.second.first);
        }

        m_map.clear();
        m_list.clear();
    }

private:
    void evict() noexcept
    {
        if (m_list.empty())
            return;

        K lastKey = m_list.back();
        auto it   = m_map.find(lastKey);

        if (m_onEvict)
            m_onEvict(it->second.first);

        m_map.erase(it);
        m_list.pop_back();
    }

    size_t m_capacity{0};
    std::list<K> m_list;
    std::unordered_map<K, std::pair<V, typename std::list<K>::iterator>> m_map;
    EvictCallback m_onEvict;
};
