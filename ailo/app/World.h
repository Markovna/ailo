#pragma once

#include <format>
#include <memory>
#include <stdexcept>
#include <vector>

#include <entt/entt.hpp>

#include "ecs/Scene.h"

namespace ailo {

// Owns the scene and all engine-wide resources (singletons such as RenderAPI, Renderer, AssetManager).
// Resources are heap-allocated, so references stay valid when more resources are inserted,
// and they are destroyed in reverse insertion order after the scene has been cleared.
class World {
public:
    World() = default;
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    Scene& scene() { return m_scene; }
    const Scene& scene() const { return m_scene; }

    template<typename T, typename... Args>
    T& insertResource(Args&&... args) {
        const auto id = entt::type_hash<T>::value();
        if (m_index.contains(id)) {
            throw std::runtime_error(std::format("resource already inserted: {}", entt::type_name<T>::value()));
        }

        auto holder = std::make_unique<Holder<T>>(std::forward<Args>(args)...);
        T& ref = holder->value;
        m_index[id] = m_resources.size();
        m_resources.push_back(std::move(holder));
        return ref;
    }

    template<typename T>
    T* tryResource() {
        auto it = m_index.find(entt::type_hash<T>::value());
        if (it == m_index.end()) {
            return nullptr;
        }
        return &static_cast<Holder<T>*>(m_resources[it->second].get())->value;
    }

    template<typename T>
    const T* tryResource() const {
        return const_cast<World*>(this)->tryResource<T>();
    }

    template<typename T>
    T& resource() {
        if (auto* ptr = tryResource<T>()) {
            return *ptr;
        }
        throw std::runtime_error(std::format("missing resource: {}", entt::type_name<T>::value()));
    }

    template<typename T>
    const T& resource() const {
        return const_cast<World*>(this)->resource<T>();
    }

    template<typename T>
    bool hasResource() const {
        return m_index.contains(entt::type_hash<T>::value());
    }

private:
    struct HolderBase {
        virtual ~HolderBase() = default;
    };

    template<typename T>
    struct Holder final : HolderBase {
        template<typename... Args>
        explicit Holder(Args&&... args) : value(std::forward<Args>(args)...) {}
        T value;
    };

    Scene m_scene;
    std::vector<std::unique_ptr<HolderBase>> m_resources;
    entt::dense_map<entt::id_type, size_t> m_index;
};

inline World::~World() {
    // Entities first: components hold asset_ptrs and GPU handles owned by resources.
    m_scene.clear();

    while (!m_resources.empty()) {
        m_resources.pop_back();
    }
}

}
