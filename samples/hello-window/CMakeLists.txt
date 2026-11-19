add_executable(StarHelloWindow main.cpp)
target_link_libraries(StarHelloWindow PRIVATE Star::Core Star::Platform)
star_configure_compiler_options(StarHelloWindow)
star_set_artifact_output_name(StarHelloWindow)

if(STAR_BUILD_TESTS)
    add_test(NAME hello-window-headless COMMAND StarHelloWindow --frames=3)
    set_tests_properties(
        hello-window-headless
        PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
            TIMEOUT 30
    )
endif()
