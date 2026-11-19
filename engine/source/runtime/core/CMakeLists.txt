star_add_module(Core core
    include/core/allocation_error.hpp
    include/core/assert.hpp
    include/core/config.hpp
    include/core/containers.hpp
    include/core/core.hpp
    include/core/cvar.hpp
    include/core/delegate.hpp
    include/core/guid.hpp
    include/core/linear_allocator.hpp
    include/core/log.hpp
    include/core/math.hpp
    include/core/name.hpp
    include/core/pool_allocator.hpp
    include/core/slot_map.hpp
    include/core/stack_allocator.hpp
    include/core/tracked_allocator.hpp
    src/arena_allocators.cpp
    src/assert.cpp
    src/config.cpp
    src/cvar.cpp
    src/guid.cpp
    src/log.cpp
    src/name.cpp
    src/tracked_allocator.cpp
)

target_link_libraries(StarCore PRIVATE ryml)
