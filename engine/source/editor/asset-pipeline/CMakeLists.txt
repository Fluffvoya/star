star_add_module(AssetPipeline asset-pipeline
    include/asset-pipeline/asset_database.hpp
    src/asset_database.cpp
)

target_link_libraries(StarAssetPipeline PUBLIC Star::Core Star::Serialization)
