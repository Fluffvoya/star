find_package(SDL3 3.2 REQUIRED CONFIG)

star_add_module(RHI rhi
    include/rhi/rhi.hpp
    include/rhi/vulkan.hpp
    src/null_device.cpp
    src/vulkan_probe.cpp
)

target_link_libraries(StarRHI PUBLIC Star::Core PRIVATE SDL3::SDL3)
