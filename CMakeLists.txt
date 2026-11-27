cmake_minimum_required(VERSION 3.28)
project(StarEngine VERSION 0.1.0 DESCRIPTION "Star Engine" LANGUAGES CXX)

option(STAR_BUILD_SAMPLES "Build sample programs" ON)
option(STAR_BUILD_TESTS "Build local tests" ON)
option(STAR_TEST_WINDOWS_VULKAN "Enable Windows Vulkan acceptance tests requiring a GPU and desktop" OFF)
option(STAR_BUILD_HEADER_TOOL "Build StarHeaderTool" ON)
option(STAR_MONOLITHIC "Build engine modules as static libraries" OFF)
option(STAR_SHIPPING_DEPS "Use shipping dependency policy" OFF)

if(STAR_SHIPPING_DEPS AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    message(STATUS "Star: shipping dependency policy requested without a vcpkg toolchain; using discovered packages")
endif()

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

set(star_entt_dir "${CMAKE_CURRENT_SOURCE_DIR}/engine/source/third-party/entt")
if(NOT EXISTS "${star_entt_dir}/CMakeLists.txt")
    message(FATAL_ERROR "Initialize the pinned EnTT dependency with git submodule update --init --recursive.")
endif()
add_subdirectory("${star_entt_dir}" EXCLUDE_FROM_ALL SYSTEM)

set(star_rapidyaml_dir "${CMAKE_CURRENT_SOURCE_DIR}/engine/source/third-party/rapidyaml")
if(NOT EXISTS "${star_rapidyaml_dir}/ext/c4core/cmake/c4Project.cmake")
    message(FATAL_ERROR "Initialize the pinned YAML dependency with git submodule update --init --recursive.")
endif()
block(SCOPE_FOR VARIABLES)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(BUILD_SHARED_LIBS OFF)
    set(RYML_INSTALL OFF)
    set(RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS ON)
    add_subdirectory("${star_rapidyaml_dir}" EXCLUDE_FROM_ALL)
endblock()
set_target_properties(ryml PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)

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

if(STAR_TEST_WINDOWS_VULKAN AND (NOT WIN32 OR NOT STAR_BUILD_TESTS OR NOT BUILD_TESTING OR NOT STAR_BUILD_SAMPLES))
    message(FATAL_ERROR "STAR_TEST_WINDOWS_VULKAN requires Windows with tests and samples enabled")
endif()

include(cmake/StarModule.cmake)
include(cmake/StarReflection.cmake)
include(cmake/Dependencies.cmake)
add_subdirectory(engine/source/runtime/core)
add_subdirectory(engine/source/runtime/reflection)
add_subdirectory(engine/source/runtime/serialization)
add_subdirectory(engine/source/runtime/scene)
add_subdirectory(engine/source/runtime/rhi)
add_subdirectory(engine/source/runtime/jobs)
add_subdirectory(engine/source/runtime/asset)
add_subdirectory(engine/source/runtime/platform)
add_subdirectory(engine/source/editor/asset-pipeline)

if(STAR_BUILD_HEADER_TOOL)
    add_subdirectory(tools/star-header-tool)
endif()

if(STAR_BUILD_SAMPLES)
    add_subdirectory(samples/hello-window)
    add_subdirectory(samples/vulkan-mesh)
endif()

if(STAR_BUILD_TESTS AND BUILD_TESTING)
    add_subdirectory(tests)
endif()
