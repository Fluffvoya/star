#include "builtin_components.hpp"

#include "scene/scene.hpp"

#include <array>

namespace star::scene_detail {
namespace {

template <class T> reflect::TypeInfo describe(std::span<const reflect::PropertyInfo> properties) noexcept {
    return {componentTypeId<T>(), ComponentTraits<T>::name,  reflect::TypeKind::Struct, sizeof(T), alignof(T),
            properties,           reflect::makeTypeOps<T>(), reflect::builtin_module,   1,         nullptr};
}

} // namespace

std::span<const reflect::TypeInfo> builtinComponents() noexcept {
    using reflect::makeProperty;
    static const std::array transform{
        makeProperty<Transform, math::Vec3, &Transform::position>("position"),
        makeProperty<Transform, math::Quat, &Transform::rotation>("rotation"),
        makeProperty<Transform, math::Vec3, &Transform::scale>("scale"),
    };
    static const std::array camera{
        makeProperty<Camera, float, &Camera::vertical_fov>("vertical_fov"),
        makeProperty<Camera, float, &Camera::near_plane>("near_plane"),
        makeProperty<Camera, float, &Camera::far_plane>("far_plane"),
    };
    static const std::array mesh{
        makeProperty<MeshRenderer, Guid, &MeshRenderer::mesh>("mesh"),
        makeProperty<MeshRenderer, std::vector<Guid>, &MeshRenderer::materials>("materials"),
        makeProperty<MeshRenderer, bool, &MeshRenderer::cast_shadows>("cast_shadows"),
        makeProperty<MeshRenderer, std::uint32_t, &MeshRenderer::layer_mask>("layer_mask"),
    };
    static const std::array light{
        makeProperty<Light, math::Vec3, &Light::color>("color"),
        makeProperty<Light, float, &Light::intensity>("intensity"),
        makeProperty<Light, float, &Light::range>("range"),
    };
    static const std::array types{
        describe<Transform>(transform),
        describe<Camera>(camera),
        describe<MeshRenderer>(mesh),
        describe<Light>(light),
    };
    return types;
}

} // namespace star::scene_detail
