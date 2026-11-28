#pragma once

#include "editor/document.hpp"

#include <string_view>

namespace star::editor {

[[nodiscard]] std::expected<void, EditorError> writeEditorFile(const std::filesystem::path &path,
                                                               std::string_view text);

} // namespace star::editor
