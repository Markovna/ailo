#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <type_traits>

#include "World.h"

namespace ailo {

template<typename...>
inline constexpr bool always_false = false;

// Iterates entities that have all of Cs. Taken by value (or const&) as a system parameter.
template<typename... Cs>
class Query {
public:
    using view_type = decltype(std::declval<Scene&>().template view<Cs...>());

    explicit Query(view_type view) : m_view(view) {}

    decltype(auto) each() { return m_view.each(); }
    auto begin() const { return m_view.begin(); }
    auto end() const { return m_view.end(); }

    template<typename C>
    decltype(auto) get(Entity entity) { return m_view.template get<C>(entity); }
    bool contains(Entity entity) const { return m_view.contains(entity); }

    view_type& view() { return m_view; }

private:
    view_type m_view;
};

// Describes how a system parameter type is fetched from the World:
//   T&, const T&   → resource T (must exist)
//   T*, const T*   → resource T or nullptr (optional)
//   World&         → the world itself
//   Query<Cs...>   → view over entities with components Cs...
// Specialize to add new parameter kinds.
template<typename T>
struct SystemParam {
    static_assert(always_false<T>,
        "Unsupported system parameter: take resources as T& / const T& / T*, queries as Query<...>");
};

template<typename T>
struct SystemParam<T&> {
    static T& fetch(World& world) { return world.resource<std::remove_const_t<T>>(); }
    static bool available(const World& world) { return world.hasResource<std::remove_const_t<T>>(); }
    static std::string_view typeName() { return entt::type_name<std::remove_const_t<T>>::value(); }
};

template<typename T>
struct SystemParam<T*> {
    static T* fetch(World& world) { return world.tryResource<std::remove_const_t<T>>(); }
    static bool available(const World&) { return true; }
    static std::string_view typeName() { return entt::type_name<std::remove_const_t<T>>::value(); }
};

template<>
struct SystemParam<World&> {
    static World& fetch(World& world) { return world; }
    static bool available(const World&) { return true; }
    static std::string_view typeName() { return "World"; }
};

template<>
struct SystemParam<Scene&> {
    static Scene& fetch(World& world) { return world.scene(); }
    static bool available(const World&) { return true; }
    static std::string_view typeName() { return "Scene"; }
};

template<typename... Cs>
struct SystemParam<Query<Cs...>> {
    static Query<Cs...> fetch(World& world) { return Query<Cs...> { world.scene().template view<Cs...>() }; }
    static bool available(const World&) { return true; }
    static std::string_view typeName() { return entt::type_name<Query<Cs...>>::value(); }
};

template<typename... Cs>
struct SystemParam<const Query<Cs...>&> : SystemParam<Query<Cs...>> {};

template<typename... Cs>
struct SystemParam<Query<Cs...>&> {
    static_assert(always_false<Cs...>, "Take Query<...> by value or const&, not by non-const reference");
};

namespace detail {

template<typename...>
struct type_list {};

// Generic lambdas (auto parameters) are not supported: parameter types must be concrete.
template<typename F>
struct function_traits : function_traits<decltype(&F::operator())> {};

template<typename R, typename... Args>
struct function_traits<R(*)(Args...)> { using args_list = type_list<Args...>; };
template<typename R, typename... Args>
struct function_traits<R(*)(Args...) noexcept> : function_traits<R(*)(Args...)> {};

template<typename C, typename R, typename... Args>
struct function_traits<R(C::*)(Args...)> : function_traits<R(*)(Args...)> {};
template<typename C, typename R, typename... Args>
struct function_traits<R(C::*)(Args...) const> : function_traits<R(*)(Args...)> {};
template<typename C, typename R, typename... Args>
struct function_traits<R(C::*)(Args...) noexcept> : function_traits<R(*)(Args...)> {};
template<typename C, typename R, typename... Args>
struct function_traits<R(C::*)(Args...) const noexcept> : function_traits<R(*)(Args...)> {};

}

[[noreturn]] void throwMissingSystemParam(std::string_view systemName, std::string_view typeName);

// A type-erased system: a callable whose parameters are resolved from the World on every run.
struct System {
    std::string name;
    std::move_only_function<void(World&)> run;

    // Throws for the first required parameter that is not available in the world.
    void validate(const World& world) const { validator(world, name); }

    void (*validator)(const World&, std::string_view name) = nullptr;
};

namespace detail {

template<typename F, typename... Args>
System makeSystem(F&& f, type_list<Args...>, std::string name) {
    System system;
    system.name = std::move(name);
    system.run = [f = std::forward<F>(f)](World& world) mutable {
        f(SystemParam<Args>::fetch(world)...);
    };
    system.validator = [](const World& world, std::string_view name) {
        ((SystemParam<Args>::available(world) ? void() : throwMissingSystemParam(name, SystemParam<Args>::typeName())), ...);
    };
    return system;
}

}

template<typename F>
System makeSystem(F&& f, std::string name = {}) {
    using Fn = std::decay_t<F>;
    using Args = typename detail::function_traits<Fn>::args_list;
    if (name.empty()) {
        name = entt::type_name<Fn>::value();
    }
    return detail::makeSystem(Fn(std::forward<F>(f)), Args {}, std::move(name));
}

}
