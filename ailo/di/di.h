#pragma once
#include <iostream>
#include <memory>
#include <type_traits>

namespace ailo::di {

template<class Key, class Value>
struct pair {};

template<class T>
using remove_qualifiers_t = std::remove_cv_t<std::remove_reference_t<std::remove_pointer_t<T>>>;

template<class T> struct deref_type { using type = T; };
template<class T> struct deref_type<std::shared_ptr<T>> { using type = T; };
template<class T, class TDeleter> struct deref_type<std::unique_ptr<T, TDeleter>> { using type = T; };

template <class T>
using decay_t = typename deref_type<remove_qualifiers_t<T>>::type;

// Wildcard for compile-time detection only (never actually instantiated).
struct detect_arg {
    template <class T> operator T();
    template <class T> operator T&() const;
    template <class T> operator const T&() const;
    template <class T> operator T&&() const;
};

// The "first arg" version converts to anything EXCEPT TParent itself.
// This prevents the copy/move constructor from being detected as a 1-arg ctor.
// Without this, every copyable type would appear to have arity >= 1,
// because T(detect_arg) would match the copy constructor T(const T&).
template <class TParent>
struct detect_arg_1st {
    template <class T, std::enable_if_t<
        !std::is_same<std::decay_t<T>, TParent>::value, int> = 0>
    operator T();
    template <class T, std::enable_if_t<
        !std::is_same<std::decay_t<T>, TParent>::value, int> = 0>
    operator T&() const;
    template <class T, std::enable_if_t<
        !std::is_same<std::decay_t<T>, TParent>::value, int> = 0>
    operator const T&() const;
    template <class T, std::enable_if_t<
        !std::is_same<std::decay_t<T>, TParent>::value, int> = 0>
    operator T&&() const;
};

template <class T, int>
using get_t = T;

template<class T, class Sequence, class = void>
struct ctor_arity;

template<class T> struct ctor_arity<T, std::index_sequence<>,
        std::enable_if_t<std::is_default_constructible_v<T>>> {
    static constexpr int value = 0;
};

template<class T> struct ctor_arity<T, std::index_sequence<>,
        std::enable_if_t<!std::is_default_constructible_v<T>>> {
    static constexpr int value = -1;
};

template<class T> struct ctor_arity<T, std::index_sequence<0>,
        std::enable_if_t<std::is_constructible_v<T, detect_arg_1st<T>>>> {
    static constexpr int value = 1;
};

template<class T> struct ctor_arity<T, std::index_sequence<0>,
        std::enable_if_t<!std::is_constructible_v<T, detect_arg_1st<T>>>>
    : ctor_arity<T, std::index_sequence<>> {
};

template<class T, int... Ns> struct ctor_arity<T, std::index_sequence<Ns...>,
        std::enable_if_t<(sizeof...(Ns) > 1) && std::is_constructible_v<T, get_t<detect_arg, Ns>...>>> {
    static constexpr int value = sizeof...(Ns);
};

template<class T, int... Ns> struct ctor_arity<T, std::index_sequence<Ns...>,
        std::enable_if_t<(sizeof...(Ns) > 1) && !std::is_constructible_v<T, get_t<detect_arg, Ns>...>>>
    : ctor_arity<T, std::make_index_sequence<sizeof...(Ns) - 1>> {
};

constexpr int max_ctor_arity = 6;

template <class T>
constexpr int ctor_arity_v = ctor_arity<T, std::make_index_sequence<max_ctor_arity>>::value;

namespace converters {

template<class T>
struct shared {
    template<class I>
    using enable_conversion_t = std::enable_if_t<std::is_convertible_v<T*, I*>>;

    template<class I, class = enable_conversion_t<I>>
    operator std::shared_ptr<I>() const { return from; }

    template<class I, class = enable_conversion_t<I>>
    operator const I*() const { return from.get(); }

    template<class I, class = enable_conversion_t<I>>
    operator I*() const { return from.get(); }

    operator const T&() const { return *from; }
    operator T&() const { return *from; }

    std::shared_ptr<T> from;
};

template<class T>
struct unique {
    template<class I>
    using enable_conversion_t = std::enable_if_t<std::is_convertible_v<T*, I*>>;

    template<class I, class = enable_conversion_t<I>>
    operator std::shared_ptr<I>() const { return std::shared_ptr<I>(from); }

    template<class I, class = enable_conversion_t<I>>
    operator std::unique_ptr<I>() const { return std::unique_ptr<I>(from); }

    template<class I, class = enable_conversion_t<I>>
    operator const I*() const { return from; }

    template<class I, class = enable_conversion_t<I>>
    operator I*() const { return from; }

    T* from;
};

}

struct scope_unique {
    template <class TExpected, class TGiven, class TInjector>
    static TExpected create(const TInjector& inj);
    // (defined after construct<> and injector)
};

struct scope_singleton {
    template <class TExpected, class TGiven, class TInjector>
    static TExpected create(const TInjector& inj);

    // Singleton storage keyed on (TGiven, TInjector) only — not on TExpected.
    // This ensures that create<shared_ptr<I>>(), create<I*>(), and create<I&>()
    // all return the same underlying object when the binding is scope_singleton.
    template <class TGiven, class TInjector>
    static std::shared_ptr<TGiven>& instance(const TInjector& inj);
};

struct scope_instance {};

struct dependency_base {};

// Primary template: for unique and singleton scopes.
// Stateless — just carries type information.
template <class TScope, class TExpected, class TGiven>
struct dependency : dependency_base,
                    pair<TExpected, dependency<TScope, TExpected, TGiven>> {
    using scope    = TScope;
    using expected = TExpected;
    using given    = TGiven;
};

// Specialization for instance scope: stores the actual shared_ptr.
template <class TExpected, class TGiven>
struct dependency<scope_instance, TExpected, TGiven>
    : dependency_base,
      pair<TExpected, dependency<scope_instance, TExpected, TGiven>> {
    using scope    = scope_instance;
    using expected = TExpected;
    using given    = TGiven;

    std::shared_ptr<TExpected> instance_;

    dependency() = default;
    explicit dependency(std::shared_ptr<TExpected> ptr) : instance_(std::move(ptr)) {}

    std::shared_ptr<TExpected> get_instance() const { return instance_; }
};

struct binder {
    // Fallback: no explicit binding found.
    // Returns a default "auto-construct T" dependency.
    template <class T>
    static dependency<scope_unique, T, T> resolve_impl(...) {
        return {};
    }

    // Match: injector has pair<T, TDependency> as a base.
    // The compiler can implicitly convert injector* to pair<T, TDep>*
    // because injector inherits from the dependency which inherits from the pair.
    template <class T, class TDependency>
    static TDependency& resolve_impl(pair<T, TDependency>* dep) {
        return static_cast<TDependency&>(*dep);
    }

    // Compile-time resolve: what dependency TYPE would we get for T?
    // Used to inspect scope/given at compile time without an injector instance.
    template <class T>
    static dependency<scope_unique, T, T> resolve_type(...);

    template <class T, class TDependency>
    static TDependency resolve_type(pair<T, TDependency>*);

    template <class T, class TInjector>
    using resolve_t = decltype(resolve_type<decay_t<T>>(std::declval<TInjector*>()));
};

template <class TInjector, class TParent>
struct any_type {
    const TInjector& injector_;

    // Convert to shared_ptr<T>: the primary use case for DI.
    template <class T>
    operator std::shared_ptr<T>() const {
        return injector_.template create<std::shared_ptr<T>>();
    }

    // Convert to unique_ptr<T>: transfers ownership of a freshly constructed instance.
    template <class T>
    operator std::unique_ptr<T>() const {
        return injector_.template create<std::unique_ptr<T>>();
    }

    // Convert to a value type T, but NOT to TParent (prevents recursion).
    template <class T,
              std::enable_if_t<
                  !std::is_same<std::decay_t<T>, TParent>::value &&
                  !std::is_abstract<T>::value &&
                  std::is_class<T>::value, int> = 0>
    operator T() const {
        return injector_.template create<T>();
    }

    // Convert to fundamental types (int, double, etc.): value-initialize.
    // Pointers are excluded so that operator T*() below handles them instead.
    template <class T,
              std::enable_if_t<
                  !std::is_class<T>::value &&
                  !std::is_abstract<T>::value &&
                  !std::is_pointer<T>::value, int> = 0>
    operator T() const {
        return T{};
    }

    // Convert to raw pointer T*.
    // Requires singleton or instance scope: unique scope would leak the object.
    template <class T,
              std::enable_if_t<
                  !std::is_same<std::decay_t<T>, TParent>::value, int> = 0>
    operator T*() const {
        using dep_type = binder::resolve_t<T, TInjector>;
        static_assert(!std::is_same<typename dep_type::scope, scope_unique>::value,
            "Injecting T* requires singleton or instance scope. "
            "Use to_singleton<>() or to_value() to avoid leaking the object.");
        return injector_.template create<T*>();
    }

    // Convert to reference T&.
    // Requires singleton or instance scope: unique scope would produce a dangling reference.
    template <class T,
              std::enable_if_t<
                  !std::is_same<std::decay_t<T>, TParent>::value &&
                  std::is_class<T>::value, int> = 0>
    operator T&() const {
        using dep_type = binder::resolve_t<T, TInjector>;
        static_assert(!std::is_same<typename dep_type::scope, scope_unique>::value,
            "Injecting T& requires singleton or instance scope. "
            "Use to_singleton<>() or to_value() to avoid a dangling reference.");
        return injector_.template create<T&>();
    }
};

template <class TGiven, class Sequence>
struct constructor;

template<class TGiven, int... Ns>
struct constructor<TGiven, std::index_sequence<Ns...>> {
    template <class TInjector>
    static TGiven* get(const TInjector& inj) {
        return new TGiven(get_t<any_type<TInjector, TGiven>, Ns>{inj}...);
    }
};

// Convenience wrapper that picks the right arity automatically.
template <class TGiven, class TInjector>
TGiven* construct(const TInjector& inj) {
    constexpr int arity = ctor_arity_v<TGiven>;
    static_assert(arity >= 0, "Cannot construct TGiven");
    return constructor<TGiven, std::make_index_sequence<arity>>::get(inj);
}

template <class TExpected, class TGiven, class TInjector>
TExpected scope_unique::create(const TInjector& inj) {
    // Construct TGiven on the heap, wrap in shared_ptr<TExpected>.
    // The implicit conversion TGiven* -> TExpected* works because
    // TGiven inherits from TExpected (or is the same type).
    return converters::unique<TGiven> { construct<TGiven>(inj) };
}

template <class TGiven, class TInjector>
std::shared_ptr<TGiven>& scope_singleton::instance(const TInjector& inj) {
    static std::shared_ptr<TGiven> instance(construct<TGiven>(inj));
    return instance;
}

template <class TExpected, class TGiven, class TInjector>
TExpected scope_singleton::create(const TInjector& inj) {
    return converters::shared<TGiven> { instance<TGiven>(inj) };
}

struct injector_base {};

template <class... TDeps>
class injector : public injector_base, public TDeps... {
public:

    explicit injector(TDeps... deps) : TDeps(std::move(deps))... {}

    template <class T>
    decltype(auto) create() const {
        using dep_type = binder::resolve_t<T, injector>;
        using scope = typename dep_type::scope;
        using given = typename dep_type::given;
        return create_by_scope<T, given, dep_type>(scope{});
    }

private:
    // -- Scope dispatch --

    template <class T, class TGiven, class TDep>
    decltype(auto) create_by_scope(scope_unique) const {
        return scope_unique::create<T, TGiven>(*this);
    }

    template <class T, class TGiven, class TDep>
    decltype(auto) create_by_scope(scope_singleton) const {
        return scope_singleton::create<T, TGiven>(*this);
    }

    template <class T, class TGiven, class TDep>
    decltype(auto) create_by_scope(scope_instance) const {
        // For instance scope, the dependency object stores the value.
        // We inherit from TDep, so we can access it directly.
        auto& dep = static_cast<const TDep&>(*this);
        return static_cast<T>(converters::shared<TGiven> { dep.get_instance() });
    }
};

template <class TExpected>
struct binding_builder {
    template <class TGiven>
    static dependency<scope_unique, TExpected, TGiven> to() {
        return {};
    }

    template <class TGiven>
    static dependency<scope_singleton, TExpected, TGiven> to_singleton() {
        return {};
    }

    static dependency<scope_instance, TExpected, TExpected>
    to_value(std::shared_ptr<TExpected> ptr) {
        return dependency<scope_instance, TExpected, TExpected>(std::move(ptr));
    }
};

template <class T>
constexpr binding_builder<T> bind{};

template <class... TDeps>
auto make_injector(TDeps... deps) {
    return injector<TDeps...>(std::move(deps)...);
}

} // namespace di