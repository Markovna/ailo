#pragma once

#include <vector>

template<class T>
class ResourceAllocator {
 public:
  template<class...Args>
  uint32_t allocate(Args&&... args) {
    uint32_t handle;

    if(m_numHandles >= m_dense.size()) {
      uint32_t index = m_dense.size();
      m_dense.push_back(index);
      m_sparse.push_back(index);
      m_resources.emplace_back(std::forward<Args>(args)...);
      handle = index;
    } else {
      uint32_t index = m_numHandles;
      handle = m_dense[index];
      m_sparse[handle] = index;
      m_resources[handle] = T(std::forward<Args>(args)...);
    }

    ++m_numHandles;

    return handle;
  }

  T& get(uint32_t handle) {
    return m_resources[handle];
  }

  void free(uint32_t handle) {
    uint16_t index = m_sparse[handle];

    --m_numHandles;

    uint16_t temp = m_dense[m_numHandles];
    m_dense[m_numHandles] = handle;

    m_sparse[temp] = index;
    m_dense[index] = temp;
  }

private:
  std::vector<uint32_t> m_dense;
  std::vector<uint32_t> m_sparse;
  std::vector<T> m_resources;
  uint32_t m_numHandles = 0;
};
