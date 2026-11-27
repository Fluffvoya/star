if(NOT TARGET StarForwardShaders)
    return()
endif()

get_target_property(shaders StarForwardShaders STAR_FORWARD_SPIRV)
set(assets ${shaders} "${CMAKE_CURRENT_SOURCE_DIR}/forward-scene.obj")
add_executable(StarVulkanForward main.cpp sample_material.cpp sample_material.hpp)
add_dependencies(StarVulkanForward StarForwardShaders)
target_link_libraries(StarVulkanForward PRIVATE Star::AssetPipeline Star::Asset Star::Renderer)
set_target_properties(StarVulkanForward PROPERTIES OUTPUT_NAME star-vulkan-forward LINK_DEPENDS "${assets}")
foreach(asset IN LISTS assets)
    add_custom_command(TARGET StarVulkanForward POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${asset}" "$<TARGET_FILE_DIR:StarVulkanForward>"
        VERBATIM)
endforeach()
