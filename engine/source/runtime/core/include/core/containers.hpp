#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace star {

template <class T, std::size_t InlineCapacity> class SmallVector final {
    static_assert(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_destructible_v<T>);

    using Allocator = std::allocator<T>;
    static constexpr std::size_t inline_count = InlineCapacity == 0 ? 1 : InlineCapacity;

  public:
    SmallVector() noexcept : m_data(inlineData()), m_capacity(InlineCapacity) {}

    SmallVector(const SmallVector &other) : SmallVector() {
        reserve(other.m_size);
        for (const T &value : other) {
            emplaceBack(value);
        }
    }

    SmallVector(SmallVector &&other) noexcept : SmallVector() { moveFrom(std::move(other)); }

    SmallVector &operator=(const SmallVector &other) {
        if (this == &other) {
            return *this;
        }
        clear();
        reserve(other.m_size);
        for (const T &value : other) {
            emplaceBack(value);
        }
        return *this;
    }

    SmallVector &operator=(SmallVector &&other) noexcept {
        if (this != &other) {
            clear();
            releaseHeap();
            m_data = inlineData();
            m_capacity = InlineCapacity;
            moveFrom(std::move(other));
        }
        return *this;
    }

    ~SmallVector() {
        clear();
        releaseHeap();
    }

    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] std::size_t size() const noexcept { return m_size; }
    [[nodiscard]] std::size_t capacity() const noexcept { return m_capacity; }
    [[nodiscard]] T *data() noexcept { return m_data; }
    [[nodiscard]] const T *data() const noexcept { return m_data; }
    [[nodiscard]] T &operator[](std::size_t index) noexcept { return m_data[index]; }
    [[nodiscard]] const T &operator[](std::size_t index) const noexcept { return m_data[index]; }
    [[nodiscard]] T &at(std::size_t index) {
        if (index >= m_size) {
            throw std::out_of_range("SmallVector index out of range");
        }
        return m_data[index];
    }
    [[nodiscard]] const T &at(std::size_t index) const {
        if (index >= m_size) {
            throw std::out_of_range("SmallVector index out of range");
        }
        return m_data[index];
    }
    [[nodiscard]] T *begin() noexcept { return m_data; }
    [[nodiscard]] const T *begin() const noexcept { return m_data; }
    [[nodiscard]] T *end() noexcept { return m_data + m_size; }
    [[nodiscard]] const T *end() const noexcept { return m_data + m_size; }

    void reserve(std::size_t requested_capacity) {
        if (requested_capacity <= m_capacity) {
            return;
        }
        const std::size_t new_capacity = std::max(requested_capacity, m_capacity == 0 ? 1 : m_capacity * 2);
        T *new_data = m_allocator.allocate(new_capacity);
        std::size_t moved = 0;
        try {
            for (; moved < m_size; ++moved) {
                std::construct_at(new_data + moved, std::move(m_data[moved]));
            }
        } catch (...) {
            while (moved != 0) {
                --moved;
                std::destroy_at(new_data + moved);
            }
            m_allocator.deallocate(new_data, new_capacity);
            throw;
        }
        const std::size_t old_size = m_size;
        destroyElements();
        releaseHeap();
        m_data = new_data;
        m_capacity = new_capacity;
        m_size = old_size;
    }

    template <class... Args> T &emplaceBack(Args &&...args) {
        if (m_size == m_capacity) {
            reserve(m_size + 1);
        }
        T *value = std::construct_at(m_data + m_size, std::forward<Args>(args)...);
        ++m_size;
        return *value;
    }

    void pushBack(const T &value) { emplaceBack(value); }
    void pushBack(T &&value) { emplaceBack(std::move(value)); }

    void popBack() noexcept {
        if (m_size != 0) {
            --m_size;
            std::destroy_at(m_data + m_size);
        }
    }

    void clear() noexcept { destroyElements(); }

  private:
    [[nodiscard]] T *inlineData() noexcept { return reinterpret_cast<T *>(m_inline_storage); }
    [[nodiscard]] const T *inlineData() const noexcept { return reinterpret_cast<const T *>(m_inline_storage); }

    void destroyElements() noexcept {
        while (m_size != 0) {
            --m_size;
            std::destroy_at(m_data + m_size);
        }
    }

    void releaseHeap() noexcept {
        if (m_data != inlineData()) {
            m_allocator.deallocate(m_data, m_capacity);
        }
    }

    void moveFrom(SmallVector &&other) noexcept {
        if (other.m_data != other.inlineData()) {
            m_data = other.m_data;
            m_size = other.m_size;
            m_capacity = other.m_capacity;
            other.m_data = other.inlineData();
            other.m_size = 0;
            other.m_capacity = InlineCapacity;
            return;
        }
        reserve(other.m_size);
        for (T &value : other) {
            emplaceBack(std::move(value));
        }
        other.clear();
    }

    Allocator m_allocator;
    T *m_data;
    std::size_t m_size = 0;
    std::size_t m_capacity;
    alignas(T) std::byte m_inline_storage[sizeof(T) * inline_count];
};

template <class Key, class Value, class Compare = std::less<Key>> class FlatMap final {
    using Entry = std::pair<Key, Value>;

  public:
    using const_iterator = typename std::vector<Entry>::const_iterator;

    [[nodiscard]] bool empty() const noexcept { return m_entries.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }
    [[nodiscard]] const_iterator begin() const noexcept { return m_entries.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return m_entries.end(); }

    [[nodiscard]] const Value *find(const Key &key) const noexcept {
        const auto position = lowerBound(key);
        return position != m_entries.end() && equivalent(position->first, key) ? &position->second : nullptr;
    }

    [[nodiscard]] Value *find(const Key &key) noexcept {
        const auto position = lowerBound(key);
        return position != m_entries.end() && equivalent(position->first, key) ? &position->second : nullptr;
    }

    [[nodiscard]] bool contains(const Key &key) const noexcept { return find(key) != nullptr; }

    template <class K, class V> bool insertOrAssign(K &&key, V &&value) {
        const auto position = lowerBound(key);
        if (position != m_entries.end() && equivalent(position->first, key)) {
            position->second = std::forward<V>(value);
            return false;
        }
        m_entries.emplace(position, std::forward<K>(key), std::forward<V>(value));
        return true;
    }

    bool erase(const Key &key) {
        const auto position = lowerBound(key);
        if (position == m_entries.end() || !equivalent(position->first, key)) {
            return false;
        }
        m_entries.erase(position);
        return true;
    }

    void clear() noexcept { m_entries.clear(); }

  private:
    [[nodiscard]] const_iterator lowerBound(const Key &key) const noexcept {
        return std::lower_bound(m_entries.begin(), m_entries.end(), key,
                                [this](const Entry &entry, const Key &value) { return m_compare(entry.first, value); });
    }

    [[nodiscard]] typename std::vector<Entry>::iterator lowerBound(const Key &key) noexcept {
        return std::lower_bound(m_entries.begin(), m_entries.end(), key,
                                [this](const Entry &entry, const Key &value) { return m_compare(entry.first, value); });
    }

    [[nodiscard]] bool equivalent(const Key &left, const Key &right) const noexcept {
        return !m_compare(left, right) && !m_compare(right, left);
    }

    std::vector<Entry> m_entries;
    Compare m_compare;
};

template <class Key, class Compare = std::less<Key>> class FlatSet final {
  public:
    using const_iterator = typename std::vector<Key>::const_iterator;

    [[nodiscard]] bool empty() const noexcept { return m_values.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_values.size(); }
    [[nodiscard]] const_iterator begin() const noexcept { return m_values.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return m_values.end(); }

    bool insert(Key value) {
        const auto position = std::lower_bound(m_values.begin(), m_values.end(), value, m_compare);
        if (position != m_values.end() && equivalent(*position, value)) {
            return false;
        }
        m_values.insert(position, std::move(value));
        return true;
    }

    bool contains(const Key &value) const noexcept {
        const auto position = std::lower_bound(m_values.begin(), m_values.end(), value, m_compare);
        return position != m_values.end() && equivalent(*position, value);
    }

    bool erase(const Key &value) {
        const auto position = std::lower_bound(m_values.begin(), m_values.end(), value, m_compare);
        if (position == m_values.end() || !equivalent(*position, value)) {
            return false;
        }
        m_values.erase(position);
        return true;
    }

    void clear() noexcept { m_values.clear(); }

  private:
    [[nodiscard]] bool equivalent(const Key &left, const Key &right) const noexcept {
        return !m_compare(left, right) && !m_compare(right, left);
    }

    std::vector<Key> m_values;
    Compare m_compare;
};

template <class Key, class Value, class Hash = std::hash<Key>, class Equal = std::equal_to<Key>>
using HashMap = std::unordered_map<Key, Value, Hash, Equal>;

template <class T> class RingBuffer final {
  public:
    explicit RingBuffer(std::size_t capacity) : m_values(capacity) {}

    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }
    [[nodiscard]] bool full() const noexcept { return !m_values.empty() && m_size == m_values.size(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_size; }
    [[nodiscard]] std::size_t capacity() const noexcept { return m_values.size(); }

    template <class U> bool tryPush(U &&value) {
        if (m_values.empty() || full()) {
            return false;
        }
        m_values[(m_head + m_size) % m_values.size()] = std::forward<U>(value);
        ++m_size;
        return true;
    }

    [[nodiscard]] std::optional<T> pop() {
        if (empty()) {
            return std::nullopt;
        }
        std::optional<T> result(std::move(m_values[m_head]));
        m_head = (m_head + 1) % m_values.size();
        --m_size;
        return result;
    }

    [[nodiscard]] const T *front() const noexcept { return empty() ? nullptr : &m_values[m_head]; }
    [[nodiscard]] const T *back() const noexcept {
        return empty() ? nullptr : &m_values[(m_head + m_size - 1) % m_values.size()];
    }

    void clear() noexcept {
        m_head = 0;
        m_size = 0;
    }

  private:
    std::vector<T> m_values;
    std::size_t m_head = 0;
    std::size_t m_size = 0;
};

} // namespace star
