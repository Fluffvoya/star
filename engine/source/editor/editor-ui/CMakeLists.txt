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
        "${star_imgui_dir}/misc/cpp/imgui_stdlib.cpp"
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

set(star_imguizmo_dir "${PROJECT_SOURCE_DIR}/engine/source/third-party/imguizmo")
add_library(StarImGuizmo STATIC "${star_imguizmo_dir}/ImGuizmo.cpp")
target_include_directories(StarImGuizmo SYSTEM PUBLIC "${star_imguizmo_dir}")
target_link_libraries(StarImGuizmo PRIVATE StarImGui)
set_target_properties(StarImGuizmo PROPERTIES
    OUTPUT_NAME star-imguizmo
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    POSITION_INDEPENDENT_CODE ON
)

star_add_module(EditorUI editor-ui
    include/editor-ui/imgui_layer.hpp
    include/editor-ui/editor_shell.hpp
    src/imgui_layer.cpp
    src/editor_shell.cpp
    src/scene_viewport.cpp
    src/scene_viewport.hpp
    src/scene_models.cpp
    src/scene_models.hpp
)

target_link_libraries(StarEditorUI PUBLIC Star::Core Star::Platform Star::RHI Star::EditorCore
    PRIVATE StarImGui StarImGuizmo SDL3::SDL3)
if(TARGET StarAssetPipeline)
    target_link_libraries(StarEditorUI PRIVATE Star::AssetPipeline)
    target_compile_definitions(StarEditorUI PRIVATE STAR_EDITOR_MODEL_IMPORT=1)
endif()

include("${PROJECT_SOURCE_DIR}/cmake/StarSlang.cmake")
if(STAR_SLANGC)
    set(ui_shader "${CMAKE_CURRENT_SOURCE_DIR}/shaders/imgui-ui.slang")
    set(ui_vertex "${CMAKE_CURRENT_BINARY_DIR}/imgui-ui-vertex.spv")
    set(ui_fragment "${CMAKE_CURRENT_BINARY_DIR}/imgui-ui-fragment.spv")
    star_compile_slang_spirv("${ui_vertex}" "${ui_shader}" vertexMain vertex)
    star_compile_slang_spirv("${ui_fragment}" "${ui_shader}" fragmentMain fragment)
    set(viewport_shader "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene-viewport.slang")
    set(viewport_vertex "${CMAKE_CURRENT_BINARY_DIR}/scene-viewport-vertex.spv")
    set(viewport_fragment "${CMAKE_CURRENT_BINARY_DIR}/scene-viewport-fragment.spv")
    star_compile_slang_spirv("${viewport_vertex}" "${viewport_shader}" vertexMain vertex)
    star_compile_slang_spirv("${viewport_fragment}" "${viewport_shader}" fragmentMain fragment)
    set(editor_shaders "${ui_vertex}" "${ui_fragment}" "${viewport_vertex}" "${viewport_fragment}")
    if(TARGET StarAssetPipeline)
        set(model_shader "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene-model.slang")
        set(model_vertex "${CMAKE_CURRENT_BINARY_DIR}/scene-model-vertex.spv")
        set(model_fragment "${CMAKE_CURRENT_BINARY_DIR}/scene-model-fragment.spv")
        star_compile_slang_spirv("${model_vertex}" "${model_shader}" vertexMain vertex)
        star_compile_slang_spirv("${model_fragment}" "${model_shader}" fragmentMain fragment)
        list(APPEND editor_shaders "${model_vertex}" "${model_fragment}")
    endif()
    add_custom_target(StarEditorUIShaders DEPENDS ${editor_shaders})
    set_property(TARGET StarEditorUIShaders PROPERTY STAR_UI_SPIRV "${editor_shaders}")
    add_dependencies(StarEditorUI StarEditorUIShaders)
else()
    message(STATUS "Star: EditorUI needs slangc to compile its ImGui shaders")
endif()
