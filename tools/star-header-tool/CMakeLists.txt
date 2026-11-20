find_path(STAR_LIBCLANG_INCLUDE_DIR clang-c/Index.h)
find_library(STAR_LIBCLANG_LIBRARY NAMES clang libclang)

if(NOT STAR_LIBCLANG_INCLUDE_DIR OR NOT STAR_LIBCLANG_LIBRARY)
    message(WARNING "StarHeaderTool disabled because libclang headers or library were not found")
    return()
endif()

add_executable(StarHeaderTool main.cpp)
target_compile_features(StarHeaderTool PRIVATE cxx_std_23)
target_include_directories(StarHeaderTool PRIVATE "${STAR_LIBCLANG_INCLUDE_DIR}")
target_link_libraries(StarHeaderTool PRIVATE "${STAR_LIBCLANG_LIBRARY}")
set_target_properties(StarHeaderTool PROPERTIES OUTPUT_NAME "star-header-tool")
