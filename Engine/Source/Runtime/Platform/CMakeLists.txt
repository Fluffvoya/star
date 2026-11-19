star_add_module(
    Platform
    TYPE RUNTIME
    DEPENDS_PRIVATE SDL3::SDL3
)

target_sources(
    StarPlatform
    PRIVATE
        Public/Platform/Clock.hpp
        Public/Platform/Platform.hpp
        Public/Platform/Window.hpp
)
