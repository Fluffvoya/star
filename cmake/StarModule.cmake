include(GenerateExportHeader)

function(star_add_module name slug)
    set(target "Star${name}")
    if(STAR_MONOLITHIC)
        add_library(${target} STATIC ${ARGN})
        target_compile_definitions(${target} PUBLIC STAR_MONOLITHIC=1)
    else()
        add_library(${target} SHARED ${ARGN})
    endif()
    add_library(Star::${name} ALIAS ${target})

    set_target_properties(
        ${target} PROPERTIES
        OUTPUT_NAME "star-${slug}"
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )
    target_compile_features(${target} PUBLIC cxx_std_23)

    set(export_dir "${CMAKE_BINARY_DIR}/generated/${slug}")
    string(TOUPPER "${name}" export_name)
    generate_export_header(
        ${target}
        BASE_NAME "STAR_${export_name}"
        EXPORT_MACRO_NAME "STAR_${export_name}_API"
        EXPORT_FILE_NAME "${export_dir}/${slug}/export.hpp"
        STATIC_DEFINE STAR_MONOLITHIC
    )
    target_include_directories(
        ${target}
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${export_dir}"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
endfunction()
