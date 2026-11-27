cmake_minimum_required(VERSION 3.28)
project(StarGameModuleFixture LANGUAGES CXX)

add_library(fixture MODULE game_module_fixture.cpp)
target_compile_features(fixture PRIVATE cxx_std_23)
set_target_properties(fixture PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    PREFIX ""
    SUFFIX "${STAR_MODULE_SUFFIX}"
    OUTPUT_NAME "star-m3-game-module"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
foreach(module IN ITEMS SCENE CORE REFLECTION SERIALIZATION)
    target_include_directories(fixture PRIVATE "${STAR_${module}_INCLUDE}" "${STAR_${module}_GENERATED}")
    target_link_libraries(fixture PRIVATE "${STAR_${module}_LINK}")
endforeach()
target_compile_definitions(fixture PRIVATE
    $<$<CONFIG:Debug>:STAR_DEBUG=1>
    $<$<CONFIG:Development>:STAR_DEVELOPMENT=1>
    $<$<CONFIG:Profile>:STAR_PROFILE=1>
    $<$<CONFIG:Shipping>:STAR_SHIPPING=1>
)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(fixture PRIVATE -fno-gnu-unique)
endif()
if(MSVC)
    target_compile_options(fixture PRIVATE /permissive- /utf-8 /Zc:__cplusplus)
endif()
