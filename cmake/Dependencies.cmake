include_guard(GLOBAL)

set(STAR_SDL3_MINIMUM_VERSION 3.2)

if(STAR_SHIPPING_DEPS)
    message(STATUS "Star: shipping dependency policy enabled; packages must come from the configured toolchain")
endif()

find_package(SDL3 ${STAR_SDL3_MINIMUM_VERSION} QUIET)
if(SDL3_FOUND)
    message(STATUS "Star: using system SDL3 ${SDL3_VERSION}")
else()
    message(FATAL_ERROR
        "SDL3 ${STAR_SDL3_MINIMUM_VERSION}+ was not found. "
        "Install it through the platform package manager or provide a vcpkg toolchain file."
    )
endif()

find_package(glm CONFIG QUIET)
if(glm_FOUND)
    message(STATUS "Star: using system glm ${glm_VERSION}")
else()
    message(STATUS "Star: glm not found; math-dependent Core facilities will remain unavailable")
endif()
