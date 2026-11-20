if(STAR_SHIPPING_DEPS)
    message(STATUS "Star: supply shipping dependencies through the configured toolchain")
endif()

find_package(SDL3 3.2 REQUIRED CONFIG)

star_add_module(Platform platform
    include/platform/clock.hpp
    include/platform/dynamic_library.hpp
    include/platform/platform.hpp
    include/platform/window.hpp
    src/clock.cpp
    src/dynamic_library.cpp
    src/platform.cpp
    src/window.cpp
)

target_link_libraries(StarPlatform PRIVATE SDL3::SDL3 ${CMAKE_DL_LIBS})
