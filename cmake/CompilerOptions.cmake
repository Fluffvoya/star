function(star_configure_compiler_options target)
    target_compile_features(${target} PUBLIC cxx_std_23)

    if(MSVC)
        target_compile_options(
            ${target}
            PRIVATE
                /W4
                /permissive-
                /utf-8
                /Zc:__cplusplus
        )

        target_compile_options(
            ${target}
            PRIVATE
                $<$<CONFIG:Debug>:/Od>
                $<$<CONFIG:Development>:/O2>
                $<$<CONFIG:Profile>:/O2>
                $<$<CONFIG:Shipping>:/O2>
                $<$<NOT:$<CONFIG:Debug>>:/Oi>
        )
    else()
        target_compile_options(
            ${target}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wshadow
                -Wnon-virtual-dtor
        )

        target_compile_options(
            ${target}
            PRIVATE
                $<$<CONFIG:Debug>:-O0>
                $<$<CONFIG:Debug>:-g>
                $<$<CONFIG:Development>:-O2>
                $<$<CONFIG:Development>:-g>
                $<$<CONFIG:Profile>:-O2>
                $<$<CONFIG:Profile>:-g>
                $<$<CONFIG:Shipping>:-O3>
        )
    endif()

    target_compile_definitions(
        ${target}
        PRIVATE
            $<$<CONFIG:Debug>:STAR_DEBUG=1>
            $<$<CONFIG:Development>:STAR_DEVELOPMENT=1>
            $<$<CONFIG:Profile>:STAR_PROFILE=1>
            $<$<CONFIG:Shipping>:STAR_SHIPPING=1>
    )

    if(STAR_MONOLITHIC)
        target_compile_definitions(${target} PUBLIC STAR_MONOLITHIC=1)
    endif()
endfunction()
