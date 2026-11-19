#include "core/name.hpp"

#include <deque>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace star {
namespace {

struct TransparentStringHash {
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
        return std::hash<std::string_view>{}(value);
    }

    [[nodiscard]] std::size_t operator()(const std::string &value) const noexcept {
        return operator()(std::string_view(value));
    }
};

class NameRegistry final {
  public:
    NameRegistry() {
        m_strings.emplace_back();
        m_lookup.emplace(m_strings.front(), 0);
    }

    [[nodiscard]] std::uint32_t intern(std::string_view text) {
        const auto existing = m_lookup.find(text);
        if (existing != m_lookup.end()) {
            return existing->second;
        }

        const auto next_id = static_cast<std::uint32_t>(m_strings.size());
        m_strings.emplace_back(text);
        m_lookup.emplace(m_strings.back(), next_id);
        return next_id;
    }

    [[nodiscard]] std::string_view find(std::uint32_t id) const {
        std::shared_lock lock(m_mutex);
        if (id >= m_strings.size()) {
            return {};
        }

        // Elements are append-only and never erased, so the string storage stays valid.
        const std::string *value = &m_strings[id];
        return *value;
    }

    std::shared_mutex &getMutex() { return m_mutex; }

  private:
    mutable std::shared_mutex m_mutex;
    std::deque<std::string> m_strings;
    std::unordered_map<std::string, std::uint32_t, TransparentStringHash, std::equal_to<>> m_lookup;
};

[[nodiscard]] NameRegistry &getNameRegistry() {
    // Intentionally never destroyed so global Name values remain valid during static teardown.
    static NameRegistry *registry = new NameRegistry();
    return *registry;
}

} // namespace

Name::Name(std::string_view text) {
    if (text.empty()) {
        return;
    }

    auto &registry = getNameRegistry();
    std::unique_lock lock(registry.getMutex());
    m_id = registry.intern(text);
}

std::string_view Name::getString() const { return getNameRegistry().find(m_id); }

} // namespace star
