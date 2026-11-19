include_guard(GLOBAL)

include(GenerateExportHeader)

function(star_add_module module_name)
    cmake_parse_arguments(
        STAR_MODULE
        "REFLECT"
        "TYPE;PCH"
        "DEPENDS_PUBLIC;DEPENDS_PRIVATE;SOURCES"
        ${ARGN}
    )

    if(NOT STAR_MODULE_TYPE)
        message(FATAL_ERROR "star_add_module(${module_name}) requires TYPE")
    endif()

    if(NOT STAR_MODULE_TYPE MATCHES "^(RUNTIME|EDITOR|TOOL)$")
        message(FATAL_ERROR "Unknown module type '${STAR_MODULE_TYPE}' for ${module_name}")
    endif()

    set(module_sources ${STAR_MODULE_SOURCES})
    if(NOT module_sources)
        file(
            GLOB_RECURSE module_sources
            CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/private/*.cpp"
        )
    endif()

    set(target_name "Star${module_name}")

    add_library(${target_name} ${STAR_MODULE_LIBRARY_TYPE} ${module_sources})
    add_library(Star::${module_name} ALIAS ${target_name})

    set_target_properties(
        ${target_name}
        PROPERTIES
            OUTPUT_NAME "${target_name}"
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES
    )

    target_include_directories(
        ${target_name}
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/public"
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/private"
            "${CMAKE_CURRENT_BINARY_DIR}"
    )

    string(TOUPPER "${module_name}" module_export_name)
    string(TOLOWER "${module_name}" module_path_name)
    set(export_directory "${CMAKE_BINARY_DIR}/generated/${module_path_name}")
    set(export_header "${export_directory}/${module_path_name}/export.hpp")
    generate_export_header(
        ${target_name}
        BASE_NAME STAR_${module_export_name}
        EXPORT_FILE_NAME "${export_header}"
        EXPORT_MACRO_NAME STAR_${module_export_name}_API
        STATIC_DEFINE STAR_MONOLITHIC
    )
    target_include_directories(${target_name} PUBLIC "${export_directory}")

    foreach(dependency IN LISTS STAR_MODULE_DEPENDS_PUBLIC)
        target_link_libraries(${target_name} PUBLIC "Star::${dependency}")
    endforeach()

    foreach(dependency IN LISTS STAR_MODULE_DEPENDS_PRIVATE)
        target_link_libraries(${target_name} PRIVATE "${dependency}")
    endforeach()

    if(STAR_MODULE_PCH)
        target_precompile_headers(${target_name} PRIVATE "${STAR_MODULE_PCH}")
    endif()

    star_configure_compiler_options(${target_name})

    if(STAR_MODULE_REFLECT)
        set_property(TARGET ${target_name} PROPERTY STAR_REFLECT_ENABLED YES)
    endif()
endfunction()
