#include "reflection/reflection.hpp"

#include <array>

namespace star::reflect {

namespace {

template <typename T>
TypeInfo makeBuiltin(std::string_view name, std::span<const PropertyInfo> fields = {},
                     TypeKind kind = TypeKind::Primitive) {
    return TypeInfo{makeTypeId(name), name,           kind, sizeof(T), alignof(T), fields,
                    makeTypeOps<T>(), builtin_module, 1,    nullptr};
}

constexpr PropertyInfo vector2_fields[] = {
    makeProperty<math::Vector2, float, &math::Vector2::x>("x"),
    makeProperty<math::Vector2, float, &math::Vector2::y>("y"),
};

constexpr PropertyInfo vector3_fields[] = {
    makeProperty<math::Vector3, float, &math::Vector3::x>("x"),
    makeProperty<math::Vector3, float, &math::Vector3::y>("y"),
    makeProperty<math::Vector3, float, &math::Vector3::z>("z"),
};

constexpr PropertyInfo quaternion_fields[] = {
    makeProperty<math::Quaternion, float, &math::Quaternion::x>("x"),
    makeProperty<math::Quaternion, float, &math::Quaternion::y>("y"),
    makeProperty<math::Quaternion, float, &math::Quaternion::z>("z"),
    makeProperty<math::Quaternion, float, &math::Quaternion::w>("w"),
};

} // namespace

const TypeInfo *detail::vector2Type() {
    static const auto type = makeBuiltin<math::Vector2>("star::math::Vector2", vector2_fields, TypeKind::Struct);
    return &type;
}

const TypeInfo *detail::vector3Type() {
    static const auto type = makeBuiltin<math::Vector3>("star::math::Vector3", vector3_fields, TypeKind::Struct);
    return &type;
}

const TypeInfo *detail::quaternionType() {
    static const auto type =
        makeBuiltin<math::Quaternion>("star::math::Quaternion", quaternion_fields, TypeKind::Struct);
    return &type;
}

std::span<const TypeInfo *const> builtinTypes() {
    static const std::array primitives{
        makeBuiltin<bool>("bool"),
        makeBuiltin<std::int8_t>("int8_t"),
        makeBuiltin<std::uint8_t>("uint8_t"),
        makeBuiltin<std::int16_t>("int16_t"),
        makeBuiltin<std::uint16_t>("uint16_t"),
        makeBuiltin<std::int32_t>("int32_t"),
        makeBuiltin<std::uint32_t>("uint32_t"),
        makeBuiltin<std::int64_t>("int64_t"),
        makeBuiltin<std::uint64_t>("uint64_t"),
        makeBuiltin<float>("float"),
        makeBuiltin<double>("double"),
        makeBuiltin<std::string>("std::string"),
        makeBuiltin<Guid>("star::Guid"),
        makeBuiltin<Name>("star::Name"),
        makeBuiltin<std::filesystem::path>("std::filesystem::path"),
    };
    static const auto pointers = [] {
        std::array<const TypeInfo *, primitives.size() + 3> types{};
        for (std::size_t index = 0; index < primitives.size(); ++index) {
            types[index] = &primitives[index];
        }
        types[primitives.size()] = detail::vector2Type();
        types[primitives.size() + 1] = detail::vector3Type();
        types[primitives.size() + 2] = detail::quaternionType();
        return types;
    }();
    return pointers;
}

} // namespace star::reflect
