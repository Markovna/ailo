#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "World.h"

namespace ailo {

template<typename...>
inline constexpr bool always_false = false;

// Query argument that skips entities having any of Cs: Query<A, Exclude<B, C>>.
template<typename... Cs>
struct Exclude {};

namespace detail {

template<typename...>
struct type_list {};

// Only used in decltype to concatenate type lists with a fold.
template<typename... As, typename... Bs>
type_list<As..., Bs...> operator+(type_list<As...>, type_list<Bs...>);

template<typename T>
struct query_arg {
    using get = type_list<T>;
    using exclude = type_list<>;
};

template<typename... Cs>
struct query_arg<Exclude<Cs...>> {
    using get = type_list<>;
    using exclude = type_list<Cs...>;
};

template<typename Get, typename Excluded>
struct query_view;

template<typename... Cs, typename... Es>
struct query_view<type_list<Cs...>, type_list<Es...>> {
    using type = decltype(std::declval<entt::registry&>().template view<Cs...>(entt::exclude<Es...>));
    static type make(entt::registry& registry) { return registry.template view<Cs...>(entt::exclude<Es...>); }
};

template<typename... Args>
using query_view_t = query_view<
    decltype((type_list<> {} + ... + typename query_arg<Args>::get {})),
    decltype((type_list<> {} + ... + typename query_arg<Args>::exclude {}))>;

}

// Iterates entities that have all of Args except those wrapped in Exclude<...>, and none of the excluded ones.
// Taken by value (or const&) as a system parameter.
template<typename... Args>
class Query {
public:
    using view_type = typename detail::query_view_t<Args...>::type;

    explicit Query(view_type view) : m_view(view) {}
    explicit Query(entt::registry& registry) : m_view(detail::query_view_t<Args...>::make(registry)) {}

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

// State owned by one system instance, persisting across its runs. Default-constructed when the system is added.
// Taken by value (or const&) as a system parameter; it is a handle, the value lives in the system.
template<typename T>
class Local {
public:
    explicit Local(T& value) : m_value(&value) {}

    T& operator*() const { return *m_value; }
    T* operator->() const { return m_value; }
    T& get() const { return *m_value; }

private:
    T* m_value;
};

// A specialization may declare `using State = ...;` and take `fetch(World&, State&)`: the state is stored per system.
template<typename T>
struct SystemParam {
    static_assert(always_false<T>,
        "Unsupported system parameter: take resources as T& / const T& / T*, queries as Query<...>, "
        "system state as Local<T>");
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
    static Query<Cs...> fetch(World& world) { return Query<Cs...> { world.scene().registry() }; }
    static bool available(const World&) { return true; }
    static std::string_view typeName() { return entt::type_name<Query<Cs...>>::value(); }
};

template<typename... Cs>
struct SystemParam<const Query<Cs...>&> : SystemParam<Query<Cs...>> {};

template<typename... Cs>
struct SystemParam<Query<Cs...>&> {
    static_assert(always_false<Cs...>, "Take Query<...> by value or const&, not by non-const reference");
};

template<typename T>
struct SystemParam<Local<T>> {
    static_assert(std::is_default_constructible_v<T>, "Local<T> requires a default-constructible T");
    using State = T;
    static Local<T> fetch(World&, T& state) { return Local<T> { state }; }
    static bool available(const World&) { return true; }
    static std::string_view typeName() { return entt::type_name<Local<T>>::value(); }
};

template<typename T>
struct SystemParam<const Local<T>&> : SystemParam<Local<T>> {};

template<typename T>
struct SystemParam<Local<T>&> {
    static_assert(always_false<T>, "Take Local<T> by value or const&, not by non-const reference");
};

namespace detail {

struct no_state {};

template<typename P>
struct param_state { using type = no_state; };

template<typename P>
    requires requires { typename SystemParam<P>::State; }
struct param_state<P> { using type = typename SystemParam<P>::State; };

template<typename P>
using param_state_t = typename param_state<P>::type;

template<typename P>
decltype(auto) fetchParam(World& world, param_state_t<P>& state) {
    if constexpr (std::is_same_v<param_state_t<P>, no_state>) {
        return SystemParam<P>::fetch(world);
    } else {
        return SystemParam<P>::fetch(world, state);
    }
}

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
    // States are indexed by parameter position, so two Local<T> of the same T get separate slots.
    system.run = [f = std::forward<F>(f), states = std::tuple<param_state_t<Args>...> {}](World& world) mutable {
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            f(fetchParam<Args>(world, std::get<I>(states))...);
        }(std::index_sequence_for<Args...> {});
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
