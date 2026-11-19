cmake_minimum_required(VERSION 3.28)
project(StarEngine VERSION 0.1.0 DESCRIPTION "Star Engine" LANGUAGES CXX)

option(STAR_BUILD_SAMPLES "Build sample programs" ON)
option(STAR_BUILD_TESTS "Build local tests" ON)
option(STAR_MONOLITHIC "Build engine modules as static libraries" OFF)
option(STAR_SHIPPING_DEPS "Use shipping dependency policy" OFF)

get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(is_multi_config)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Development;Profile;Shipping" CACHE STRING "Supported configurations" FORCE)
elseif(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Development CACHE STRING "Build configuration" FORCE)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

add_compile_definitions(
    $<$<CONFIG:Debug>:STAR_DEBUG=1>
    $<$<CONFIG:Development>:STAR_DEVELOPMENT=1>
    $<$<CONFIG:Profile>:STAR_PROFILE=1>
    $<$<CONFIG:Shipping>:STAR_SHIPPING=1>
)

if(MSVC)
    add_compile_options(
        /W4 /permissive- /utf-8 /Zc:__cplusplus
        $<$<CONFIG:Debug>:/Od>
        $<$<CONFIG:Development,Profile,Shipping>:/O2>
        $<$<NOT:$<CONFIG:Debug>>:/Oi>
    )
else()
    add_compile_options(
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnon-virtual-dtor
        $<$<CONFIG:Debug>:-O0>
        $<$<CONFIG:Development,Profile>:-O2>
        $<$<CONFIG:Debug,Development,Profile>:-g>
        $<$<CONFIG:Shipping>:-O3>
    )
endif()

if(STAR_BUILD_TESTS)
    include(CTest)
endif()

include(cmake/StarModule.cmake)
add_subdirectory(engine/source/runtime/core)
add_subdirectory(engine/source/runtime/jobs)
add_subdirectory(engine/source/runtime/platform)

if(STAR_BUILD_SAMPLES)
    add_subdirectory(samples/hello-window)
endif()

if(STAR_BUILD_TESTS AND BUILD_TESTING)
    add_subdirectory(tests)
endif()
