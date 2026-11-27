star_add_module(Asset asset
    include/asset/asset_manager.hpp
    src/asset_manager.cpp
)

target_link_libraries(StarAsset PUBLIC Star::Core PRIVATE Star::Jobs)
