#pragma once

#include <cstddef>
#include <functional>
#include <utility>

namespace utils {

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

template <class T>
void hash_combine(std::size_t& seed, const T& v) {
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed<<6) + (seed>>2);
}

template<typename F>
class [[nodiscard]] scope_exit {
public:
    template<typename Fn>
    explicit scope_exit(Fn&& fn) : m_fn(std::forward<Fn>(fn)) {}

    scope_exit(const scope_exit&) = delete;
    scope_exit& operator=(const scope_exit&) = delete;

    ~scope_exit() { if (m_active) m_fn(); }

    void release() noexcept { m_active = false; }

private:
    F m_fn;
    bool m_active = true;
};

template<typename F>
scope_exit(F) -> scope_exit<F>;

}
