cmake_minimum_required(VERSION 3.28)
project(StarGameModuleFixture LANGUAGES CXX)

function(add_fixture target output)
    add_library(${target} MODULE game_module_fixture.cpp)
    target_compile_features(${target} PRIVATE cxx_std_23)
    set_target_properties(${target} PROPERTIES
        CXX_EXTENSIONS OFF
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        PREFIX ""
        SUFFIX "${STAR_MODULE_SUFFIX}"
        OUTPUT_NAME "${output}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    foreach(module IN ITEMS SCENE CORE REFLECTION SERIALIZATION)
        target_include_directories(${target} PRIVATE "${STAR_${module}_INCLUDE}" "${STAR_${module}_GENERATED}")
        target_link_libraries(${target} PRIVATE "${STAR_${module}_LINK}")
    endforeach()
    target_compile_definitions(${target} PRIVATE
        $<$<CONFIG:Debug>:STAR_DEBUG=1>
        $<$<CONFIG:Development>:STAR_DEVELOPMENT=1>
        $<$<CONFIG:Profile>:STAR_PROFILE=1>
        $<$<CONFIG:Shipping>:STAR_SHIPPING=1>
    )
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE -fno-gnu-unique)
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /permissive- /utf-8 /Zc:__cplusplus)
    endif()
endfunction()

add_fixture(fixture star-m3-game-module)
add_fixture(reload_fixture star-m5-game-module)
add_fixture(failing_fixture star-m5-game-module-failing)
add_fixture(auxiliary_fixture star-m5-auxiliary-module)
add_fixture(failing_auxiliary_fixture star-m5-auxiliary-module-failing)
target_compile_definitions(reload_fixture PRIVATE STAR_FIXTURE_RELOAD_VERSION=1)
target_compile_definitions(failing_fixture PRIVATE STAR_FIXTURE_FAIL_STARTUP=1)
target_compile_definitions(auxiliary_fixture PRIVATE STAR_FIXTURE_SECONDARY=1)
target_compile_definitions(failing_auxiliary_fixture PRIVATE STAR_FIXTURE_SECONDARY=1 STAR_FIXTURE_FAIL_STARTUP=1)
