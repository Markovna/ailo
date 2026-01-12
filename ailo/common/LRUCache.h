#pragma once

#include <list>
#include <unordered_map>

namespace ailo {

template<typename Key, typename Value, typename HashFunction = std::hash<Key>>
class LRUCache {
private:
    using List = std::list<std::pair<Key, Value>>;
    using Cache = std::unordered_map<Key, typename List::iterator, HashFunction>;

public:
    using iterator = List::iterator;

    explicit LRUCache(size_t cap) : m_capacity(cap) {}

    Value* get(const Key& key) {
        auto it = m_cache.find(key);
        if (it == m_cache.end()) {
            return nullptr;
        }
        m_items.splice(m_items.begin(), m_items, it->second);
        return &it->second->second;
    }

    template<class... Args >
    std::pair<iterator, bool> tryEmplace(const Key& key, Args&&... args) {
        auto it = m_cache.find(key);

        if (it != m_cache.end()) {
            m_items.splice(m_items.begin(), m_items, it->second);
            return {it->second, false};
        }

        if (m_items.size() == m_capacity) {
            Key key_to_delete = m_items.back().first;
            m_items.pop_back();
            m_cache.erase(key_to_delete);
        }

        m_items.emplace_front(std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple(std::forward<Args>(args)...));
        m_cache[key] = m_items.begin();
        return {m_items.begin(), true};
    }

    void clear() {
        m_cache.clear();
        m_cache.clear();
    }

private:
    size_t m_capacity;
    List m_items;
    Cache m_cache;
};

}
