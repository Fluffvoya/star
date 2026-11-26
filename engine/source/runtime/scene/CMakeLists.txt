find_package(EnTT 3.14.0 EXACT CONFIG REQUIRED)

star_add_module(Scene scene
    include/scene/game_object.hpp
    include/scene/scene.hpp
    include/scene/world.hpp
    src/scene.cpp
    src/world.cpp
)

target_link_libraries(StarScene PUBLIC Star::Core PRIVATE EnTT::EnTT)
