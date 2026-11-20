star_add_module(Reflection reflection
    include/reflection/annotations.hpp
    include/reflection/reflection.hpp
    src/reflection.cpp
)

target_link_libraries(StarReflection PUBLIC Star::Core)
