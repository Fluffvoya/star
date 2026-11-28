set(star_imgui_dir "${PROJECT_SOURCE_DIR}/engine/source/third-party/imgui")
if(NOT EXISTS "${star_imgui_dir}/imgui.cpp")
    message(FATAL_ERROR "Initialize the pinned ImGui dependency with git submodule update --init --recursive.")
endif()

if(NOT TARGET StarImGui)
    add_library(StarImGui STATIC
        "${star_imgui_dir}/imgui.cpp"
        "${star_imgui_dir}/imgui_draw.cpp"
        "${star_imgui_dir}/imgui_tables.cpp"
        "${star_imgui_dir}/imgui_widgets.cpp"
        "${star_imgui_dir}/imgui_demo.cpp"
    )
    target_include_directories(StarImGui SYSTEM PUBLIC "${star_imgui_dir}")
    target_compile_definitions(StarImGui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
    set_target_properties(StarImGui PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        POSITION_INDEPENDENT_CODE ON
    )
endif()

find_package(SDL3 3.2 REQUIRED CONFIG)

star_add_module(EditorUI editor-ui
    include/editor-ui/imgui_layer.hpp
    include/editor-ui/editor_shell.hpp
    src/imgui_layer.cpp
    src/editor_shell.cpp
)

target_link_libraries(StarEditorUI PUBLIC Star::Core Star::Platform Star::RHI PRIVATE StarImGui SDL3::SDL3)

include("${PROJECT_SOURCE_DIR}/cmake/StarSlang.cmake")
if(STAR_SLANGC)
    set(ui_shader "${CMAKE_CURRENT_SOURCE_DIR}/shaders/imgui-ui.slang")
    set(ui_vertex "${CMAKE_CURRENT_BINARY_DIR}/imgui-ui-vertex.spv")
    set(ui_fragment "${CMAKE_CURRENT_BINARY_DIR}/imgui-ui-fragment.spv")
    star_compile_slang_spirv("${ui_vertex}" "${ui_shader}" vertexMain vertex)
    star_compile_slang_spirv("${ui_fragment}" "${ui_shader}" fragmentMain fragment)
    add_custom_target(StarEditorUIShaders DEPENDS "${ui_vertex}" "${ui_fragment}")
    set_property(TARGET StarEditorUIShaders PROPERTY STAR_UI_SPIRV "${ui_vertex};${ui_fragment}")
    add_dependencies(StarEditorUI StarEditorUIShaders)
else()
    message(STATUS "Star: EditorUI needs slangc to compile its ImGui shaders")
endif()
