find_package(EnTT 3.14.0 EXACT CONFIG REQUIRED)

star_add_module(Scene scene
    include/scene/components.hpp
    include/scene/game_object.hpp
    include/scene/scene.hpp
    include/scene/world.hpp
    src/builtin_components.cpp
    src/builtin_components.hpp
    src/scene.cpp
    src/transform_math.cpp
    src/transform_math.hpp
    src/world.cpp
)

target_link_libraries(StarScene PUBLIC Star::Core Star::Reflection PRIVATE EnTT::EnTT)
