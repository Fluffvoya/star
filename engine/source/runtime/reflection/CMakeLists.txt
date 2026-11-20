star_add_module(Reflection reflection
    include/reflection/annotations.hpp
    include/reflection/reflection.hpp
    src/builtin_types.cpp
    src/reflection.cpp
)

target_link_libraries(StarReflection PUBLIC Star::Core)

find_package(xxHash CONFIG QUIET)
if(TARGET xxHash::xxhash)
    target_link_libraries(StarReflection PRIVATE xxHash::xxhash)
else()
    find_path(STAR_XXHASH_INCLUDE_DIR NAMES xxhash.h REQUIRED)
    find_library(STAR_XXHASH_LIBRARY NAMES xxhash REQUIRED)
    target_include_directories(StarReflection PRIVATE "${STAR_XXHASH_INCLUDE_DIR}")
    target_link_libraries(StarReflection PRIVATE "${STAR_XXHASH_LIBRARY}")
endif()
