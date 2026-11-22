find_package(SDL3 3.2 REQUIRED CONFIG)

star_add_module(RHI rhi
    include/rhi/rhi.hpp
    include/rhi/render_graph.hpp
    include/rhi/shader_compiler.hpp
    include/rhi/vulkan.hpp
    src/null_device.cpp
    src/render_graph.cpp
    src/shader_compiler.cpp
    src/vulkan_device.cpp
    src/vulkan_probe.cpp
)

target_link_libraries(StarRHI PUBLIC Star::Core PRIVATE SDL3::SDL3)

if(WIN32 OR (UNIX AND NOT APPLE))
    find_package(VulkanHeaders 1.3 QUIET CONFIG)
    find_package(volk QUIET CONFIG)
    find_package(VulkanMemoryAllocator QUIET CONFIG)
    if(NOT TARGET GPUOpen::VulkanMemoryAllocator)
        find_package(unofficial-vulkan-memory-allocator QUIET CONFIG)
    endif()
    if(TARGET GPUOpen::VulkanMemoryAllocator)
        set(vma_target GPUOpen::VulkanMemoryAllocator)
    elseif(TARGET unofficial::VulkanMemoryAllocator::VulkanMemoryAllocator)
        set(vma_target unofficial::VulkanMemoryAllocator::VulkanMemoryAllocator)
    endif()

    if(TARGET Vulkan::Headers AND TARGET volk::volk AND vma_target)
        add_library(StarVulkanRuntime STATIC src/vulkan_runtime.cpp src/vma.cpp)
        target_link_libraries(StarVulkanRuntime PUBLIC Vulkan::Headers volk::volk ${vma_target}
            PRIVATE SDL3::SDL3 Star::Core)
        target_include_directories(StarVulkanRuntime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_include_directories(StarVulkanRuntime PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_BINARY_DIR}/generated/rhi")
        target_compile_definitions(StarVulkanRuntime PUBLIC VK_NO_PROTOTYPES VMA_STATIC_VULKAN_FUNCTIONS=0
            VMA_DYNAMIC_VULKAN_FUNCTIONS=1 VMA_VULKAN_VERSION=1003000)
        set_target_properties(StarVulkanRuntime PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
        target_compile_definitions(StarRHI PRIVATE STAR_WITH_VULKAN=1)
        target_link_libraries(StarRHI PRIVATE StarVulkanRuntime)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT STAR_MONOLITHIC)
            target_link_options(StarRHI PRIVATE "LINKER:--exclude-libs,libvolk.a")
        endif()
    else()
        message(STATUS "Star: Vulkan device requires Vulkan-Headers, volk and Vulkan Memory Allocator")
    endif()
endif()
