if(NOT STAR_SLANGC)
    message(STATUS "Star: present-triangle requires slangc")
    return()
endif()

include("${PROJECT_SOURCE_DIR}/cmake/StarSlang.cmake")
set(triangle_shader "${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.slang")
set(triangle_vertex "${CMAKE_CURRENT_BINARY_DIR}/triangle-vertex.spv")
set(triangle_fragment "${CMAKE_CURRENT_BINARY_DIR}/triangle-fragment.spv")
star_compile_slang_spirv("${triangle_vertex}" "${triangle_shader}" vertexMain vertex)
star_compile_slang_spirv("${triangle_fragment}" "${triangle_shader}" fragmentMain fragment)
add_custom_target(StarPresentTriangleShaders DEPENDS "${triangle_vertex}" "${triangle_fragment}")

add_executable(StarPresentTriangle main.cpp)
add_dependencies(StarPresentTriangle StarPresentTriangleShaders)
target_link_libraries(StarPresentTriangle PRIVATE Star::RHI Star::Platform Star::Core)
set_target_properties(StarPresentTriangle PROPERTIES
    OUTPUT_NAME star-present-triangle
    LINK_DEPENDS "${triangle_vertex};${triangle_fragment}"
)
foreach(shader IN ITEMS "${triangle_vertex}" "${triangle_fragment}")
    add_custom_command(TARGET StarPresentTriangle POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${shader}" "$<TARGET_FILE_DIR:StarPresentTriangle>"
        VERBATIM)
endforeach()
