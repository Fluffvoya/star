if(NOT TARGET StarForwardShaders OR NOT TARGET StarScene)
    return()
endif()

get_target_property(shaders StarForwardShaders STAR_FORWARD_SPIRV)
add_executable(StarSceneForward main.cpp)
add_dependencies(StarSceneForward StarForwardShaders)
target_link_libraries(StarSceneForward PRIVATE Star::Scene Star::Asset Star::Renderer)
set_target_properties(StarSceneForward PROPERTIES OUTPUT_NAME star-scene-forward LINK_DEPENDS "${shaders}")
foreach(asset IN LISTS shaders)
    add_custom_command(TARGET StarSceneForward POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${asset}" "$<TARGET_FILE_DIR:StarSceneForward>"
        VERBATIM)
endforeach()
