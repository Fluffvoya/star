#include "core/assert.hpp"

#include <cstdlib>

namespace star::detail {

void failAssertion(std::string_view expression, std::string_view file, unsigned int line, std::string_view message) {
    log::error(LogCategory("Assert"), "{} ({}:{}): {}", expression, file, line, message);
    std::abort();
}

} // namespace star::detail
