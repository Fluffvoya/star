#include "asset/material.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace star {
namespace {

using serialization::ArchiveDocument;
using serialization::ArchiveError;
using serialization::ArchiveErrorCode;
using serialization::ArchiveValue;
using serialization::LoadPolicy;
using serialization::PlainScalar;
using Mapping = ArchiveValue::Mapping;

constexpr std::string_view material_type = "star::Material";
constexpr std::string_view instance_type = "star::MaterialInstance";
constexpr std::array<std::string_view, 3> blend_modes{"Opaque", "Masked", "Transparent"};
constexpr std::array<std::string_view, 2> shading_models{"Lit", "Unlit"};
constexpr std::array<std::string_view, 2> booleans{"false", "true"};

std::unexpected<ArchiveError> error(ArchiveErrorCode code, std::string path) {
    return std::unexpected(ArchiveError{code, std::move(path)});
}

const ArchiveValue *find(const Mapping &fields, std::string_view key) {
    const auto it = fields.find(key);
    return it == fields.end() ? nullptr : &it->second;
}

std::string_view scalar(const ArchiveValue *field) {
    if (!field) {
        return {};
    }
    if (const auto *text = std::get_if<std::string>(&field->value)) {
        return *text;
    }
    if (const auto *plain = std::get_if<PlainScalar>(&field->value)) {
        return plain->text;
    }
    return {};
}

Guid readGuid(Mapping &fields, std::string_view key) {
    const auto guid = Guid::parse(scalar(find(fields, key))).value_or(Guid{});
    fields.erase(std::string(key));
    return guid;
}

std::expected<void, ArchiveError> checkDocument(const ArchiveDocument &document, std::string_view type) {
    if (document.type_name != type) {
        return error(ArchiveErrorCode::TypeMismatch, "Type");
    }
    if (document.version != 1) {
        return error(ArchiveErrorCode::UnsupportedVersion, "Version");
    }
    return {};
}

std::expected<Mapping, ArchiveError> takeMapping(Mapping &fields, std::string_view key) {
    const auto it = fields.find(key);
    if (it == fields.end()) {
        return Mapping{};
    }
    auto *mapping = std::get_if<Mapping>(&it->second.value);
    if (!mapping) {
        return error(ArchiveErrorCode::InvalidInput, std::string(key));
    }
    auto value = std::move(*mapping);
    fields.erase(it);
    return value;
}

template <typename T, std::size_t N>
bool takeState(Mapping &fields, std::string_view key, const std::array<std::string_view, N> &names, T &value) {
    const auto it = fields.find(key);
    if (it == fields.end()) {
        return true;
    }
    const auto text = scalar(&it->second);
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i] == text) {
            value = static_cast<T>(i);
            fields.erase(it);
            return true;
        }
    }
    return false;
}

std::optional<float> number(const ArchiveValue &value) {
    const auto *plain = std::get_if<PlainScalar>(&value.value);
    if (plain == nullptr) {
        return std::nullopt;
    }
    float result = 0.0F;
    const auto parsed = std::from_chars(plain->text.data(), plain->text.data() + plain->text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != plain->text.data() + plain->text.size() || !std::isfinite(result)) {
        return std::nullopt;
    }
    return result;
}

std::optional<Guid> takeGuid(const Mapping &fields, std::string_view key, std::string_view path,
                             std::expected<void, ArchiveError> &status) {
    const auto *field = find(fields, key);
    if (field == nullptr) {
        return std::nullopt;
    }
    const auto text = scalar(field);
    const auto guid = Guid::parse(text);
    if (!guid || !guid->isValid()) {
        status = error(ArchiveErrorCode::InvalidInput, std::string(path) + "." + std::string(key));
        return std::nullopt;
    }
    return *guid;
}

std::optional<float> takeNumber(const Mapping &fields, std::string_view key, std::string_view path,
                                std::expected<void, ArchiveError> &status) {
    const auto *field = find(fields, key);
    if (field == nullptr) {
        return std::nullopt;
    }
    const auto value = number(*field);
    if (!value) {
        status = error(ArchiveErrorCode::InvalidInput, std::string(path) + "." + std::string(key));
    }
    return value;
}

} // namespace

std::expected<MaterialAsset, ArchiveError> readMaterial(const ArchiveDocument &document, LoadPolicy policy) {
    if (auto valid = checkDocument(document, material_type); !valid) {
        return std::unexpected(valid.error());
    }
    MaterialAsset material;
    material.extensions = document.fields;
    material.shader = readGuid(material.extensions, "Shader");
    if (!material.shader.isValid()) {
        return error(ArchiveErrorCode::InvalidInput, "Shader");
    }
    auto parameters = takeMapping(material.extensions, "Parameters");
    if (!parameters) {
        return std::unexpected(parameters.error());
    }
    material.parameters = std::move(*parameters);

    auto state = takeMapping(material.extensions, "RenderState");
    if (!state) {
        return std::unexpected(state.error());
    }
    if (!takeState(*state, "Blend", blend_modes, material.state.blend)) {
        return error(ArchiveErrorCode::InvalidInput, "RenderState.Blend");
    }
    if (!takeState(*state, "Shading", shading_models, material.state.shading)) {
        return error(ArchiveErrorCode::InvalidInput, "RenderState.Shading");
    }
    if (!takeState(*state, "DoubleSided", booleans, material.state.double_sided)) {
        return error(ArchiveErrorCode::InvalidInput, "RenderState.DoubleSided");
    }
    if (!takeState(*state, "DepthTest", booleans, material.state.depth_test)) {
        return error(ArchiveErrorCode::InvalidInput, "RenderState.DepthTest");
    }
    material.state_extensions = std::move(*state);
    if (policy == LoadPolicy::Strict) {
        if (!material.extensions.empty()) {
            return error(ArchiveErrorCode::UnknownField, material.extensions.begin()->first);
        }
        if (!material.state_extensions.empty()) {
            return error(ArchiveErrorCode::UnknownField, "RenderState." + material.state_extensions.begin()->first);
        }
    }
    return material;
}

std::expected<MaterialInstanceAsset, ArchiveError> readMaterialInstance(const ArchiveDocument &document,
                                                                        LoadPolicy policy) {
    if (auto valid = checkDocument(document, instance_type); !valid) {
        return std::unexpected(valid.error());
    }
    MaterialInstanceAsset instance;
    instance.extensions = document.fields;
    instance.parent = readGuid(instance.extensions, "Parent");
    if (!instance.parent.isValid()) {
        return error(ArchiveErrorCode::InvalidInput, "Parent");
    }
    auto overrides = takeMapping(instance.extensions, "Overrides");
    if (!overrides) {
        return std::unexpected(overrides.error());
    }
    instance.overrides = std::move(*overrides);
    if (policy == LoadPolicy::Strict && !instance.extensions.empty()) {
        return error(ArchiveErrorCode::UnknownField, instance.extensions.begin()->first);
    }
    return instance;
}

std::expected<ArchiveDocument, ArchiveError> writeMaterial(const MaterialAsset &material) {
    if (!material.shader.isValid()) {
        return error(ArchiveErrorCode::InvalidInput, "Shader");
    }
    const auto blend = static_cast<std::size_t>(material.state.blend);
    const auto shading = static_cast<std::size_t>(material.state.shading);
    if (blend >= blend_modes.size()) {
        return error(ArchiveErrorCode::InvalidInput, "RenderState.Blend");
    }
    if (shading >= shading_models.size()) {
        return error(ArchiveErrorCode::InvalidInput, "RenderState.Shading");
    }

    Mapping state = material.state_extensions;
    state["Blend"] = ArchiveValue{std::string(blend_modes[blend])};
    state["Shading"] = ArchiveValue{std::string(shading_models[shading])};
    state["DoubleSided"] = ArchiveValue{PlainScalar{std::string(booleans[material.state.double_sided])}};
    state["DepthTest"] = ArchiveValue{PlainScalar{std::string(booleans[material.state.depth_test])}};
    ArchiveDocument document{std::string(material_type), 1, material.extensions};
    document.fields["Shader"] = ArchiveValue{material.shader.toString()};
    document.fields["Parameters"] = ArchiveValue{material.parameters};
    document.fields["RenderState"] = ArchiveValue{std::move(state)};
    return document;
}

std::expected<ArchiveDocument, ArchiveError> writeMaterialInstance(const MaterialInstanceAsset &instance) {
    if (!instance.parent.isValid()) {
        return error(ArchiveErrorCode::InvalidInput, "Parent");
    }
    ArchiveDocument document{std::string(instance_type), 1, instance.extensions};
    document.fields["Parent"] = ArchiveValue{instance.parent.toString()};
    document.fields["Overrides"] = ArchiveValue{instance.overrides};
    return document;
}

std::expected<ResolvedMaterial, ArchiveError> resolveMaterial(const MaterialAsset &material) {
    ResolvedMaterial resolved{material.shader, material.state};
    std::expected<void, ArchiveError> status;
    const auto &parameters = material.parameters;
    if (const auto *field = find(parameters, "BaseColor"); field != nullptr) {
        const auto *channels = std::get_if<ArchiveValue::Sequence>(&field->value);
        if (channels == nullptr || channels->size() != resolved.base_color.size()) {
            return error(ArchiveErrorCode::InvalidInput, "Parameters.BaseColor");
        }
        for (std::size_t index = 0; index < resolved.base_color.size(); ++index) {
            const auto value = number((*channels)[index]);
            if (!value) {
                return error(ArchiveErrorCode::InvalidInput, "Parameters.BaseColor");
            }
            resolved.base_color[index] = *value;
        }
    }
    const auto metallic = takeNumber(parameters, "Metallic", "Parameters", status);
    if (!status) {
        return std::unexpected(status.error());
    }
    if (metallic && (*metallic < 0.0F || *metallic > 1.0F)) {
        return error(ArchiveErrorCode::InvalidInput, "Parameters.Metallic");
    }
    if (metallic) {
        resolved.metallic = *metallic;
    }
    const auto roughness = takeNumber(parameters, "Roughness", "Parameters", status);
    if (!status) {
        return std::unexpected(status.error());
    }
    if (roughness && (*roughness < 0.0F || *roughness > 1.0F)) {
        return error(ArchiveErrorCode::InvalidInput, "Parameters.Roughness");
    }
    if (roughness) {
        resolved.roughness = *roughness;
    }
    resolved.base_color_texture = takeGuid(parameters, "BaseColorTexture", "Parameters", status);
    if (!status) {
        return std::unexpected(status.error());
    }
    resolved.metallic_roughness_texture = takeGuid(parameters, "MetallicRoughnessTexture", "Parameters", status);
    if (!status) {
        return std::unexpected(status.error());
    }
    if (!material.state.depth_test) {
        return error(ArchiveErrorCode::UnsupportedProperty, "RenderState.DepthTest");
    }
    return resolved;
}

std::expected<MaterialAsset, ArchiveError> resolveMaterialInstance(Guid parent_guid, const MaterialAsset &parent,
                                                                   const MaterialInstanceAsset &instance) {
    if (!parent_guid.isValid() || parent_guid != instance.parent) {
        return error(ArchiveErrorCode::InvalidInput, "Parent");
    }
    MaterialAsset material = parent;
    for (const auto &[name, value] : instance.overrides) {
        material.parameters.insert_or_assign(name, value);
    }
    return material;
}

} // namespace star
