#pragma once
#include <cstdint>
#include <functional>

#include <entt/entt.hpp>
#include "Assets.h"

namespace ailo {

using asset_key = dod::slot_map_key64<class _>;

class AssetGCQueue {
public:
    void push(asset_key key) {
        m_pending.push_back(key);
    }

    size_t count() const { return m_pending.size(); }

    void clear() {
        m_pending.clear();
    }

    std::vector<asset_key> drain() {
        std::vector<asset_key> batch;
        batch.swap(m_pending);
        return batch;
    }

private:
    std::vector<asset_key> m_pending;
};

class Asset {
    std::atomic<uint32_t> ref_count {0};
    asset_key key = {};
    AssetGCQueue* gc_queue = nullptr;

    template<typename T>
    friend class asset_ptr;

    template<typename T>
    friend class AssetPool;
};

template<typename T>
class AssetPool;

template<typename T>
class asset_ptr {
public:
    asset_ptr() noexcept : m_asset(nullptr) {}
    asset_ptr(std::nullptr_t) noexcept : m_asset(nullptr) {}

    // Copy
    asset_ptr(const asset_ptr& other) noexcept : m_asset(other.m_asset) {
        add_ref();
    }

    asset_ptr& operator=(const asset_ptr& other) noexcept {
        if (this != &other) {
            release();
            m_asset = other.m_asset;
            add_ref();
        }
        return *this;
    }

    // Move
    asset_ptr(asset_ptr&& other) noexcept : m_asset(other.m_asset) {
        other.m_asset = nullptr;
    }

    asset_ptr& operator=(asset_ptr&& other) noexcept {
        if (this != &other) {
            release();
            m_asset = other.m_asset;
            other.m_asset = nullptr;
        }
        return *this;
    }

    ~asset_ptr() {
        release();
    }

    // Pointer interface — no manager dependency needed
    T* get() const noexcept {
        return m_asset;
    }

    T* operator->() const noexcept {
        assert(m_asset && "Dereferencing null or unloaded asset_ptr");
        return m_asset;
    }

    T& operator*() const noexcept {
        assert(m_asset && "Dereferencing null or unloaded asset_ptr");
        return *m_asset;
    }

    explicit operator bool() const noexcept {
        return m_asset != nullptr;
    }

    bool operator==(const asset_ptr& other) const noexcept { return m_asset == other.m_asset; }
    bool operator!=(const asset_ptr& other) const noexcept { return m_asset != other.m_asset; }
    bool operator==(std::nullptr_t) const noexcept { return m_asset == nullptr; }
    bool operator!=(std::nullptr_t) const noexcept { return m_asset != nullptr; }

    void reset() noexcept {
        release();
        m_asset = nullptr;
    }

private:
    explicit asset_ptr(T* asset) noexcept : m_asset(asset) {
        add_ref();
    }

    void add_ref() noexcept {
        if (m_asset) {
            m_asset->ref_count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void release() noexcept {
        if (m_asset) {
            // fetch_sub returns the value BEFORE the subtraction
            if (m_asset->ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                // Was 1, now 0 — enqueue for garbage collection
                if (m_asset->gc_queue) {
                    m_asset->gc_queue->push(m_asset->key);
                }
            }
        }
    }

    // Allow asset_ptr_cast to access internals
    template <typename U, typename V>
    friend asset_ptr<U> asset_ptr_cast(const asset_ptr<V>&);

    friend class AssetManager;
    friend class AssetPool<T>;

private:
    T* m_asset = nullptr;
};

template <typename U, typename V>
asset_ptr<U> asset_ptr_cast(const asset_ptr<V>& ptr) {
    static_assert(std::is_void_v<V> || std::is_base_of_v<V, U> || std::is_base_of_v<U, V>,
                  "asset_ptr_cast: types must be related or source must be void");
    if (!ptr.m_asset) return asset_ptr<U>{};
    auto* dynamic = static_cast<U*>(ptr.m_asset);
    return asset_ptr<U>{dynamic};
}

class AssetPoolBase {
public:
    virtual ~AssetPoolBase() = default;
    virtual void gc() = 0;
    virtual void clear() = 0;
};

template<typename T>
class AssetPool : public AssetPoolBase {
public:
    AssetPool() = default;

    template<class ...Args>
    T* emplaceWithPath(const std::string& path, Args&& ...args) {
        auto key = m_assets.emplace(std::forward<Args>(args)...);
        m_pathIndex[path] = key;

        auto* asset = m_assets.get(key);
        asset->key = key;
        asset->gc_queue = &m_gcQueue;
        return asset;
    }

    template<class ...Args>
    T* emplace(Args&& ...args) {
        auto key = m_assets.emplace(std::forward<Args>(args)...);

        auto* asset = m_assets.get(key);
        asset->key = key;
        asset->gc_queue = &m_gcQueue;
        return asset;
    }

    T* get(const std::string& path) {
        auto it = m_pathIndex.find(path);
        if (it == m_pathIndex.end()) {
            return nullptr;
        }
        return m_assets.get(it->second);
    }

    T* get(asset_key key) {
        return m_assets.get(key);
    }

    void erase(asset_key key) {
        m_assets.erase(key);
    }

    void gc() override {
        auto batch = m_gcQueue.drain();
        for (auto key : batch) {
            auto ptr = m_assets.get(key);
            if (!ptr || ptr->ref_count.load(std::memory_order_acquire) > 0) continue;

            m_assets.erase(key);
        }
    }

    void clear() override {
        m_assets.clear();
        m_pathIndex.clear();
        m_gcQueue.clear();
    }

private:
    using key_type = asset_key;

    dod::slot_map<T, key_type> m_assets;
    std::unordered_map<std::string, key_type> m_pathIndex;
    AssetGCQueue m_gcQueue;
};

class AssetManager;

template<typename T>
class LoadContext {
public:
    LoadContext(AssetManager* m_asset_manager, const std::string& m_path)
        : m_assetManager(m_asset_manager),
          m_path(m_path) {
    }

    template<typename ...Args>
    T& construct(Args&&... args);

    template<typename U>
    asset_ptr<U> load(const std::string& path);

private:
    AssetManager* m_assetManager;
    std::string m_path;
};

class AssetLoaderBase {
public:
    virtual ~AssetLoaderBase() = default;
};

template<typename T>
class AssetLoader : public AssetLoaderBase {
public:
    virtual void load(LoadContext<T>& context, const std::string& path) = 0;
};

class AssetManager {
public:
    template<class T>
    void registerLoader(std::unique_ptr<AssetLoader<T>> loader) {
        const id_type id = entt::type_hash<T>::value();
        m_loaders[id] = std::move(loader);
    }

    template<class T>
    asset_ptr<T> load(const std::string& path) {
        auto& pool = ensurePool<T>();
        auto* ptr = pool.get(path);
        if (ptr) return asset_ptr<T> { ptr };

        auto loader = tryGetLoader<T>();
        if (!loader) return {};

        LoadContext<T> context { this, path };

        loader->load(context, path);
        return asset_ptr<T> { pool.get(path) };
    }

    template<class T>
    asset_ptr<T> load(const std::string& path, std::function<void(LoadContext<T>&)> loader) {
        auto& pool = ensurePool<T>();
        auto* ptr = pool.get(path);
        if (ptr) return asset_ptr<T> { ptr };

        LoadContext<T> context { this, path };

        loader(context);
        return asset_ptr<T> { pool.get(path) };
    }

    template<class T>
    asset_ptr<T> get(const std::string& path) {
        auto& pool = ensurePool<T>();
        return asset_ptr<T> { pool.get(path) };
    }

    template<class T, typename ...Args>
    asset_ptr<T> emplace(Args&&... args) {
        auto& pool = ensurePool<T>();
        auto* ptr = pool.emplace(std::forward<Args>(args)...);
        return asset_ptr<T> { ptr };
    }

    template<class T, typename ...Args>
    asset_ptr<T> emplaceWithPath(const std::string& path, Args&&... args) {
        auto& pool = ensurePool<T>();
        auto* ptr = pool.emplaceWithPath(path, std::forward<Args>(args)...);
        return asset_ptr<T> { ptr };
    }

    void gc() {
        for (auto [id, pool] : m_pools) {
            pool->gc();
        }
    }

    void reset() {
        for (auto [id, pool] : m_pools) {
            pool->clear();
        }
    }

private:
    using id_type = entt::id_type;

    entt::dense_map<id_type, std::unique_ptr<AssetLoaderBase>> m_loaders;
    entt::dense_map<id_type, std::unique_ptr<AssetPoolBase>> m_pools;

    template<class T>
    AssetPool<T>& ensurePool() {
        const id_type id = entt::type_hash<T>::value();
        auto it = m_pools.find(id);
        if (it != m_pools.end()) {
            return *static_cast<AssetPool<T>*>(it->second.get());
        }

        auto pool = std::make_unique<AssetPool<T>>();
        auto* raw = pool.get();
        m_pools[id] = std::move(pool);

        return *raw;
    }

    template<class T>
    AssetLoader<T>* tryGetLoader() {
        const id_type id = entt::type_hash<T>::value();
        auto it = m_loaders.find(id);
        if (it == m_loaders.end()) {
            return nullptr;
        }
        return static_cast<AssetLoader<T>*>(it->second.get());
    }

    template<class T>
    friend class LoadContext;
};

template <typename T>
template <typename ...Args>
T& LoadContext<T>::construct(Args&&... args) {
    return *m_assetManager->ensurePool<T>().emplaceWithPath(m_path, std::forward<Args>(args)...);
}

template <typename T>
template <typename U>
asset_ptr<U> LoadContext<T>::load(const std::string& path) {
    return m_assetManager->load<U>(path);
}

}
