#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <unordered_map>
#include <memory>
#include <typeindex>
#include <tuple>
#include <ranges>
#include <algorithm>
#include <stdexcept>

namespace ailo {

// Entity is just an ID (integer handle)
using Entity = uint32_t;

// Base class for type erasure of component pools
class IComponentPool {
public:
    virtual ~IComponentPool() = default;
    virtual bool has(Entity entity) const = 0;
    virtual void remove(Entity entity) = 0;
    virtual size_t size() const = 0;
};

// ComponentPool: Stores components of type T in a cache-friendly way using sparse set pattern
// - Dense array stores actual components contiguously (good for iteration)
// - Sparse map provides O(1) lookup from entity to component index
template<typename T>
class ComponentPool : public IComponentPool {
private:
    std::vector<T> components;                    // Dense array of components (cache-friendly)
    std::vector<Entity> entities;                 // Parallel array of entity IDs
    std::unordered_map<Entity, size_t> entityToIndex; // Sparse map for O(1) lookup

public:
    // Add a new component for an entity
    template<typename... Args>
    T& add(Entity entity, Args&&... args) {
        // If entity already has this component, return existing
        if (auto it = entityToIndex.find(entity); it != entityToIndex.end()) {
            return components[it->second];
        }

        // Add new component
        size_t index = components.size();
        components.emplace_back(std::forward<Args>(args)...);
        entities.push_back(entity);
        entityToIndex[entity] = index;
        return components.back();
    }

    // Get component for an entity
    T& get(Entity entity) {
        auto it = entityToIndex.find(entity);
        if (it == entityToIndex.end()) {
            throw std::runtime_error("Entity does not have this component");
        }
        return components[it->second];
    }

    const T& get(Entity entity) const {
        auto it = entityToIndex.find(entity);
        if (it == entityToIndex.end()) {
            throw std::runtime_error("Entity does not have this component");
        }
        return components[it->second];
    }

    // Check if entity has this component
    bool has(Entity entity) const override {
        return entityToIndex.contains(entity);
    }

    // Remove component from entity (swap-and-pop for O(1) removal)
    void remove(Entity entity) override {
        auto it = entityToIndex.find(entity);
        if (it == entityToIndex.end()) return;

        size_t index = it->second;
        size_t lastIndex = components.size() - 1;

        // Swap with last element and pop
        if (index != lastIndex) {
            components[index] = std::move(components[lastIndex]);
            entities[index] = entities[lastIndex];
            entityToIndex[entities[index]] = index;
        }

        components.pop_back();
        entities.pop_back();
        entityToIndex.erase(entity);
    }

    size_t size() const override {
        return components.size();
    }

    const std::vector<Entity>& getEntities() const {
        return entities;
    }

    std::vector<T>& getComponents() {
        return components;
    }

    const std::vector<T>& getComponents() const {
        return components;
    }
};

// Forward declaration
class ECS;

// View: Iterate over entities that have a specific set of components
template<typename... Components>
class View {
private:
    ECS* ecs;

    // Get all entities that have all required components
    std::vector<Entity> getMatchingEntities() const;

public:
    explicit View(ECS* ecs) : ecs(ecs) {}

    // Basic iterator that yields Entity IDs
    class Iterator {
    private:
        const std::vector<Entity>* entities;
        size_t index;

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Entity;
        using difference_type = std::ptrdiff_t;
        using pointer = const Entity*;
        using reference = Entity;

        Iterator(const std::vector<Entity>* entities, size_t index)
            : entities(entities), index(index) {}

        Entity operator*() const {
            return (*entities)[index];
        }

        Iterator& operator++() {
            ++index;
            return *this;
        }

        Iterator operator++(int) {
            Iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        bool operator==(const Iterator& other) const {
            return index == other.index;
        }

        bool operator!=(const Iterator& other) const {
            return index != other.index;
        }
    };

    // Range that yields tuples of (entity, component references...)
    class EachRange {
    private:
        ECS* ecs;
        std::vector<Entity> entities;

    public:
        EachRange(ECS* ecs, std::vector<Entity> entities)
            : ecs(ecs), entities(std::move(entities)) {}

        class EachIterator {
        private:
            ECS* ecs;
            const std::vector<Entity>* entities;
            size_t index;

        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = std::tuple<Entity, Components&...>;
            using difference_type = std::ptrdiff_t;
            using pointer = void;
            using reference = value_type;

            EachIterator(ECS* ecs, const std::vector<Entity>* entities, size_t index)
                : ecs(ecs), entities(entities), index(index) {}

            auto operator*() const -> std::tuple<Entity, Components&...>;

            EachIterator& operator++() {
                ++index;
                return *this;
            }

            EachIterator operator++(int) {
                EachIterator tmp = *this;
                ++(*this);
                return tmp;
            }

            bool operator==(const EachIterator& other) const {
                return index == other.index;
            }

            bool operator!=(const EachIterator& other) const {
                return index != other.index;
            }
        };

        EachIterator begin() {
            return EachIterator(ecs, &entities, 0);
        }

        EachIterator end() {
            return EachIterator(ecs, &entities, entities.size());
        }
    };

    // Iterator interface for "for(auto entity: view)"
    Iterator begin() const {
        static thread_local std::vector<Entity> entities;
        entities = getMatchingEntities();
        return Iterator(&entities, 0);
    }

    Iterator end() const {
        static thread_local std::vector<Entity> entities;
        return Iterator(&entities, entities.size());
    }

    // Get a specific component for an entity
    template<typename T>
    T& get(Entity entity);

    // Returns a range that yields tuples for structured bindings
    EachRange each() {
        return EachRange(ecs, getMatchingEntities());
    }
};

// Main ECS class
class ECS {
private:
    uint32_t nextEntity = 0;
    std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>> componentPools;

    template<typename T>
    ComponentPool<T>* getPool() {
        auto typeIndex = std::type_index(typeid(T));
        auto it = componentPools.find(typeIndex);
        if (it == componentPools.end()) {
            auto pool = std::make_unique<ComponentPool<T>>();
            auto* ptr = pool.get();
            componentPools[typeIndex] = std::move(pool);
            return ptr;
        }
        return static_cast<ComponentPool<T>*>(it->second.get());
    }

public:
    // Create a new entity
    Entity create() {
        return nextEntity++;
    }

    // Add a component to an entity
    template<typename T, typename... Args>
    T& add(Entity entity, Args&&... args) {
        return getPool<T>()->add(entity, std::forward<Args>(args)...);
    }

    // Get a component from an entity
    template<typename T>
    T& get(Entity entity) {
        return getPool<T>()->get(entity);
    }

    // Check if entity has a component
    template<typename T>
    bool has(Entity entity) {
        return getPool<T>()->has(entity);
    }

    // Remove a component from an entity
    template<typename T>
    void remove(Entity entity) {
        getPool<T>()->remove(entity);
    }

    // Destroy an entity and remove all its components
    void destroy(Entity entity) {
        // Remove entity from all component pools
        for (auto& [typeIndex, pool] : componentPools) {
            pool->remove(entity);
        }
    }

    // Create a view over entities with specific components
    template<typename... Components>
    View<Components...> view() {
        return View<Components...>(this);
    }

    template<typename... Components>
    friend class View;
};

// View implementation

template<typename... Components>
std::vector<Entity> View<Components...>::getMatchingEntities() const {
    // Find the smallest component pool to minimize iteration
    std::array<IComponentPool*, sizeof...(Components)> pools = {
        ecs->getPool<Components>()...
    };

    // Find smallest pool
    IComponentPool* smallestPool = nullptr;
    size_t smallestSize = std::numeric_limits<size_t>::max();
    size_t smallestIndex = 0;

    for (size_t i = 0; i < pools.size(); ++i) {
        if (pools[i]->size() < smallestSize) {
            smallestSize = pools[i]->size();
            smallestPool = pools[i];
            smallestIndex = i;
        }
    }

    // Get entities from the first component pool
    auto* firstPool = ecs->getPool<std::tuple_element_t<0, std::tuple<Components...>>>();

    // Filter entities that have all required components
    std::vector<Entity> matching;
    for (Entity entity : firstPool->getEntities()) {
        if ((ecs->has<Components>(entity) && ...)) {
            matching.push_back(entity);
        }
    }

    return matching;
}

template<typename... Components>
template<typename T>
T& View<Components...>::get(Entity entity) {
    return ecs->get<T>(entity);
}

template<typename... Components>
auto View<Components...>::EachRange::EachIterator::operator*() const -> std::tuple<Entity, Components&...> {
    Entity entity = (*entities)[index];
    return std::tuple<Entity, Components&...>(entity, ecs->get<Components>(entity)...);
}

} // namespace ailo
