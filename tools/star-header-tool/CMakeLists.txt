find_path(STAR_LIBCLANG_INCLUDE_DIR clang-c/Index.h)
find_library(STAR_LIBCLANG_LIBRARY NAMES clang libclang)

if(NOT STAR_LIBCLANG_INCLUDE_DIR OR NOT STAR_LIBCLANG_LIBRARY)
    message(WARNING "StarHeaderTool disabled because libclang headers or library were not found")
    return()
endif()

get_filename_component(clang_library_dir "${STAR_LIBCLANG_LIBRARY}" DIRECTORY)
find_program(STAR_CLANG_DRIVER NAMES clang HINTS "${clang_library_dir}/../bin")
if(STAR_CLANG_DRIVER)
    execute_process(
        COMMAND "${STAR_CLANG_DRIVER}" -print-resource-dir
        OUTPUT_VARIABLE STAR_CLANG_RESOURCE_DIR
        RESULT_VARIABLE clang_result
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

add_executable(StarHeaderTool main.cpp generation_cache.cpp generation_cache.hpp runtime_generator.cpp runtime_generator.hpp)
target_compile_features(StarHeaderTool PRIVATE cxx_std_23)
target_include_directories(StarHeaderTool PRIVATE "${STAR_LIBCLANG_INCLUDE_DIR}")
target_link_libraries(StarHeaderTool PRIVATE "${STAR_LIBCLANG_LIBRARY}")
if(STAR_CLANG_RESOURCE_DIR AND clang_result EQUAL 0)
    target_compile_definitions(StarHeaderTool PRIVATE STAR_CLANG_RESOURCE_DIR="${STAR_CLANG_RESOURCE_DIR}")
endif()
set_target_properties(StarHeaderTool PROPERTIES OUTPUT_NAME "star-header-tool")
