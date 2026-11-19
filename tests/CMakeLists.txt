foreach(module IN ITEMS Core Jobs Platform)
    string(TOLOWER "${module}" module_slug)
    set(target "Star${module}Tests")
    set(test_name "${module_slug}-tests")

    add_executable(${target} "${module_slug}_tests.cpp" test_main.cpp)
    target_link_libraries(${target} PRIVATE Star::${module})
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "star-${test_name}")

    add_test(NAME ${test_name} COMMAND ${target})
endforeach()

find_package(Threads REQUIRED)
target_sources(StarCoreTests PRIVATE cvar_tests.cpp)
target_link_libraries(StarCoreTests PRIVATE Threads::Threads)

set_tests_properties(jobs-tests platform-tests PROPERTIES TIMEOUT 30)
set_tests_properties(platform-tests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
