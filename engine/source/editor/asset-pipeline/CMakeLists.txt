star_add_module(AssetPipeline asset-pipeline
    include/asset-pipeline/asset_database.hpp
    include/asset-pipeline/derived_data_cache.hpp
    include/asset-pipeline/texture_importer.hpp
    include/asset-pipeline/cached_texture_import.hpp
    include/asset-pipeline/model_importer.hpp
    src/asset_database.cpp
    src/derived_data_cache.cpp
    src/texture_importer.cpp
    src/cached_texture_import.cpp
    src/model_importer.cpp
    src/gltf_importer.cpp
)

target_link_libraries(StarAssetPipeline PUBLIC Star::Core Star::Serialization Star::Asset)
target_include_directories(StarAssetPipeline SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/engine/source/third-party/stb")

find_package(xxHash 0.8 CONFIG QUIET)
if(TARGET xxHash::xxhash)
    target_link_libraries(StarAssetPipeline PRIVATE xxHash::xxhash)
else()
    find_path(STAR_XXHASH_INCLUDE_DIR NAMES xxhash.h REQUIRED)
    find_library(STAR_XXHASH_LIBRARY NAMES xxhash REQUIRED)
    target_include_directories(StarAssetPipeline PRIVATE "${STAR_XXHASH_INCLUDE_DIR}")
    target_link_libraries(StarAssetPipeline PRIVATE "${STAR_XXHASH_LIBRARY}")
endif()

set(star_ufbx_dir "${PROJECT_SOURCE_DIR}/engine/source/third-party/ufbx")
add_library(StarUfbx OBJECT "${star_ufbx_dir}/ufbx.c")
target_include_directories(StarUfbx SYSTEM INTERFACE "${star_ufbx_dir}")
set_target_properties(StarUfbx PROPERTIES C_VISIBILITY_PRESET hidden)
target_link_libraries(StarAssetPipeline PRIVATE StarUfbx)

find_package(fastgltf CONFIG REQUIRED)
target_link_libraries(StarAssetPipeline PRIVATE fastgltf::fastgltf)
