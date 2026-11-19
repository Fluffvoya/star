star_add_module(
    Platform
    TYPE RUNTIME
    DEPENDS_PRIVATE SDL3::SDL3
)

target_sources(
    StarPlatform
    PRIVATE
        public/platform/clock.hpp
        public/platform/platform.hpp
        public/platform/window.hpp
)
