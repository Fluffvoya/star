star_add_module(EditorCore editor
    include/editor/document.hpp
    include/editor/command.hpp
    src/document.cpp
    src/commands.cpp
)

target_link_libraries(StarEditorCore PUBLIC Star::Core Star::Scene Star::Serialization)
