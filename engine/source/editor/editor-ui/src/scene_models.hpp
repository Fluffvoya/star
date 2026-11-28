#pragma once

#include "core/guid.hpp"
#include "core/math.hpp"
#include "editor-ui/imgui_layer.hpp"

#include <filesystem>
#include <memory>
#include <span>

namespace star::editor {

struct SceneModelInstance {
    Guid model;
    math::Mat4 clip_from_model;
};

class SceneModels final {
  public:
    SceneModels(rhi::Device &device, const std::filesystem::path &shader_directory);
    ~SceneModels();
    SceneModels(const SceneModels &) = delete;
    SceneModels &operator=(const SceneModels &) = delete;

    [[nodiscard]] bool isReady() const noexcept;
    void setProjectRoot(const std::filesystem::path &root);
    void refresh();
    void draw(rhi::CommandList &commands, std::span<const SceneModelInstance> instances);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace star::editor
