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
target_sources(StarCoreTests PRIVATE config_tests.cpp containers_tests.cpp cvar_tests.cpp delegate_tests.cpp global_allocator_tests.cpp math_tests.cpp small_vector_tests.cpp)
target_link_libraries(StarCoreTests PRIVATE Threads::Threads)
target_sources(StarJobsTests PRIVATE task_graph_tests.cpp)

add_library(StarPlatformTestModule MODULE platform_test_module.cpp)
set_target_properties(StarPlatformTestModule PROPERTIES OUTPUT_NAME star-platform-test-module)
add_dependencies(StarPlatformTests StarPlatformTestModule)
target_compile_definitions(StarPlatformTests PRIVATE STAR_PLATFORM_TEST_MODULE="$<TARGET_FILE_NAME:StarPlatformTestModule>")
file(GENERATE OUTPUT "$<TARGET_FILE_DIR:StarPlatformTestModule>/star-platform-map.txt"
    CONTENT "Star mapping\n" NEWLINE_STYLE UNIX)
file(GENERATE OUTPUT "$<TARGET_FILE_DIR:StarPlatformTestModule>/star-platform-map-empty.txt" CONTENT "")

set_tests_properties(jobs-tests platform-tests PROPERTIES TIMEOUT 30)
set_tests_properties(platform-tests PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:StarPlatformTestModule>"
)
