star_add_module(Scene scene
    include/scene/components.hpp
    include/scene/game_object.hpp
    include/scene/game_module.hpp
    include/scene/scene.hpp
    include/scene/scene_archive.hpp
    include/scene/static_render_snapshot.hpp
    include/scene/world.hpp
    include/scene/system.hpp
    src/builtin_components.cpp
    src/builtin_components.hpp
    src/game_module.cpp
    src/scene.cpp
    src/scene_archive.cpp
    src/scene_archive_access.hpp
    src/static_render_snapshot.cpp
    src/transform_math.cpp
    src/transform_math.hpp
    src/world.cpp
    src/system.cpp
)

target_link_libraries(StarScene PUBLIC Star::Core Star::Reflection Star::Serialization Star::Asset PRIVATE EnTT::EnTT Star::Jobs Star::Platform)
