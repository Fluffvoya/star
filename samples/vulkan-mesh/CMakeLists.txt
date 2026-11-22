if(NOT TARGET StarVulkanRuntime)
    return()
endif()

find_package(SDL3 3.2 REQUIRED CONFIG)
include("${PROJECT_SOURCE_DIR}/cmake/StarSlang.cmake")
if(NOT STAR_SLANGC)
    message(STATUS "Star: the Vulkan mesh sample requires slangc")
    return()
endif()

set(shader_source "${CMAKE_CURRENT_SOURCE_DIR}/mesh.slang")
set(vertex_shader "${CMAKE_CURRENT_BINARY_DIR}/mesh-vertex.spv")
set(fragment_shader "${CMAKE_CURRENT_BINARY_DIR}/mesh-fragment.spv")
star_compile_slang_spirv("${vertex_shader}" "${shader_source}" vertexMain vertex)
star_compile_slang_spirv("${fragment_shader}" "${shader_source}" fragmentMain fragment)

add_executable(StarVulkanMesh main.cpp "${vertex_shader}" "${fragment_shader}")
target_link_libraries(StarVulkanMesh PRIVATE Star::RHI StarVulkanRuntime SDL3::SDL3)
set_target_properties(StarVulkanMesh PROPERTIES
    OUTPUT_NAME star-vulkan-mesh
    LINK_DEPENDS "${vertex_shader};${fragment_shader}")
add_custom_command(TARGET StarVulkanMesh POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${vertex_shader}" "${fragment_shader}" "$<TARGET_FILE_DIR:StarVulkanMesh>"
    VERBATIM)
