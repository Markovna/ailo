#pragma once
#include <cassert>
#include <cstdint>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/slot_map.h"
#include "entt/entt.hpp"

namespace ailo {

template<class T>
class AssetPtr;

namespace detail {
    class AssetStorageBase {
    public:
        virtual ~AssetStorageBase() = default;

        virtual std::size_t size() const = 0;
        // Prints every asset still in the storage, with its key and reference count.
        virtual void reportLeaks(std::ostream& out) const = 0;
    };

    class AssetLoaderBase {
    public:
        virtual ~AssetLoaderBase() = default;
    };

    template<class T>
    using SlotMap = dod::slot_map_key32<T>;
}

// Stable handle to an asset inside an AssetStorage. The key is versioned, so a
// handle to a removed asset resolves to nothing even after its slot is reused.
template<class T>
class AssetIndex {
public:
    using ValueType = detail::SlotMap<T>;

    constexpr AssetIndex() = default;
    constexpr explicit AssetIndex(ValueType value) : m_value(value) {}

    constexpr ValueType value() const { return m_value; }
    constexpr bool isValid() const { return m_value != ValueType::invalid(); }

    friend bool operator==(const AssetIndex&, const AssetIndex&) = default;

private:
    ValueType m_value = ValueType::invalid();
};

struct noname_t {};

template<class T>
class AssetStorage final : public detail::AssetStorageBase {
private:
    friend class AssetPtr<T>;

    struct Entry {
        std::optional<T> asset = {};
        std::string key;
        std::uint32_t refCount = 0;
    };

    using SlotMap = dod::slot_map<Entry, typename AssetIndex<T>::ValueType>;
    using AssetIndex = AssetIndex<T>;

public:
    using Key = SlotMap::key;

    AssetStorage() = default;
    ~AssetStorage() override;

    AssetStorage(const AssetStorage&) = delete;
    AssetStorage& operator=(const AssetStorage&) = delete;

    bool has(const std::string& key) const;
    std::optional<AssetPtr<T>> get(const std::string& key);

    T* get(AssetIndex index);
    const T* get(AssetIndex index) const;

    // Throws std::invalid_argument if the key is empty or an asset with this key already exists.
    template<class ...Args>
    AssetPtr<T> emplace(const std::string& key, Args&&... args);

    template<class ...Args>
    AssetPtr<T> emplace(noname_t, Args&&... args);

    std::size_t size() const override { return m_map.size(); }
    bool empty() const { return m_map.empty(); }

    void reportLeaks(std::ostream& out) const override;

private:

    Entry* findEntry(AssetIndex index);
    const Entry* findEntry(AssetIndex index) const;

    void addRef(AssetIndex index) noexcept;
    void release(AssetIndex index) noexcept;
    void remove(AssetIndex index) noexcept;
    std::uint32_t refCount(AssetIndex index) const noexcept;

    SlotMap m_map;
    std::unordered_map<std::string, Key> m_keys;
};

// Owning, reference-counted handle
template<class T>
class AssetPtr {
public:
    using AssetIndex = AssetIndex<T>;

    AssetPtr() = default;
    AssetPtr(AssetStorage<T>* storage, AssetIndex index);

    AssetPtr(const AssetPtr& other);
    AssetPtr(AssetPtr&& other) noexcept;
    AssetPtr& operator=(AssetPtr other) noexcept;
    ~AssetPtr();

    void reset() noexcept;
    void swap(AssetPtr& other) noexcept;

    T* get() const;
    T* operator->() const;
    T& operator*() const;
    explicit operator bool() const { return m_storage != nullptr; }

    AssetIndex index() const { return m_index; }
    std::uint32_t useCount() const;

    friend bool operator==(const AssetPtr& a, const AssetPtr& b) {
        return a.m_storage == b.m_storage && a.m_index == b.m_index;
    }

private:
    AssetIndex m_index;
    AssetStorage<T>* m_storage = nullptr;
};

class AssetServer;

template<class T>
class LoadContext {
public:
    LoadContext(AssetServer* assetServer, AssetStorage<T>& storage, const std::string& key);

    // Creates the asset being loaded under this context's key. Must be called exactly once.
    template<class ...Args>
    T& construct(Args&&... args);

    // Loads a dependency (of any asset type) through the same server.
    template<class U = T>
    AssetPtr<U> load(const std::string& key);

    const std::string& key() const { return m_key; }

private:
    AssetServer* m_assetServer;
    AssetStorage<T>& m_storage;
    std::string m_key;
    // Keeps the freshly constructed asset alive until AssetServer::load picks it up.
    std::optional<AssetPtr<T>> m_asset;
};

template<class T>
class AssetLoader : public detail::AssetLoaderBase {
public:
    virtual void load(const std::string& key, LoadContext<T>& context) = 0;
};

class AssetServer {
public:
    AssetServer() = default;
    ~AssetServer();

    AssetServer(const AssetServer&) = delete;
    AssetServer& operator=(const AssetServer&) = delete;

    template<class T>
    AssetPtr<T> load(const std::string& key) {
        auto& storage = getStorage<T>();
        auto ptr = storage.get(key);
        if (!ptr) {
            LoadContext<T> context {this, storage, key};

            auto& loader = getLoader<T>();
            loader.load(key, context);

            ptr = storage.get(key);
            if (!ptr) {
                throw std::runtime_error("asset loader did not construct asset '" + key + "'");
            }
        }
        return ptr.value();
    }

    // Throws std::invalid_argument if a loader for T is already registered.
    template<class T>
    void registerLoader(std::unique_ptr<AssetLoader<T>> loader);

    template<class T>
    void registerStorage(AssetStorage<T>*);

    // Prints every asset still alive in the registered storages and returns how many there are.
    std::size_t reportLeaks(std::ostream& out = std::cerr) const;

private:
    template<class T> AssetStorage<T>& getStorage();
    template<class T> AssetLoader<T>& getLoader();

    detail::AssetStorageBase* findStorage(entt::id_type type) const;
    detail::AssetLoaderBase* findLoader(entt::id_type type) const;
    void setLoader(entt::id_type type, std::unique_ptr<detail::AssetLoaderBase> loader);
    void setStorage(entt::id_type type, detail::AssetStorageBase*);

    std::unordered_map<entt::id_type, std::unique_ptr<detail::AssetLoaderBase>> m_loaders;
    std::unordered_map<entt::id_type, detail::AssetStorageBase*> m_storages;
};

// ---------------------------------------------------------------------------
// AssetStorage
// ---------------------------------------------------------------------------

template<class T>
AssetStorage<T>::~AssetStorage() {
    assert(m_map.empty() && "AssetStorage destroyed while AssetPtrs still reference it");
}

template<class T>
bool AssetStorage<T>::has(const std::string& key) const {
    return m_keys.contains(key);
}

template<class T>
std::optional<AssetPtr<T>> AssetStorage<T>::get(const std::string& key) {
    auto it = m_keys.find(key);
    if (it == m_keys.end()) {
        return std::nullopt;
    }
    return AssetPtr<T>(this, AssetIndex { it->second });
}

template<class T>
T* AssetStorage<T>::get(AssetIndex index) {
    Entry* entry = findEntry(index);
    if (!entry) return nullptr;

    auto& asset = entry->asset;
    return asset.has_value() ? &asset.value() : nullptr;
}

template<class T>
const T* AssetStorage<T>::get(AssetIndex index) const {
    const Entry* entry = findEntry(index);
    if (!entry) return nullptr;

    auto& asset = entry->asset;
    return asset.has_value() ? &asset.value() : nullptr;
}

template<class T>
template<class ...Args>
AssetPtr<T> AssetStorage<T>::emplace(const std::string& key, Args&&... args) {
    if (key.empty()) {
        throw std::invalid_argument("asset key must not be empty");
    }
    if (has(key)) {
        throw std::invalid_argument("asset '" + key + "' already exists");
    }

    const Key slot = m_map.emplace();
    try {
        Entry& entry = *m_map.get(slot);
        entry.key = key;
        entry.asset.emplace(std::forward<Args>(args)...);
        m_keys.emplace(key, slot);
    } catch (...) {
        m_map.erase(slot);
        throw;
    }

    return AssetPtr<T>(this, AssetIndex { slot });
}

template <class T>
template <class ... Args>
AssetPtr<T> AssetStorage<T>::emplace(noname_t, Args&&... args) {
    const Key slot = m_map.emplace();
    try {
        Entry& entry = *m_map.get(slot);
        entry.asset.emplace(std::forward<Args>(args)...);
    } catch (...) {
        m_map.erase(slot);
        throw;
    }

    return AssetPtr<T>(this, AssetIndex { slot });
}

template<class T>
void AssetStorage<T>::reportLeaks(std::ostream& out) const {
    for (const Entry& entry : m_map) {
        out << "[AssetServer] leaked " << entt::type_name<T>::value()
            << " '" << entry.key << "'"
            << " (" << entry.refCount << " references)" << std::endl;
    }
}

template<class T>
auto AssetStorage<T>::findEntry(AssetIndex index) -> Entry* {
    return const_cast<Entry*>(std::as_const(*this).findEntry(index));
}

template<class T>
auto AssetStorage<T>::findEntry(AssetIndex index) const -> const Entry* {
    return m_map.get(index.value());
}

template<class T>
void AssetStorage<T>::addRef(AssetIndex index) noexcept {
    ++m_map.get(index.value())->refCount;
}

template<class T>
void AssetStorage<T>::release(AssetIndex index) noexcept {
    Entry* entry = m_map.get(index.value());
    assert(entry->refCount > 0);
    if (--entry->refCount == 0) {
        remove(index);
    }
}

template<class T>
std::uint32_t AssetStorage<T>::refCount(AssetIndex index) const noexcept {
    return m_map.get(index.value())->refCount;
}

template<class T>
void AssetStorage<T>::remove(AssetIndex index) noexcept {
    Entry* entry = m_map.get(index.value());
    if (!entry->key.empty()) {
        m_keys.erase(entry->key);
    }
    // Destroy the asset while its slot is still occupied: its destructor may release other assets of this storage,
    // which must not re-enter slot_map::erase.
    entry->asset.reset();
    m_map.erase(index.value());
}

// ---------------------------------------------------------------------------
// AssetPtr
// ---------------------------------------------------------------------------

template<class T>
AssetPtr<T>::AssetPtr(AssetStorage<T>* storage, AssetIndex index)
    : m_index(index), m_storage(storage) {
    if (m_storage) {
        m_storage->addRef(m_index);
    }
}

template<class T>
AssetPtr<T>::AssetPtr(const AssetPtr& other)
    : AssetPtr(other.m_storage, other.m_index) {}

template<class T>
AssetPtr<T>::AssetPtr(AssetPtr&& other) noexcept
    : m_index(std::exchange(other.m_index, AssetIndex{})),
      m_storage(std::exchange(other.m_storage, nullptr)) {}

template<class T>
AssetPtr<T>& AssetPtr<T>::operator=(AssetPtr other) noexcept {
    swap(other);
    return *this;
}

template<class T>
AssetPtr<T>::~AssetPtr() {
    reset();
}

template<class T>
void AssetPtr<T>::reset() noexcept {
    if (m_storage) {
        AssetStorage<T>* storage = std::exchange(m_storage, nullptr);
        const AssetIndex index = std::exchange(m_index, AssetIndex{});
        storage->release(index);
    }
}

template<class T>
void AssetPtr<T>::swap(AssetPtr& other) noexcept {
    std::swap(m_index, other.m_index);
    std::swap(m_storage, other.m_storage);
}

template<class T>
T* AssetPtr<T>::get() const {
    return m_storage ? m_storage->get(m_index) : nullptr;
}

template<class T>
T* AssetPtr<T>::operator->() const {
    assert(m_storage && "dereferencing a null AssetPtr");
    return get();
}

template<class T>
T& AssetPtr<T>::operator*() const {
    assert(m_storage && "dereferencing a null AssetPtr");
    return *get();
}

template<class T>
std::uint32_t AssetPtr<T>::useCount() const {
    return m_storage ? m_storage->refCount(m_index) : 0;
}

// ---------------------------------------------------------------------------
// LoadContext
// ---------------------------------------------------------------------------

template<class T>
LoadContext<T>::LoadContext(AssetServer* assetServer, AssetStorage<T>& storage, const std::string& key)
    : m_assetServer(assetServer), m_storage(storage), m_key(key) {}

template<class T>
template<class ...Args>
T& LoadContext<T>::construct(Args&&... args) {
    if (m_asset) {
        throw std::logic_error("asset '" + m_key + "' was already constructed");
    }
    m_asset = m_storage.emplace(m_key, std::forward<Args>(args)...);
    return **m_asset;
}

template<class T>
template<class U>
AssetPtr<U> LoadContext<T>::load(const std::string& key) {
    return m_assetServer->template load<U>(key);
}

// ---------------------------------------------------------------------------
// AssetServer
// ---------------------------------------------------------------------------

template<class T>
void AssetServer::registerLoader(std::unique_ptr<AssetLoader<T>> loader) {
    const auto type = entt::type_index<T>::value();
    if (findLoader(type)) {
        throw std::invalid_argument(std::string("asset loader already registered for type ") + typeid(T).name());
    }
    setLoader(type, std::move(loader));
}

template <class T>
void AssetServer::registerStorage(AssetStorage<T>* storage) {
    setStorage(entt::type_index<T>::value(), storage);
}

template<class T>
AssetStorage<T>& AssetServer::getStorage() {
    auto* storage = findStorage(entt::type_index<T>::value());
    if (!storage) {
        throw std::runtime_error(std::string("no asset storage registered for type ") + typeid(T).name());
    }
    return static_cast<AssetStorage<T>&>(*storage);
}

template<class T>
AssetLoader<T>& AssetServer::getLoader() {
    auto* loader = findLoader(entt::type_index<T>::value());
    if (!loader) {
        throw std::runtime_error(std::string("no asset loader registered for type ") + typeid(T).name());
    }
    return static_cast<AssetLoader<T>&>(*loader);
}

}