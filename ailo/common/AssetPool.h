#pragma once

#include <string>
#include <unordered_map>

#include "common/slot_map.h"
#include "entt/container/dense_map.hpp"
#include "entt/core/type_info.hpp"

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

class AssetPoolBase {
public:
    using key_type_base = uint64_t;

public:
    virtual ~AssetPoolBase() = default;

    void gc(Engine& engine) {
        for (auto key : m_gcList) {
            destroy(engine, key);
        }
        m_gcList.clear();
    }

    virtual void reset(Engine& engine) {
        m_gcList.clear();
    }

protected:
    void destroyLater(uint64_t key) {
        m_gcList.push_back(key);
    }

    virtual void destroy(Engine& engine, key_type_base key) = 0;

private:
    std::vector<key_type_base> m_gcList;
};

template<typename TAsset>
class AssetPool : public AssetPoolBase {
public:
    using key_type = dod::slot_map<TAsset>::key;

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

        auto ptr = m_assets.get(it->second);
        if (ptr->m_refCount == 0) {
            return {};
        }

        return { ptr };
    }

    void erase(const key_type& key) {
        destroyLater(static_cast<uint64_t>(key));
    }

    void reset(Engine& engine) override {
        AssetPoolBase::reset(engine);

        for (auto& asset : m_assets) {
            if constexpr (has_destroy<TAsset>) {
                asset.destroy(engine);
            }
        }

        m_pathIndex.clear();
        m_assets.clear();
    }

protected:
    void destroy(Engine& engine, key_type_base _key) override {
        auto key = static_cast<key_type>(_key);
        if constexpr (has_destroy<TAsset>) {
            m_assets.get(key)->destroy(engine);
        }

        m_assets.erase(key);
    }

private:
    dod::slot_map<TAsset> m_assets;
    std::unordered_map<std::string, key_type> m_pathIndex;
};

template<typename T>
class enable_asset_ptr {
    uint32_t m_refCount = 0;
    AssetPool<T>::key_type m_key = {};
    AssetPool<T>* m_pool = nullptr;

    friend class AssetPool<T>;
    friend class asset_ptr<T>;
};

class AssetManager {
public:
    template<class TAsset, typename...Args>
    asset_ptr<TAsset> emplace(const std::string& path, Args&&... args) {
        return assure<TAsset>().emplace(path, std::forward<Args>(args)...);
    }

    template<class TAsset, typename...Args>
    asset_ptr<TAsset> emplace(assets::no_path tag, Args&&... args) {
        return assure<TAsset>().emplace(tag, std::forward<Args>(args)...);
    }

    template<class TAsset>
    asset_ptr<TAsset> get(const std::string& path) {
        return assure<TAsset>().get(path);
    }

    void gc(Engine& engine) {
        for (auto [key, pool] : m_pools) {
            pool->gc(engine);
        }
    }

    void reset(Engine& engine) {
        for (auto [key, pool] : m_pools) {
            pool->reset(engine);
        }
    }

    using id_type = entt::id_type;

    template<class T>
    using type_hash = entt::type_hash<T>;

    template<class TAsset>
    AssetPool<TAsset>& assure() {
        const id_type id = type_hash<TAsset>::value();
        auto it = m_pools.find(id);
        if (it == m_pools.end()) {
            auto ptr = std::make_shared<AssetPool<TAsset>>();
            m_pools.emplace(id, ptr);
            return *ptr;
        }
        return static_cast<AssetPool<TAsset>&>(*it->second);
    }

private:
    entt::dense_map<id_type, std::shared_ptr<AssetPoolBase>> m_pools;
};

} // namespace ailo
