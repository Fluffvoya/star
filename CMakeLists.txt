cmake_minimum_required(VERSION 3.28)

project(
    StarEngine
    VERSION 0.1.0
    DESCRIPTION "Star Engine"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Development CACHE STRING "Build configuration" FORCE)
endif()

set(CMAKE_CONFIGURATION_TYPES "Debug;Development;Profile;Shipping" CACHE STRING "Supported configurations" FORCE)

option(STAR_BUILD_TESTS "Build local tests" ON)
option(STAR_BUILD_SAMPLES "Build sample programs" ON)
option(STAR_MONOLITHIC "Link engine modules into a static monolithic runtime" OFF)
option(STAR_SHIPPING_DEPS "Use shipping dependency policy" OFF)

if(STAR_BUILD_TESTS)
    include(CTest)
endif()

if(STAR_MONOLITHIC)
    set(STAR_MODULE_LIBRARY_TYPE STATIC)
else()
    set(STAR_MODULE_LIBRARY_TYPE SHARED)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerOptions)
include(Dependencies)
include(StarModule)

set(STAR_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${STAR_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Lib")

add_subdirectory(Engine/Source/Runtime/Core)
add_subdirectory(Engine/Source/Runtime/Jobs)
add_subdirectory(Engine/Source/Runtime/Platform)

if(STAR_BUILD_SAMPLES)
    add_subdirectory(Samples/HelloWindow)
endif()

if(STAR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
