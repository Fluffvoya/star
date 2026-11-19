#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace star {

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
