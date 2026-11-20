#pragma once

#include "core/guid.hpp"
#include "core/math.hpp"
#include "core/name.hpp"
#include "reflection/export.hpp"

#include <charconv>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace star::serialization {
class Archive;
}

namespace star::reflect {

using TypeId = std::uint64_t;
using ModuleId = std::uint64_t;
inline constexpr ModuleId builtin_module = 0;

[[nodiscard]] STAR_REFLECTION_API TypeId makeTypeId(std::string_view name) noexcept;

enum class TypeKind : std::uint8_t {
    Struct,
    Class,
    Enum,
    Primitive,
};

enum class PropertyKind : std::uint8_t {
    Boolean,
    SignedInteger,
    UnsignedInteger,
    FloatingPoint,
    String,
    Reflected,
};

enum class PropertyFlags : std::uint32_t {
    None = 0,
    Transient = 1u << 0,
    ReadOnly = 1u << 1,
};

constexpr PropertyFlags operator|(PropertyFlags left, PropertyFlags right) {
    return static_cast<PropertyFlags>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

constexpr bool hasFlag(PropertyFlags value, PropertyFlags flag) {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}

struct TypeInfo;
struct ContainerOps;

struct PropertyInfo {
    std::string_view name;
    PropertyKind kind;
    PropertyFlags flags;
    const TypeInfo *nested_type;
    void *(*get_mutable)(void *object);
    const void *(*get_const)(const void *object);
    std::string (*format_scalar)(const void *member) = nullptr;
    bool (*parse_scalar)(std::string_view text, void *member) = nullptr;
    std::string_view former_name{};
    std::string_view metadata{};
    const ContainerOps *container = nullptr;
    const TypeInfo *(*resolve_nested_type)() = nullptr;

    [[nodiscard]] const TypeInfo *nestedType() const {
        return resolve_nested_type == nullptr ? nested_type : resolve_nested_type();
    }
};

struct ContainerOps {
    bool is_mapping;
    PropertyInfo (*element)(const TypeInfo *nested_type);
    std::size_t (*size)(const void *container);
    void (*visit)(const void *container, void *context,
                  void (*entry)(void *context, std::string_view key, const void *value));
    bool (*replace)(void *container, std::size_t size, void *context, bool (*fill)(void *replacement, void *context));
    void *(*write)(void *container, std::size_t index, std::string_view key);
};

struct TypeOps {
    void (*construct)(void *object);
    void (*destroy)(void *object);
    void (*copy_construct)(void *destination, const void *source);
    bool (*equals)(const void *left, const void *right);
    void (*swap)(void *left, void *right) noexcept = nullptr;
};

struct EnumEntry {
    std::string_view name;
    std::uint64_t value;
};

struct EnumOps {
    std::uint64_t (*get)(const void *object);
    void (*set)(void *object, std::uint64_t value);
};

struct FunctionInfo {
    std::string_view name;
    std::string_view metadata;
    bool (*invoke)(void *object) noexcept;
};

struct BaseAccess {
    const void *(*get_const)(const void *object);
    void *(*get_mutable)(void *object);
};

struct TypeInfo {
    TypeId id;
    std::string_view name;
    TypeKind kind;
    std::size_t size;
    std::size_t alignment;
    std::span<const PropertyInfo> properties;
    TypeOps ops;
    ModuleId owner;
    std::uint32_t version;
    const TypeInfo *base;
    std::string_view former_name{};
    std::span<const EnumEntry> enum_entries{};
    EnumOps enum_ops{};
    std::span<const FunctionInfo> functions{};
    std::string_view metadata{};
    BaseAccess base_access{};
    void (*serialize)(void *object, serialization::Archive &archive) = nullptr;
};

[[nodiscard]] STAR_REFLECTION_API std::span<const TypeInfo *const> builtinTypes();

enum class RegistryErrorCode : std::uint8_t {
    DuplicateType,
    TypeOwnerMismatch,
    InvalidType,
    AllocationFailed,
};

struct RegistryError {
    RegistryErrorCode code;
    TypeId type_id;
    std::string message;
};

class STAR_REFLECTION_API TypeRegistry final {
  public:
    [[nodiscard]] const TypeInfo *find(TypeId id) const noexcept;
    [[nodiscard]] const TypeInfo *find(std::string_view name) const noexcept;
    [[nodiscard]] std::vector<const TypeInfo *> derivedFrom(const TypeInfo &base) const;

    [[nodiscard]] std::expected<void, RegistryError> registerModule(ModuleId owner,
                                                                    std::span<const TypeInfo *const> types);
    void unregisterModule(ModuleId owner) noexcept;
    void clear() noexcept;

  private:
    std::unordered_map<TypeId, const TypeInfo *> m_types;
};

namespace detail {

template <typename T> consteval PropertyKind propertyKind() {
    if constexpr (std::is_same_v<T, bool>) {
        return PropertyKind::Boolean;
    } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T> &&
                         requires(const char *p, T &value) { std::from_chars(p, p, value); }) {
        return PropertyKind::SignedInteger;
    } else if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T> &&
                         requires(const char *p, T &value) { std::from_chars(p, p, value); }) {
        return PropertyKind::UnsignedInteger;
    } else if constexpr (std::is_floating_point_v<T>) {
        return PropertyKind::FloatingPoint;
    } else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, Guid> || std::is_same_v<T, Name> ||
                         std::is_same_v<T, std::filesystem::path>) {
        return PropertyKind::String;
    } else {
        return PropertyKind::Reflected;
    }
}

template <typename Owner, typename Member, Member Owner::*MemberPointer> void *getMutable(void *object) {
    return &(static_cast<Owner *>(object)->*MemberPointer);
}

template <typename Owner, typename Member, Member Owner::*MemberPointer> const void *getConst(const void *object) {
    return &(static_cast<const Owner *>(object)->*MemberPointer);
}

template <typename T> void construct(void *object) { std::construct_at(static_cast<T *>(object)); }
template <typename T> void destroy(void *object) { std::destroy_at(static_cast<T *>(object)); }
template <typename T> void copyConstruct(void *destination, const void *source) {
    std::construct_at(static_cast<T *>(destination), *static_cast<const T *>(source));
}
template <typename T> bool equals(const void *left, const void *right) {
    if constexpr (requires(const T &a, const T &b) { a == b; }) {
        return *static_cast<const T *>(left) == *static_cast<const T *>(right);
    } else {
        return false;
    }
}

template <typename T> std::string formatScalar(const void *member) {
    const auto &value = *static_cast<const T *>(member);
    if constexpr (std::is_same_v<T, std::string>) {
        return value;
    } else if constexpr (std::is_same_v<T, Guid>) {
        return value.toString();
    } else if constexpr (std::is_same_v<T, Name>) {
        return std::string(value.getString());
    } else if constexpr (std::is_same_v<T, std::filesystem::path>) {
        const auto text = value.generic_u8string();
        return std::string(reinterpret_cast<const char *>(text.data()), text.size());
    } else if constexpr (std::is_same_v<T, bool>) {
        return value ? "true" : "false";
    } else {
        if constexpr (std::is_floating_point_v<T>) {
            if (std::isnan(value)) {
                return ".nan";
            }
            if (std::isinf(value)) {
                return std::signbit(value) ? "-.inf" : ".inf";
            }
        }
        char buffer[128];
        const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
        return std::string(buffer, converted.ptr);
    }
}

template <typename T> bool parseScalar(std::string_view text, void *member) {
    auto &destination = *static_cast<T *>(member);
    if constexpr (std::is_same_v<T, std::string>) {
        destination = text;
    } else if constexpr (std::is_same_v<T, Guid>) {
        const auto parsed = Guid::parse(text);
        if (!parsed) {
            return false;
        }
        destination = *parsed;
    } else if constexpr (std::is_same_v<T, Name>) {
        destination = Name(text);
    } else if constexpr (std::is_same_v<T, std::filesystem::path>) {
        std::u8string bytes(text.begin(), text.end());
        destination = std::filesystem::path(bytes, std::filesystem::path::generic_format);
    } else if constexpr (std::is_same_v<T, bool>) {
        if (text == "true" || text == "True" || text == "TRUE") {
            destination = true;
        } else if (text == "false" || text == "False" || text == "FALSE") {
            destination = false;
        } else {
            return false;
        }
    } else {
        if constexpr (std::is_floating_point_v<T>) {
            if (text == ".nan" || text == ".NaN" || text == ".NAN") {
                destination = std::numeric_limits<T>::quiet_NaN();
                return true;
            }
            if (text == ".inf" || text == ".Inf" || text == ".INF") {
                destination = std::numeric_limits<T>::infinity();
                return true;
            }
            if (text == "-.inf" || text == "-.Inf" || text == "-.INF") {
                destination = -std::numeric_limits<T>::infinity();
                return true;
            }
        }
        if (text.starts_with('+')) {
            text.remove_prefix(1);
            if (text.starts_with('-')) {
                return false;
            }
        }
        if (text.empty()) {
            return false;
        }
        T value{};
        const auto converted = [&] {
            if constexpr (std::is_integral_v<T>) {
                int base = 10;
                if (text.starts_with("0x") || text.starts_with("0o")) {
                    base = text[1] == 'x' ? 16 : 8;
                    text.remove_prefix(2);
                }
                return std::from_chars(text.data(), text.data() + text.size(), value, base);
            } else {
                return std::from_chars(text.data(), text.data() + text.size(), value);
            }
        }();
        if (converted.ec != std::errc{} || converted.ptr != text.data() + text.size()) {
            return false;
        }
        destination = value;
    }
    return true;
}

template <typename T> void swapObjects(void *left, void *right) noexcept {
    using std::swap;
    swap(*static_cast<T *>(left), *static_cast<T *>(right));
}

} // namespace detail

template <typename T> constexpr PropertyInfo makeValueInfo(const TypeInfo *nested_type = nullptr);

namespace detail {

STAR_REFLECTION_API const TypeInfo *vector2Type();
STAR_REFLECTION_API const TypeInfo *vector3Type();
STAR_REFLECTION_API const TypeInfo *quaternionType();

template <typename T> constexpr const ContainerOps *containerOps() {
    if constexpr (requires(T &container, const T &source, std::size_t index) {
                      requires std::is_same_v<decltype(source[index]), const typename T::value_type &>;
                      requires std::is_same_v<decltype(container[index]), typename T::value_type &>;
                      { source.size() } -> std::same_as<std::size_t>;
                  } && (requires(T &container, std::size_t count) { container.resize(count); } ||
                        requires { typename std::tuple_size<T>::type; })) {
        using Element = typename T::value_type;
        static constexpr ContainerOps ops{
            false,
            &makeValueInfo<Element>,
            [](const void *container) { return static_cast<const T *>(container)->size(); },
            [](const void *container, void *context, auto entry) {
                for (const auto &item : *static_cast<const T *>(container)) {
                    entry(context, {}, &item);
                }
            },
            [](void *container, std::size_t count, void *context, auto fill) {
                T replacement{};
                if constexpr (requires { replacement.resize(count); }) {
                    replacement.resize(count);
                } else if (count != replacement.size()) {
                    return false;
                }
                if (!fill(&replacement, context)) {
                    return false;
                }
                *static_cast<T *>(container) = std::move(replacement);
                return true;
            },
            [](void *container, std::size_t index, std::string_view) -> void * {
                return &(*static_cast<T *>(container))[index];
            }};
        return &ops;
    } else if constexpr (requires(T &container, std::string key) {
                             requires std::is_same_v<typename T::key_type, std::string>;
                             typename T::mapped_type;
                             container.try_emplace(std::move(key));
                         }) {
        using Element = typename T::mapped_type;
        static constexpr ContainerOps ops{
            true,
            &makeValueInfo<Element>,
            [](const void *container) { return static_cast<const T *>(container)->size(); },
            [](const void *container, void *context, auto entry) {
                for (const auto &[key, item] : *static_cast<const T *>(container)) {
                    entry(context, key, &item);
                }
            },
            [](void *container, std::size_t, void *context, auto fill) {
                T replacement{};
                if (!fill(&replacement, context)) {
                    return false;
                }
                *static_cast<T *>(container) = std::move(replacement);
                return true;
            },
            [](void *container, std::size_t, std::string_view key) -> void * {
                return &static_cast<T *>(container)->try_emplace(std::string(key)).first->second;
            }};
        return &ops;
    }
    return nullptr;
}

} // namespace detail

template <typename T> constexpr PropertyInfo makeValueInfo(const TypeInfo *nested_type) {
    constexpr auto kind = detail::propertyKind<T>();
    PropertyInfo property{"", kind, PropertyFlags::None, nested_type, nullptr, nullptr};
    if constexpr (kind != PropertyKind::Reflected) {
        property.format_scalar = &detail::formatScalar<T>;
        property.parse_scalar = &detail::parseScalar<T>;
    } else {
        property.container = detail::containerOps<T>();
        if (nested_type == nullptr) {
            if constexpr (std::is_same_v<T, math::Vector2>) {
                property.resolve_nested_type = &detail::vector2Type;
            } else if constexpr (std::is_same_v<T, math::Vector3>) {
                property.resolve_nested_type = &detail::vector3Type;
            } else if constexpr (std::is_same_v<T, math::Quaternion>) {
                property.resolve_nested_type = &detail::quaternionType;
            }
        }
    }
    return property;
}

template <typename Owner, typename Member, Member Owner::*MemberPointer>
constexpr PropertyInfo makeProperty(std::string_view name, PropertyFlags flags = PropertyFlags::None,
                                    const TypeInfo *nested_type = nullptr, std::string_view former_name = {}) {
    auto property = makeValueInfo<Member>(nested_type);
    property.name = name;
    property.flags = flags;
    property.get_mutable = &detail::getMutable<Owner, Member, MemberPointer>;
    property.get_const = &detail::getConst<Owner, Member, MemberPointer>;
    property.former_name = former_name;
    return property;
}

template <typename T> constexpr TypeOps makeTypeOps() {
    static_assert(std::is_default_constructible_v<T> && std::is_destructible_v<T> && std::is_copy_constructible_v<T>);
    TypeOps ops{&detail::construct<T>, &detail::destroy<T>, &detail::copyConstruct<T>, &detail::equals<T>};
    if constexpr (std::is_nothrow_swappable_v<T>) {
        ops.swap = &detail::swapObjects<T>;
    }
    return ops;
}

template <typename E> constexpr EnumOps makeEnumOps() {
    return EnumOps{[](const void *object) { return static_cast<std::uint64_t>(*static_cast<const E *>(object)); },
                   [](void *object, std::uint64_t value) {
                       *static_cast<E *>(object) = static_cast<E>(static_cast<std::underlying_type_t<E>>(value));
                   }};
}

template <typename Derived, typename Base> constexpr BaseAccess makeBaseAccess() {
    return BaseAccess{[](const void *object) -> const void * {
                          return static_cast<const Base *>(static_cast<const Derived *>(object));
                      },
                      [](void *object) -> void * { return static_cast<Base *>(static_cast<Derived *>(object)); }};
}

} // namespace star::reflect
