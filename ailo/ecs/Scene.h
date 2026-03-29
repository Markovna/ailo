#pragma once

#include <entt/entt.hpp>

#include "Engine.h"

namespace ailo {

using Entity = entt::entity;

class Texture;
class Scene;

class Scene {
 public:
  Scene();

  auto addEntity() { return m_registry.create(); }
  void removeEntity(entt::entity entity) { m_registry.destroy(entity); }

  template<typename ...Types>
  decltype(auto) view() { return m_registry.view<Types...>(); }

  template<typename Type, typename ...Args>
  decltype(auto) addComponent(entt::entity entity, Args&& ...args) {
   return m_registry.emplace<Type, Args...>(entity, std::forward<Args>(args)...);
  }

  template<typename Type>
  void removeComponent(entt::entity entity) {
    m_registry.remove<Type>(entity);
  }

    template<typename Type>
    decltype(auto) onDestroy() {
        return m_registry.on_destroy<Type>();
    }

  decltype(auto) single() const { return m_singleEntity; }

  template<typename Type>
  decltype(auto) get(entt::entity entity) {
    return m_registry.get<Type>(entity);
  }

 template<typename Type>
 decltype(auto) tryGet(entt::entity entity) {
   return m_registry.try_get<Type>(entity);
  }

 private:
  entt::registry m_registry;
  entt::entity m_singleEntity;
};

}
