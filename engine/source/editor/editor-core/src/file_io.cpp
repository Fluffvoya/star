#include "file_io.hpp"

#include <fstream>

namespace star::editor {

std::expected<void, EditorError> writeEditorFile(const std::filesystem::path &path, std::string_view text) {
    std::error_code error;
    if (const auto parent = path.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, error);
    }
    const auto staging = path.parent_path() / (".star-save-" + Guid::generate().toString());
    if (error || !std::filesystem::create_directory(staging, error)) {
        return std::unexpected(EditorError{EditorErrorCode::FileWriteFailed, "Cannot stage save for " + path.string()});
    }
    const auto temporary = staging / "scene";
    std::ofstream file(temporary, std::ios::binary);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    const bool written = file.good();
    if (written) {
        std::filesystem::rename(temporary, path, error);
    }
    std::error_code cleanup_error;
    std::filesystem::remove(temporary, cleanup_error);
    std::filesystem::remove(staging, cleanup_error);
    if (!written || error) {
        return std::unexpected(EditorError{EditorErrorCode::FileWriteFailed, "Write failed for " + path.string()});
    }
    return {};
}

} // namespace star::editor
