star_add_module(RHI rhi
    include/rhi/rhi.hpp
    src/null_device.cpp
)

target_link_libraries(StarRHI PUBLIC Star::Core)
