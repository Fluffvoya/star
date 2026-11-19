star_add_module(
    Core
    TYPE RUNTIME
)

target_sources(
    StarCore
    PRIVATE
        Public/Core/Assert.hpp
        Public/Core/Core.hpp
        Public/Core/Guid.hpp
        Public/Core/Log.hpp
        Public/Core/Name.hpp
)
