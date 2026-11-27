star_add_module(AssetPipeline asset-pipeline
    include/asset-pipeline/asset_database.hpp
    include/asset-pipeline/derived_data_cache.hpp
    src/asset_database.cpp
    src/derived_data_cache.cpp
)

target_link_libraries(StarAssetPipeline PUBLIC Star::Core Star::Serialization)

find_package(xxHash 0.8 CONFIG QUIET)
if(TARGET xxHash::xxhash)
    target_link_libraries(StarAssetPipeline PRIVATE xxHash::xxhash)
else()
    find_path(STAR_XXHASH_INCLUDE_DIR NAMES xxhash.h REQUIRED)
    find_library(STAR_XXHASH_LIBRARY NAMES xxhash REQUIRED)
    target_include_directories(StarAssetPipeline PRIVATE "${STAR_XXHASH_INCLUDE_DIR}")
    target_link_libraries(StarAssetPipeline PRIVATE "${STAR_XXHASH_LIBRARY}")
endif()
