star_add_module(Asset asset
    include/asset/asset_manager.hpp
    include/asset/material.hpp
    include/asset/texture_asset.hpp
    include/asset/model_asset.hpp
    src/asset_binary.hpp
    src/asset_manager.cpp
    src/material.cpp
    src/texture_asset.cpp
    src/model_asset.cpp
)

target_link_libraries(StarAsset PUBLIC Star::Core Star::Serialization PRIVATE Star::Jobs)
