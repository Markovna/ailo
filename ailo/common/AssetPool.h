#pragma once

#include <string>
#include <unordered_map>

#include "common/slot_map.h"

namespace ailo {

class Engine;

template<typename T>
concept has_destroy = requires(T obj, Engine& engine) { obj.destroy(engine); };

template<typename TAsset>
class AssetPool;

template<typename T>
class enable_asset_ptr;

template<typename T>
class asset_ptr {
private:
    asset_ptr(T* ptr) : m_ptr(ptr) {
        inc();
    }

public:
    asset_ptr() noexcept = default;

    asset_ptr(const asset_ptr& rhs) noexcept {
        *this = rhs;
    }

    asset_ptr(asset_ptr&& rhs) noexcept {
        *this = std::move(rhs);
        rhs.m_ptr = nullptr;
    }

    asset_ptr& operator=(const asset_ptr& rhs) noexcept {
        if (this == &rhs) { return *this; }

        reset();
        m_ptr = rhs.m_ptr;
        inc();
        return *this;
    }

    asset_ptr& operator=(asset_ptr&& rhs) noexcept {
        std::swap(m_ptr, rhs.m_ptr);
        return *this;
    }

    ~asset_ptr() {
        dec();
    }

    T& operator*() const { return *m_ptr; }
    T* operator->() const { return get(); }

    operator bool() const noexcept { return m_ptr != nullptr; }

    void reset() {
        dec();
        m_ptr = nullptr;
    }

    T* get() const {
        return m_ptr;
    }

private:
    void dec() {
        if (m_ptr == nullptr) { return; }

        if (--m_ptr->m_refCount == 0) {
            destroy(m_ptr);
        }
    }

    void inc() {
        if (m_ptr) { ++m_ptr->m_refCount; }
    }

    static void destroy(T* ptr) {
        ptr->m_pool->erase(ptr->m_key);
    }

    T* m_ptr = nullptr;

    friend class enable_asset_ptr<T>;
    friend class AssetPool<T>;
};

namespace assets {
    struct no_path {};
}

template<typename TAsset>
class AssetPool {
public:
    using key_type = typename dod::slot_map<TAsset>::key;

public:
    template<typename...Args>
    asset_ptr<TAsset> emplace(const std::string& path, Args&&... args) {
        auto key = m_assets.emplace(std::forward<Args>(args)...);
        m_pathIndex[path] = key;

        auto ptr = m_assets.get(key);
        ptr->m_key = key;
        ptr->m_pool = this;
        return { ptr };
    }

    template<typename...Args>
    asset_ptr<TAsset> emplace(assets::no_path, Args&&... args) {
        auto key = m_assets.emplace(std::forward<Args>(args)...);

        auto ptr = m_assets.get(key);
        ptr->m_key = key;
        ptr->m_pool = this;
        return { ptr };
    }

    asset_ptr<TAsset> get(const std::string& path) {
        auto it = m_pathIndex.find(path);
        if (it == m_pathIndex.end()) {
            return {};
        }

        return { m_assets.get(it->second) };
    }

    void erase(const key_type& key) {
        m_garbage.push_back(key);
    }

    void gc(Engine& engine) {
        for (auto key : m_garbage) {
            if constexpr (has_destroy<TAsset>) {
                m_assets.get(key)->destroy(engine);
            }

            m_assets.erase(key);
        }
        m_garbage.clear();
    }

    void reset(Engine& engine) {
        for (auto& asset : m_assets) {
            if constexpr (has_destroy<TAsset>) {
                asset.destroy(engine);
            }
        }

        m_pathIndex.clear();
        m_assets.clear();
        m_garbage.clear();
    }

private:
    dod::slot_map<TAsset> m_assets;
    std::unordered_map<std::string, key_type> m_pathIndex;
    std::vector<key_type> m_garbage;
};

template<typename T>
class enable_asset_ptr {
    uint32_t m_refCount = 0;
    AssetPool<T>::key_type m_key = {};
    AssetPool<T>* m_pool = nullptr;

    friend class AssetPool<T>;
    friend class asset_ptr<T>;
};


template<typename ...Types>
class AssetManager {
public:
    template<class TAsset, typename...Args>
    asset_ptr<TAsset> emplace(const std::string& path, Args&&... args) {
        return std::get<AssetPool<TAsset>>(m_pools).emplace(path, std::forward<Args>(args)...);
    }

    template<class TAsset, typename...Args>
    asset_ptr<TAsset> emplace(assets::no_path tag, Args&&... args) {
        return std::get<AssetPool<TAsset>>(m_pools).emplace(tag, std::forward<Args>(args)...);
    }

    template<class TAsset>
    asset_ptr<TAsset> get(const std::string& path) {
        return std::get<AssetPool<TAsset>>(m_pools).get(path);
    }

    void gc(Engine& engine) {
        std::apply([&](auto&&... pools) {
            (pools.gc(engine), ...);
        }, m_pools);
    }

    void reset(Engine& engine) {
        std::apply([&](auto&&... pools) {
            (pools.reset(engine), ...);
        }, m_pools);
    }

private:
    std::tuple<AssetPool<Types>...> m_pools;
};

} // namespace ailo
