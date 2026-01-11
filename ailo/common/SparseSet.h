#pragma once
#include <vector>
#include <string>
#include <memory>
#include <algorithm>

namespace ailo {

template<typename T>
class RawData {
public:
    explicit RawData(size_t capacity) : m_data(alloc(capacity)), m_capacity(capacity) {}
    RawData() = default;

    RawData(const RawData&) = delete;
    RawData& operator=(const RawData&) = delete;

    RawData(RawData&& other) noexcept : m_data(other.m_data), m_capacity(other.m_capacity) {
        other.m_data = nullptr;
        other.m_capacity = 0;
    }

    RawData& operator=(RawData&& other) noexcept {
        swap(other);
        return *this;
    }

    ~RawData() { free(m_data); }

    T* data() { return m_data; }
    size_t capacity() const { return m_capacity; }

    T* operator+(size_t diff) { return m_data + diff; }
    const T* operator+(size_t diff) const { return m_data + diff; }

    T& operator[](size_t idx) { return m_data[idx]; }
    const T& operator[](size_t idx) const { return m_data[idx]; }

private:
    static T* alloc(size_t size) {
        return static_cast<T*>(operator new(size * sizeof(T)));
    }

    static void free(T* ptr) {
        operator delete(ptr);
    }

    void swap(RawData& other) noexcept {
        std::swap(m_data, other.m_data);
        std::swap(m_capacity, other.m_capacity);
    }

private:
    T* m_data = nullptr;
    size_t m_capacity = 0;
};

template<typename Key, typename Value>
class SparseSet {
public:
    static_assert(std::is_unsigned_v<Key> && std::is_integral_v<Key>, "Key must be unsigned integral");

    template<typename T>
      class Iterator {
      public:
          // Required type aliases
          using iterator_category = std::random_access_iterator_tag;
          using value_type = T;
          using difference_type = std::ptrdiff_t;
          using pointer = T*;
          using reference = T&;

          // Constructors
          Iterator() : ptr_(nullptr) {}
          explicit Iterator(pointer ptr) : ptr_(ptr) {}

          // Dereference operators
          reference operator*() const { return *ptr_; }
          pointer operator->() const { return ptr_; }
          reference operator[](difference_type n) const { return ptr_[n]; }

          // Increment/Decrement
          Iterator& operator++() { ++ptr_; return *this; }
          Iterator operator++(int) { Iterator tmp = *this; ++ptr_; return tmp; }
          Iterator& operator--() { --ptr_; return *this; }
          Iterator operator--(int) { Iterator tmp = *this; --ptr_; return tmp; }

          // Arithmetic operators
          Iterator& operator+=(difference_type n) { ptr_ += n; return *this; }
          Iterator& operator-=(difference_type n) { ptr_ -= n; return *this; }

          Iterator operator+(difference_type n) const { return SimpleIterator(ptr_ + n); }
          Iterator operator-(difference_type n) const { return SimpleIterator(ptr_ - n); }

          friend Iterator operator+(difference_type n, const Iterator& it) {
              return SimpleIterator(it.ptr_ + n);
          }

          difference_type operator-(const Iterator& other) const {
              return ptr_ - other.ptr_;
          }

          bool operator==(const Iterator& other) const { return ptr_ == other.ptr_; }
          bool operator!=(const Iterator& other) const { return ptr_ != other.ptr_; }
          bool operator<(const Iterator& other) const { return ptr_ < other.ptr_; }
          bool operator>(const Iterator& other) const { return ptr_ > other.ptr_; }
          bool operator<=(const Iterator& other) const { return ptr_ <= other.ptr_; }
          bool operator>=(const Iterator& other) const { return ptr_ >= other.ptr_; }

          auto operator<=>(const Iterator& other) const = default;

      private:
          pointer ptr_;
      };

    using iterator = Iterator<Value>;
    using const_iterator = Iterator<const Value>;
    using value_type = Value;
    using reference = value_type&;

    iterator begin() { return Iterator{ m_values.data() }; }
    iterator end() { return Iterator{ m_values.data() + m_size }; }
    const_iterator begin() const { return Iterator{m_values.data() }; }
    const_iterator end() const { return Iterator{ m_values.data() + m_size }; }

    SparseSet() = default;
    SparseSet(const SparseSet& other)
        : m_values(other.size()),
        m_size(other.m_size),
        m_sparse(other.m_sparse),
        m_dense(other.m_dense) {
        std::uninitialized_copy_n(other.m_values.data(), other.m_size, m_values.data());
    }

    SparseSet(SparseSet&& other) noexcept
        : m_values(std::move(other.m_values)),
        m_size(other.m_size),
        m_sparse(std::move(other.m_sparse)),
        m_dense(std::move(other.m_dense)) {

        other.m_size = 0;
    }

    SparseSet& operator=(const SparseSet& other) {
        SparseSet tmp(other);
        swap(tmp);
        return *this;
    }

    SparseSet& operator=(SparseSet&& other) noexcept {
        swap(other);
        return *this;
    }

    ~SparseSet() {
        std::destroy_n(m_values.data(), m_size);
    }

    size_t size() const { return m_size; }

    template<typename... Args>
    std::pair<Key, reference> emplace(Args&& ...args) {
        auto key = insertKey();
        auto& ref = emplaceBack(std::forward<Args>(args)...);
        return {key, ref};
    }

    std::pair<Key, iterator> insert(const Value& value) {
        return emplace(value);
    }

    std::pair<Key, iterator> insert(Value&& value) {
        return emplace(std::move(value));
    }

    void erase(Key key) {
        if (!contains(key)) {
            return;
        }

        auto denseIdx = m_sparse[key];
        auto lastDenseIdx = size() - 1;

        // swap remove
        auto lastKey = m_dense[lastDenseIdx];
        m_dense[denseIdx] = lastKey;
        m_sparse[lastKey] = denseIdx;

        std::destroy_at(m_values + denseIdx);
        std::uninitialized_move_n(m_values + lastDenseIdx, 1, m_values + denseIdx);
        m_size--;

        // update last dense idx for reuse
        m_dense[lastDenseIdx] = key;
        m_sparse[key] = lastDenseIdx;
    }

    Value& operator[](const Key& key) {
        return m_values[m_sparse[key]];
    }

    bool contains(Key key) {
        if(key >= m_sparse.size()) { return false; }

        auto denseIdx = m_sparse[key];
        auto currentIdx = m_dense[denseIdx];
        return denseIdx < size() && key == currentIdx;
    }

    void reserve(size_t size) {
        if (size > m_values.capacity()) {
            RawData<Value> newValues(size);
            std::uninitialized_move_n(m_values.data(), m_size, newValues.data());
            std::destroy_n(m_values.data(), m_size);
            std::swap(m_values, newValues);
        }
    }

    iterator find(const Key& key) {
        return m_values.begin() + m_sparse[key];
    }

    const_iterator find(const Key& key) const {
        return m_values.begin() + m_sparse[key];
    }

private:
    template<class ...Args>
    reference emplaceBack(Args&& ...args) {
        if (m_size == m_values.capacity()) {
            reserve(m_size == 0 ? 4 : m_values.capacity() * 2);
        }
        auto ptr = std::construct_at(m_values.data() + m_size, std::forward<Args>(args)...);
        m_size++;
        return *ptr;
    }

    void swap(SparseSet& other) noexcept {
        std::swap(m_values, other.m_values);
        std::swap(m_size, other.m_size);
        std::swap(m_sparse, other.m_sparse);
        std::swap(m_dense, other.m_dense);
    }

    Key insertKey() {
        auto denseIdx = size();

        // try to reuse last freed index
        if(denseIdx < m_dense.size()) {
            return static_cast<Key>(m_dense[denseIdx]);
        }

        // allocate new index
        auto key = static_cast<Key>(m_sparse.size());

        m_dense.push_back(key);
        m_sparse.push_back(denseIdx);

        return key;
    }

private:
    RawData<Value> m_values;
    size_t m_size = 0;

    std::vector<Key> m_sparse;
    std::vector<Key> m_dense;
};

}
