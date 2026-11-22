find_program(STAR_SLANGC NAMES slangc DOC "Slang shader compiler executable")
find_program(STAR_SPIRV_VAL NAMES spirv-val DOC "SPIR-V validator executable")

function(star_compile_slang_spirv output source entry stage)
    set(reflection "${output}.json")
    set(validation)
    if(STAR_SPIRV_VAL)
        set(validation COMMAND "${STAR_SPIRV_VAL}" --target-env vulkan1.3 "${output}")
    endif()
    add_custom_command(
        OUTPUT "${output}"
        BYPRODUCTS "${reflection}"
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${output}" "${reflection}"
        COMMAND "${STAR_SLANGC}" "${source}" -entry "${entry}" -stage "${stage}"
            -target spirv -profile spirv_1_6 -o "${output}"
            -reflection-json "${reflection}" -depfile "${output}.d"
        ${validation}
        DEPENDS "${source}" "${STAR_SLANGC}"
        DEPFILE "${output}.d"
        VERBATIM
    )
endfunction()
