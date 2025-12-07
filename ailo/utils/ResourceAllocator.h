#pragma once

#include <vector>

namespace ailo {

template<typename T>
class Handle {
 public:
  using HandleId = uint32_t;

  Handle() noexcept = default;

  Handle(const Handle&) noexcept = default;
  Handle& operator=(const Handle&) noexcept = default;

  Handle(Handle&& rhs) noexcept
      : id(rhs.id) {
    rhs.id = INVALID_ID;
  }

  explicit Handle(HandleId id) noexcept: id(id) {}

  Handle& operator=(Handle&& rhs) noexcept {
    if (this != &rhs) {
      id = rhs.id;
      rhs.id = INVALID_ID;
    }
    return *this;
  }

  bool operator==(Handle other) const noexcept { return id == other.id; }
  bool operator!=(Handle other) const noexcept { return id != other.id; }

  explicit constexpr operator bool() const noexcept { return id != INVALID_ID; }

  template<typename D, typename = std::enable_if_t<std::is_base_of<T, D>::value> >
  Handle(const Handle<D>& derived) noexcept : Handle(derived.id) {}

  HandleId getId() const noexcept { return id; }

  template<typename B>
  static typename std::enable_if_t<
      std::is_base_of_v<B, T>, Handle>
  cast(Handle<B>& from) {
    return Handle<T>(from.getId());
  }

 private:
  static constexpr HandleId INVALID_ID = HandleId { std::numeric_limits<uint32_t>::max() };

  template<class U> friend
  class Handle;

 private:
  HandleId id = INVALID_ID;
};

template<class T>
class ResourceAllocator {
 public:
  using Handle = Handle<T>;

  template<class...Args>
  Handle allocate(Args&& ... args) {
    uint32_t handle;

    if (m_numHandles >= m_dense.size()) {
      auto index = m_dense.size();
      m_dense.push_back(index);
      m_sparse.push_back(index);
      m_resources.emplace_back(std::forward<Args>(args)...);
      handle = index;
    } else {
      auto index = m_numHandles;
      handle = m_dense[index];
      m_sparse[handle] = index;
      m_resources[handle] = T(std::forward<Args>(args)...);
    }

    ++m_numHandles;

    return Handle(handle);
  }

  T& get(Handle handle) {
    auto id = handle.getId();
    return m_resources[id];
  }

  void free(Handle handle) {
    auto index = m_sparse[handle.getId()];

    --m_numHandles;

    auto temp = m_dense[m_numHandles];
    m_dense[m_numHandles] = handle.getId();

    m_sparse[temp] = index;
    m_dense[index] = temp;
  }

  size_t size() { return m_numHandles; }

 private:
  std::vector<uint32_t> m_dense;
  std::vector<uint32_t> m_sparse;
  std::vector<T> m_resources;
  uint32_t m_numHandles = 0;
};

}