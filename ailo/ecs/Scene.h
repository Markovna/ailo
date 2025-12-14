#pragma once

#include <entt/entt.hpp>

namespace ailo {

using Entity = entt::entity;

class Scene {
 public:
  Scene() = default;

  auto addEntity() {
   return m_registry.create();
  }

 void removeEntity(entt::entity entity) {
   m_registry.destroy(entity);
  }

  template<typename ...Types>
  decltype(auto) view() { return m_registry.view<Types...>(); }

  template<typename Type, typename ...Args>
  decltype(auto) addComponent(entt::entity entity, Args&& ...args) {
   return m_registry.emplace<Type, Args...>(entity, std::forward<Args>(args)...);
  }

 template<typename Type>
 decltype(auto) get(entt::entity entity) {
   return m_registry.get<Type>(entity);
 }



 private:
  entt::registry m_registry;
};

}