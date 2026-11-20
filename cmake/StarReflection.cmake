function(star_generate_reflection target module owner)
    cmake_parse_arguments(PARSE_ARGV 3 args "" "" "HEADERS;INCLUDE_DIRS;CLANG_ARGS")
    if(NOT TARGET StarHeaderTool OR NOT args_HEADERS OR args_UNPARSED_ARGUMENTS OR args_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "Star reflection generation requires StarHeaderTool and an explicit header list")
    endif()

    set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/${module}")
    set(source "${generated_dir}/${module}.gen.cpp")
    set(header "${generated_dir}/${module}.gen.hpp")
    set(depfile "${generated_dir}/${module}.d")
    set(headers)
    set(arguments)
    foreach(input IN LISTS args_HEADERS)
        cmake_path(ABSOLUTE_PATH input NORMALIZE OUTPUT_VARIABLE absolute_input)
        list(APPEND headers "${absolute_input}")
        list(APPEND arguments --input "${absolute_input}")
    endforeach()
    foreach(include_dir IN LISTS args_INCLUDE_DIRS)
        list(APPEND arguments --include-dir "${include_dir}")
    endforeach()
    foreach(argument IN LISTS args_CLANG_ARGS)
        list(APPEND arguments --clang-arg "${argument}")
    endforeach()

    add_custom_command(
        OUTPUT "${source}" "${header}"
        COMMAND StarHeaderTool ${arguments} --module "${module}" --owner "${owner}"
                --output "${header}" --source "${source}" --depfile "${depfile}"
        DEPENDS StarHeaderTool ${headers}
        DEPFILE "${depfile}"
        VERBATIM
    )
    target_sources(${target} PRIVATE "${source}" "${header}")
    target_include_directories(${target} PRIVATE "${generated_dir}")
    target_link_libraries(${target} PRIVATE Star::Reflection)
endfunction()
