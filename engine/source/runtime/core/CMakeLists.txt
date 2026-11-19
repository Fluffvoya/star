star_add_module(
    Core
    TYPE RUNTIME
)

target_sources(
    StarCore
    PRIVATE
        public/core/assert.hpp
        public/core/core.hpp
        public/core/guid.hpp
        public/core/log.hpp
        public/core/name.hpp
)
