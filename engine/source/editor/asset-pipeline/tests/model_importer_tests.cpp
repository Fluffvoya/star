#include "asset-pipeline/model_importer.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <span>
#include <string_view>

namespace {

using namespace star::asset_pipeline;

std::span<const std::byte> asBytes(std::string_view source) { return std::as_bytes(std::span(source)); }

constexpr std::string_view obj = R"(
o Quad
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
vt 0 0
vt 1 0
vt 1 1
vt 0 1
f 1/1 2/2 3/3 4/4
)";

constexpr std::string_view fbx = R"(
; FBX 7.4.0 project file
FBXHeaderExtension: {
    FBXHeaderVersion: 1003
    FBXVersion: 7400
}
GlobalSettings: {
    Version: 1000
    Properties70: {
        P: "UpAxis", "int", "Integer", "",1
        P: "UpAxisSign", "int", "Integer", "",1
        P: "FrontAxis", "int", "Integer", "",2
        P: "FrontAxisSign", "int", "Integer", "",1
        P: "CoordAxis", "int", "Integer", "",0
        P: "CoordAxisSign", "int", "Integer", "",1
        P: "UnitScaleFactor", "double", "Number", "",1
    }
}
Objects: {
    Geometry: 100, "Geometry::Triangle", "Mesh" {
        Vertices: *9 {
            a: 0,0,0,100,0,0,0,100,0
        }
        PolygonVertexIndex: *3 {
            a: 0,1,-3
        }
    }
    Model: 200, "Model::Triangle", "Mesh" {
        Version: 232
        Properties70: {
            P: "Lcl Translation", "Lcl Translation", "", "A",200,0,0
        }
    }
}
Connections: {
    C: "OO",100,200
    C: "OO",200,0
}
)";

} // namespace

STAR_TEST(model_importer_triangulates_obj_and_generates_normals) {
    const auto model = importStaticModel(asBytes(obj), StaticModelFormat::Obj);
    STAR_CHECK(model.has_value());
    STAR_CHECK(model->parts.size() == 1);
    const auto &part = model->parts.front();
    STAR_CHECK(part.vertices.size() == 4);
    STAR_CHECK(part.indices.size() == 6);
    for (const auto &vertex : part.vertices) {
        STAR_CHECK(vertex.normal.z > 0.99F);
        STAR_CHECK(std::abs(vertex.uv.y - (1.0F - vertex.position.y)) < 0.001F);
    }
}

STAR_TEST(model_importer_converts_fbx_units_and_bakes_translation) {
    const auto model = importStaticModel(asBytes(fbx), StaticModelFormat::Fbx);
    STAR_CHECK(model.has_value());
    STAR_CHECK(model->parts.size() == 1);
    const auto &part = model->parts.front();
    STAR_CHECK(part.indices.size() == 3);
    float maximum_x = 0.0F;
    for (const auto &vertex : part.vertices) {
        STAR_CHECK(vertex.position.x >= 1.999F);
        maximum_x = std::max(maximum_x, vertex.position.x);
    }
    STAR_CHECK(std::abs(maximum_x - 3.0F) < 0.001F);
}

STAR_TEST(model_importer_rejects_missing_geometry) {
    STAR_CHECK(!importStaticModel(asBytes("o Empty\n"), StaticModelFormat::Obj).has_value());
    STAR_CHECK(!importStaticModel(asBytes("Invalid FBX"), StaticModelFormat::Fbx).has_value());
}

namespace {

constexpr std::string_view gltf_prefix = R"(
{
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"translation": [2, 0, 0], "children": [1]}, {"mesh": 0, "scale": [-1, 1, 1]}],
  "meshes": [{"name": "Triangle", "primitives": [{"attributes": {"POSITION": 0}, "material": 0}]}],
  "materials": [{"name": "Paint", "pbrMetallicRoughness": {
      "baseColorFactor": [1.0, 0.25, 0.125, 1.0], "metallicFactor": 0.4, "roughnessFactor": 0.8}}],
  "bufferViews": [{"buffer": 0, "byteLength": 36}],
  "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
                 "min": [0, 0, 0], "max": [1, 1, 0]}],
  "buffers": [{"byteLength": 36)";

void appendUInt32(std::vector<std::byte> &buffer, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        buffer.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    }
}

std::vector<std::byte> triangleBytes() {
    std::vector<std::byte> data;
    for (const float component : {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F}) {
        appendUInt32(data, std::bit_cast<std::uint32_t>(component));
    }
    return data;
}

} // namespace

STAR_TEST(model_importer_reads_glb_and_bakes_parent_and_mirrored_transforms) {
    auto json = std::string(gltf_prefix) + "}]}";
    while (json.size() % 4 != 0) {
        json.push_back(' ');
    }
    const auto triangle = triangleBytes();
    std::vector<std::byte> glb;
    appendUInt32(glb, 0x46546c67U); // glTF.
    appendUInt32(glb, 2U);
    appendUInt32(glb, static_cast<std::uint32_t>(28 + json.size() + triangle.size()));
    appendUInt32(glb, static_cast<std::uint32_t>(json.size()));
    appendUInt32(glb, 0x4e4f534aU); // JSON.
    const auto json_bytes = asBytes(json);
    glb.insert(glb.end(), json_bytes.begin(), json_bytes.end());
    appendUInt32(glb, static_cast<std::uint32_t>(triangle.size()));
    appendUInt32(glb, 0x004e4942U); // BIN.
    glb.insert(glb.end(), triangle.begin(), triangle.end());

    const auto model = importGltfStaticModel(glb);
    STAR_CHECK(model.has_value());
    STAR_CHECK(model->parts.size() == 1);
    const auto &mesh = model->parts.front();
    STAR_CHECK(mesh.vertices.size() == 3);
    STAR_CHECK(mesh.vertices[0].position.x == 2.0F);
    STAR_CHECK(mesh.vertices[1].position.x == 1.0F);
    STAR_CHECK(mesh.vertices[0].normal.z > 0.99F);
    STAR_CHECK(mesh.indices == std::vector<std::uint32_t>({0, 2, 1}));
    STAR_CHECK(mesh.material_name == "Paint");
    STAR_CHECK(mesh.material.has_value());
    STAR_CHECK(mesh.material->base_color[1] == 0.25F);
    STAR_CHECK(mesh.material->metallic == 0.4F);
    STAR_CHECK(mesh.material->roughness == 0.8F);
}

STAR_TEST(model_importer_resolves_external_gltf_buffers_and_checks_bounds) {
    const auto json = std::string(gltf_prefix) + R"(, "uri": "buffers/triangle.bin"}]})";
    const auto source = asBytes(json);
    STAR_CHECK(!importGltfStaticModel(source).has_value());
    const auto model = importGltfStaticModel(source, [](std::string_view path) {
        STAR_CHECK(path == "buffers/triangle.bin");
        return triangleBytes();
    });
    STAR_CHECK(model.has_value());

    const auto truncated = importGltfStaticModel(source, [](std::string_view) { return std::vector<std::byte>(35); });
    STAR_CHECK(!truncated.has_value());
    const auto traversal_json = std::string(gltf_prefix) + R"(, "uri": "../triangle.bin"}]})";
    const auto traversal = importGltfStaticModel(asBytes(traversal_json), [](std::string_view) {
        STAR_CHECK(false);
        return triangleBytes();
    });
    STAR_CHECK(!traversal.has_value());
}
