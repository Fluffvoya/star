#include "core/cvar.hpp"

#include <charconv>
#include <cmath>
#include <exception>
#include <functional>
#include <locale>
#include <map>
#include <mutex>
#include <sstream>
#include <system_error>
#include <type_traits>
#include <vector>

namespace star {

namespace detail {

struct CVarRecord {
    std::string_view name;
    CVarInfo info;
};

} // namespace detail

namespace {

struct Registry {
    std::mutex mutex;
    std::map<std::string, detail::CVarRecord, std::less<>> records;
};

Registry &getRegistry() {
    static Registry registry;
    return registry;
}

bool isValidName(std::string_view name) noexcept {
    bool segment_start = true;
    for (const char character : name) {
        if (character == '.' && !segment_start) {
            segment_start = true;
            continue;
        }
        const bool letter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
        const bool digit = character >= '0' && character <= '9';
        if (!letter && character != '_' && (segment_start || !digit)) {
            return false;
        }
        segment_start = false;
    }
    return !segment_start;
}

bool isValidValue(const CVarValue &value) noexcept {
    const auto *number = std::get_if<float>(&value);
    return number == nullptr || std::isfinite(*number);
}

std::expected<float, CVarError> parseFloat(std::string_view text) noexcept {
    try {
        std::istringstream stream{std::string(text)};
        stream.imbue(std::locale::classic());
        float parsed{};
        if (!(stream >> std::noskipws >> parsed) || !stream.eof() || !std::isfinite(parsed)) {
            return std::unexpected(CVarError::InvalidValue);
        }
        return parsed;
    } catch (const std::exception &) {
        return std::unexpected(CVarError::AllocationFailed);
    }
}

std::expected<void, CVarError> setValueFromString(CVarValue &value, std::string_view text) noexcept {
    return std::visit(
        [text](auto &current) -> std::expected<void, CVarError> {
            using Value = std::remove_cvref_t<decltype(current)>;
            if constexpr (std::same_as<Value, std::string>) {
                try {
                    current.assign(text);
                } catch (const std::exception &) {
                    return std::unexpected(CVarError::AllocationFailed);
                }
            } else if constexpr (std::same_as<Value, bool>) {
                if (text == "true" || text == "1") {
                    current = true;
                } else if (text == "false" || text == "0") {
                    current = false;
                } else {
                    return std::unexpected(CVarError::InvalidValue);
                }
            } else if constexpr (std::same_as<Value, float>) {
                const auto parsed = parseFloat(text);
                if (!parsed) {
                    return std::unexpected(parsed.error());
                }
                current = *parsed;
            } else {
                if (text.empty()) {
                    return std::unexpected(CVarError::InvalidValue);
                }
                int parsed{};
                const char *end = text.data() + text.size();
                const auto [last, error] = std::from_chars(text.data(), end, parsed);
                if (error != std::errc{} || last != end) {
                    return std::unexpected(CVarError::InvalidValue);
                }
                current = parsed;
            }
            return {};
        },
        value);
}

} // namespace

std::expected<CVarInfo, CVarError> CVarRegistry::getInfo(std::string_view name) noexcept {
    auto &registry = getRegistry();
    const std::lock_guard lock(registry.mutex);
    const auto found = registry.records.find(name);
    if (found == registry.records.end()) {
        return std::unexpected(CVarError::NotFound);
    }
    try {
        return found->second.info;
    } catch (const std::exception &) {
        return std::unexpected(CVarError::AllocationFailed);
    }
}

std::expected<void, CVarError> CVarRegistry::setFromString(std::string_view name, std::string_view text) noexcept {
    auto &registry = getRegistry();
    const std::lock_guard lock(registry.mutex);
    const auto found = registry.records.find(name);
    if (found == registry.records.end()) {
        return std::unexpected(CVarError::NotFound);
    }
    return setValueFromString(found->second.info.value, text);
}

std::expected<void, CVarError> CVarRegistry::setFromStrings(std::span<const CVarAssignment> assignments) noexcept {
    auto &registry = getRegistry();
    const std::lock_guard lock(registry.mutex);
    try {
        std::vector<std::pair<CVarValue *, CVarValue>> changes;
        changes.reserve(assignments.size());
        for (const auto &[name, text] : assignments) {
            const auto found = registry.records.find(name);
            if (found == registry.records.end()) {
                return std::unexpected(CVarError::NotFound);
            }
            CVarValue &current = found->second.info.value;
            CVarValue value = current;
            if (const auto parsed = setValueFromString(value, text); !parsed) {
                return std::unexpected(parsed.error());
            }
            changes.emplace_back(&current, std::move(value));
        }
        for (auto &[current, value] : changes) {
            *current = std::move(value);
        }
        return {};
    } catch (const std::exception &) {
        return std::unexpected(CVarError::AllocationFailed);
    }
}

namespace detail {

std::expected<CVarRecord *, CVarError> registerCVar(std::string_view name, CVarValue value,
                                                    std::string_view description, CVarFlags flags) noexcept {
    if (!isValidName(name)) {
        return std::unexpected(CVarError::InvalidName);
    }
    if (!isValidValue(value)) {
        return std::unexpected(CVarError::InvalidValue);
    }

    auto &registry = getRegistry();
    const std::lock_guard lock(registry.mutex);
    const auto position = registry.records.lower_bound(name);
    if (position != registry.records.end() && position->first == name) {
        return std::unexpected(CVarError::DuplicateName);
    }

    try {
        CVarRecord record{{}, {std::string(description), flags, value, std::move(value)}};
        const auto inserted = registry.records.emplace_hint(position, std::string(name), std::move(record));
        inserted->second.name = inserted->first;
        return &inserted->second;
    } catch (const std::exception &) {
        return std::unexpected(CVarError::AllocationFailed);
    }
}

void unregisterCVar(CVarRecord &record) noexcept {
    auto &registry = getRegistry();
    const std::lock_guard lock(registry.mutex);
    registry.records.erase(registry.records.find(record.name));
}

std::expected<CVarValue, CVarError> readCVar(const CVarRecord &record) noexcept {
    const std::lock_guard lock(getRegistry().mutex);
    try {
        return record.info.value;
    } catch (const std::exception &) {
        return std::unexpected(CVarError::AllocationFailed);
    }
}

std::expected<void, CVarError> writeCVar(CVarRecord &record, CVarValue value) noexcept {
    if (!isValidValue(value)) {
        return std::unexpected(CVarError::InvalidValue);
    }

    const std::lock_guard lock(getRegistry().mutex);
    if (record.info.value.index() != value.index()) {
        return std::unexpected(CVarError::InvalidValue);
    }
    record.info.value = std::move(value);
    return {};
}

} // namespace detail

} // namespace star
