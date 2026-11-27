if(NOT TARGET StarVulkanRuntime)
    return()
endif()

include("${PROJECT_SOURCE_DIR}/cmake/StarSlang.cmake")
if(NOT STAR_SLANGC)
    return()
endif()

set(shader_source "${CMAKE_CURRENT_SOURCE_DIR}/forward.slang")
set(vertex_shader "${CMAKE_CURRENT_BINARY_DIR}/forward-vertex.spv")
set(fragment_shader "${CMAKE_CURRENT_BINARY_DIR}/forward-fragment.spv")
set(unlit_shader "${CMAKE_CURRENT_BINARY_DIR}/forward-unlit.spv")
star_compile_slang_spirv("${unlit_shader}" "${shader_source}" unlitMain fragment)
set(cluster_shader "${CMAKE_CURRENT_BINARY_DIR}/forward-cluster.spv")
star_compile_slang_spirv("${cluster_shader}" "${shader_source}" clusterMain compute)
star_compile_slang_spirv("${vertex_shader}" "${shader_source}" vertexMain vertex)
star_compile_slang_spirv("${fragment_shader}" "${shader_source}" fragmentMain fragment)

add_executable(StarVulkanForward main.cpp environment.cpp environment.hpp sample_material.cpp sample_material.hpp "${vertex_shader}" "${fragment_shader}" "${cluster_shader}" "${unlit_shader}")
target_link_libraries(StarVulkanForward PRIVATE Star::AssetPipeline Star::Asset Star::RHI StarVulkanRuntime)
set_target_properties(StarVulkanForward PROPERTIES OUTPUT_NAME star-vulkan-forward)
foreach(asset IN ITEMS "${vertex_shader}" "${fragment_shader}" "${cluster_shader}" "${unlit_shader}" "${CMAKE_CURRENT_SOURCE_DIR}/forward-scene.obj")
    add_custom_command(TARGET StarVulkanForward POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${asset}" "$<TARGET_FILE_DIR:StarVulkanForward>"
        VERBATIM)
endforeach()
set_property(TARGET StarVulkanForward APPEND PROPERTY LINK_DEPENDS
    "${vertex_shader}" "${fragment_shader}" "${cluster_shader}" "${unlit_shader}" "${CMAKE_CURRENT_SOURCE_DIR}/forward-scene.obj")
