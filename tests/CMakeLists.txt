add_executable(StarCoreTests core_tests.cpp test_main.cpp)
target_include_directories(StarCoreTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(StarCoreTests PRIVATE Star::Core)
star_configure_compiler_options(StarCoreTests)
add_test(NAME core-tests COMMAND StarCoreTests)

add_executable(StarJobsTests jobs_tests.cpp test_main.cpp)
target_include_directories(StarJobsTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(StarJobsTests PRIVATE Star::Jobs)
star_configure_compiler_options(StarJobsTests)
add_test(NAME jobs-tests COMMAND StarJobsTests)
set_tests_properties(
    jobs-tests
    PROPERTIES
        TIMEOUT 30
)

add_executable(StarPlatformTests platform_tests.cpp test_main.cpp)
target_include_directories(StarPlatformTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(StarPlatformTests PRIVATE Star::Platform)
star_configure_compiler_options(StarPlatformTests)
add_test(NAME platform-tests COMMAND StarPlatformTests)
set_tests_properties(
    platform-tests
    PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
        TIMEOUT 30
)
