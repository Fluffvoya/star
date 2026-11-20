#pragma once

#include "serialization/archive.hpp"

namespace star::serialization {

inline constexpr unsigned max_archive_depth = 64;

using ValueResult = std::expected<ArchiveValue, ArchiveError>;

bool isPlainScalar(std::string_view text);
ArchiveValue documentValue(const ArchiveDocument &document);
std::expected<ArchiveDocument, ArchiveError> parseDocument(ArchiveValue root);
bool validateValue(const ArchiveValue &value, unsigned depth = 0);

} // namespace star::serialization
