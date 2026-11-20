star_add_module(Serialization serialization
    include/serialization/archive.hpp
    src/archive.cpp
    src/archive_value.hpp
    src/binary_archive.cpp
    src/yaml_archive.cpp
)

target_link_libraries(StarSerialization PUBLIC Star::Reflection PRIVATE ryml)
