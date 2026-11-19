add_executable(StarHelloWindow main.cpp)
target_link_libraries(StarHelloWindow PRIVATE Star::Core Star::Platform)
set_target_properties(StarHelloWindow PROPERTIES OUTPUT_NAME star-hello-window)

if(STAR_BUILD_TESTS AND BUILD_TESTING)
    add_test(NAME hello-window-headless COMMAND StarHelloWindow --frames=3)
    set_tests_properties(
        hello-window-headless PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
        TIMEOUT 30
    )
endif()
