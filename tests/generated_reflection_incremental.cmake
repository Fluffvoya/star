file(MAKE_DIRECTORY "${work}/source")
set(input "${work}/source/input.hpp")
set(dependency "${work}/source/settings.hpp")
set(unrelated "${work}/source/unrelated.cpp")
set(generated "${work}/build/generated/incremental/incremental.gen.cpp")
set(generated_header "${work}/build/generated/incremental/incremental.gen.hpp")
set(depfile "${work}/build/generated/incremental/incremental.d")
set(cache "${work}/build/header-tool-cache/incremental.cache")

file(WRITE "${work}/source/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.28)
project(ReflectionIncremental LANGUAGES CXX)
add_executable(StarHeaderTool IMPORTED)
set_target_properties(StarHeaderTool PROPERTIES IMPORTED_LOCATION "${tool}")
add_library(Star::Reflection INTERFACE IMPORTED)
include("${root}/cmake/StarReflection.cmake")
add_library(reflected STATIC EXCLUDE_FROM_ALL unrelated.cpp)
star_generate_reflection(reflected incremental 47
    HEADERS input.hpp
    INCLUDE_DIRS "${root}/engine/source/runtime/reflection/include" "${CMAKE_CURRENT_SOURCE_DIR}")
add_custom_target(generate DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/generated/incremental/incremental.gen.cpp")
]=])

set(marked_header [=[
#pragma once
#include "reflection/annotations.hpp"
#define SETTINGS_HEADER <settings.hpp>
#include SETTINGS_HEADER

#if REFLECT_SAMPLE && !defined(REFLECTION_CACHE_TEST)
STAR_STRUCT()
#else
struct InactiveBranch {};
#endif
struct IncrementalSample {
    STAR_PROPERTY()
    int value;
};
]=])

file(WRITE "${input}" "${marked_header}")
file(WRITE "${dependency}" "#define REFLECT_SAMPLE 1\n")
file(WRITE "${unrelated}" "void unrelated() {}\n")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${work}/source" -B "${work}/build" -G Ninja
        "-Dtool=${tool}" "-Droot=${root}" "-DCMAKE_CXX_COMPILER=${compiler}"
        "-DCMAKE_MAKE_PROGRAM=${ninja}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE diagnostics
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "${output}\n${diagnostics}")
endif()

function(rebuild expect_generation expect_type)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${work}/build" --target generate --verbose
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE diagnostics
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${output}\n${diagnostics}")
    endif()
    string(FIND "${output}" "--module incremental" ran)
    if((expect_generation STREQUAL "TRUE" AND ran EQUAL -1) OR
        (expect_generation STREQUAL "FALSE" AND NOT ran EQUAL -1))
        message(FATAL_ERROR "Unexpected generator scheduling: ${output}")
    endif()
    file(READ "${generated}" source)
    string(FIND "${source}" "GeneratedType<::IncrementalSample>" found)
    if((expect_type AND found EQUAL -1) OR (NOT expect_type AND NOT found EQUAL -1))
        message(FATAL_ERROR "Unexpected generated registration: ${source}")
    endif()
endfunction()

function(runGenerator expect_hit)
    execute_process(
        COMMAND "${tool}" --input "${input}" --include-dir "${root}/engine/source/runtime/reflection/include"
            --include-dir "${work}/source" --module incremental --owner 47 --output "${generated_header}"
            --source "${generated}" --depfile "${depfile}" --cache "${cache}" --verbose ${ARGN}
        WORKING_DIRECTORY "${work}/build"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE diagnostics
    )
    if(NOT status EQUAL 0 OR (expect_hit AND NOT output MATCHES "cache hit") OR
        (NOT expect_hit AND output MATCHES "cache hit"))
        message(FATAL_ERROR "Unexpected cache result: ${output}\n${diagnostics}")
    endif()
endfunction()

function(expectDiagnostic line pattern)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${work}/build" --target generate
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE diagnostics
    )
    if(status EQUAL 0 OR NOT "${output}\n${diagnostics}" MATCHES "input.hpp:${line}:[0-9]+: error: ${pattern}")
        message(FATAL_ERROR "Expected a source diagnostic: ${output}\n${diagnostics}")
    endif()
endfunction()

rebuild(TRUE TRUE)
rebuild(FALSE TRUE)
if(NOT EXISTS "${cache}")
    message(FATAL_ERROR "The CMake generation rule did not write a cache entry")
endif()
file(SHA256 "${generated}" generated_hash)
file(REMOVE "${generated}" "${generated_header}" "${depfile}")
runGenerator(TRUE)
file(SHA256 "${generated}" restored_hash)
file(READ "${depfile}" restored_dependencies)
if(NOT generated_hash STREQUAL restored_hash OR NOT EXISTS "${generated_header}" OR
    NOT restored_dependencies MATCHES "settings.hpp")
    message(FATAL_ERROR "A cache hit must restore both generated files and the transitive depfile")
endif()

runGenerator(FALSE --clang-arg "-DREFLECTION_CACHE_TEST=1")
file(READ "${generated}" from_changed_arguments)
if(from_changed_arguments MATCHES "GeneratedType<::IncrementalSample>")
    message(FATAL_ERROR "Changed Clang arguments did not remove the registration")
endif()
runGenerator(TRUE --clang-arg "-DREFLECTION_CACHE_TEST=1")
runGenerator(FALSE)
file(WRITE "${cache}" "broken cache")
runGenerator(FALSE)
runGenerator(TRUE)
rebuild(ANY TRUE)
rebuild(FALSE TRUE)

file(WRITE "${unrelated}" "void unrelated() { int value = 1; (void)value; }\n")
rebuild(FALSE TRUE)

file(WRITE "${dependency}" "#define REFLECT_SAMPLE 0\n")
rebuild(TRUE FALSE)
runGenerator(TRUE)
rebuild(FALSE FALSE)

file(APPEND "${input}" "\n")
rebuild(TRUE FALSE)

file(WRITE "${dependency}" "#define REFLECT_SAMPLE 1\n")
rebuild(TRUE TRUE)

string(REPLACE "STAR_STRUCT()" "" unmarked_header "${marked_header}")
file(WRITE "${input}" "${unmarked_header}")
rebuild(TRUE FALSE)
rebuild(FALSE FALSE)

file(WRITE "${input}" "#include \"reflection/annotations.hpp\"\nSTAR_CLASS()\nclass MissingBody {};\n")
expectDiagnostic(3 "STAR_CLASS requires STAR_GENERATED_BODY")

file(SHA256 "${cache}" valid_cache)
file(SHA256 "${generated}" valid_generated)
file(WRITE "${input}" [=[
#include "reflection/annotations.hpp"
struct Unregistered {};
STAR_STRUCT()
struct Invalid {
    STAR_PROPERTY() int *pointer = nullptr;
};
]=])
expectDiagnostic(5 "unsupported reflected property type.*int")

file(WRITE "${input}" [=[
#include "reflection/annotations.hpp"
struct Unregistered {};
STAR_STRUCT()
struct Invalid {
    STAR_PROPERTY() Unregistered record;
};
]=])
expectDiagnostic(5 "unsupported reflected property type.*Unregistered")

file(SHA256 "${cache}" cache_after_failure)
file(SHA256 "${generated}" generated_after_failure)
if(NOT valid_cache STREQUAL cache_after_failure OR NOT valid_generated STREQUAL generated_after_failure)
    message(FATAL_ERROR "Invalid input replaced a successful cache entry or generated file")
endif()
