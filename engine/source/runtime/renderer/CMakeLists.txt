star_add_module(Renderer renderer
    include/renderer/forward_renderer.hpp
    include/renderer/render_scene.hpp
    src/forward_renderer.cpp
)

target_link_libraries(StarRenderer PUBLIC Star::Asset PRIVATE Star::RHI)

if(TARGET StarVulkanRuntime)
    target_sources(StarRenderer PRIVATE src/environment.cpp src/environment.hpp)
    target_link_libraries(StarRenderer PRIVATE StarVulkanRuntime)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT STAR_MONOLITHIC)
        target_link_options(StarRenderer PRIVATE "LINKER:--exclude-libs,libvolk.a")
    endif()
    target_include_directories(StarRenderer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    target_compile_definitions(StarRenderer PRIVATE STAR_FORWARD_VULKAN=1)

    include("${PROJECT_SOURCE_DIR}/cmake/StarSlang.cmake")
    if(STAR_SLANGC)
        set(shader "${CMAKE_CURRENT_SOURCE_DIR}/shaders/forward.slang")
        set(vertex "${CMAKE_CURRENT_BINARY_DIR}/forward-vertex.spv")
        set(fragment "${CMAKE_CURRENT_BINARY_DIR}/forward-fragment.spv")
        set(unlit "${CMAKE_CURRENT_BINARY_DIR}/forward-unlit.spv")
        set(cluster "${CMAKE_CURRENT_BINARY_DIR}/forward-cluster.spv")
        star_compile_slang_spirv("${vertex}" "${shader}" vertexMain vertex)
        star_compile_slang_spirv("${fragment}" "${shader}" fragmentMain fragment)
        star_compile_slang_spirv("${unlit}" "${shader}" unlitMain fragment)
        star_compile_slang_spirv("${cluster}" "${shader}" clusterMain compute)
        add_custom_target(StarForwardShaders DEPENDS "${vertex}" "${fragment}" "${unlit}" "${cluster}")
        add_dependencies(StarRenderer StarForwardShaders)
        set_property(TARGET StarForwardShaders PROPERTY STAR_FORWARD_SPIRV "${vertex};${fragment};${unlit};${cluster}")
    endif()
endif()
