#include "Core/Assert.hpp"

#include <cstdlib>

namespace star::detail {

void FailAssertion(std::string_view expression, std::string_view file, unsigned int line, std::string_view message) {
    log::Error(LogCategory("Assert"), "{} ({}:{}): {}", expression, file, line, message);
    std::abort();
}

} // namespace star::detail
