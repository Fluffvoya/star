star_add_module(EditorCore editor
    include/editor/document.hpp
    include/editor/command.hpp
    include/editor/project.hpp
    src/document.cpp
    src/commands.cpp
    src/file_io.cpp
    src/file_io.hpp
    src/project.cpp
)

target_link_libraries(StarEditorCore PUBLIC Star::Core Star::Scene Star::Serialization)
