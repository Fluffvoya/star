star_add_module(Asset asset
    include/asset/asset_manager.hpp
    include/asset/material.hpp
    src/asset_manager.cpp
    src/material.cpp
)

target_link_libraries(StarAsset PUBLIC Star::Core Star::Serialization PRIVATE Star::Jobs)
