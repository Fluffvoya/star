if(NOT TARGET StarEditorUI)
    return()
endif()
if(NOT TARGET StarEditorUIShaders)
    message(STATUS "Star: star-editor requires slangc for its UI shaders")
    return()
endif()

get_target_property(ui_shaders StarEditorUIShaders STAR_UI_SPIRV)

add_executable(StarEditor main.cpp)
target_link_libraries(StarEditor PRIVATE Star::EditorUI Star::Platform Star::RHI Star::Core)
set_target_properties(StarEditor PROPERTIES
    OUTPUT_NAME star-editor
    LINK_DEPENDS "${ui_shaders}"
)

foreach(shader IN LISTS ui_shaders)
    add_custom_command(TARGET StarEditor POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${shader}" "$<TARGET_FILE_DIR:StarEditor>"
        VERBATIM)
endforeach()
