add_custom_target(StarPublicHeadersCheck)

foreach(module IN ITEMS Core Jobs Platform)
    string(TOLOWER "${module}" module_slug)
    set(target "Star${module}Tests")
    set(test_name "${module_slug}-tests")

    add_executable(${target} "${module_slug}_tests.cpp" test_main.cpp)
    target_link_libraries(${target} PRIVATE Star::${module})
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "star-${test_name}")

    add_test(NAME ${test_name} COMMAND ${target})

    get_target_property(module_source_dir Star${module} SOURCE_DIR)
    set(include_dir "${module_source_dir}/include")
    file(GLOB_RECURSE public_headers RELATIVE "${include_dir}" CONFIGURE_DEPENDS
        "${include_dir}/*.h" "${include_dir}/*.hpp")
    list(APPEND public_headers "${module_slug}/export.hpp")

    set(header_target "Star${module}PublicHeadersCheck")
    add_library(${header_target} OBJECT EXCLUDE_FROM_ALL)
    target_link_libraries(${header_target} PRIVATE Star::${module})
    set_target_properties(${header_target} PROPERTIES
        DISABLE_PRECOMPILE_HEADERS ON
        UNITY_BUILD OFF
    )
    foreach(header IN LISTS public_headers)
        set(check_source "${CMAKE_CURRENT_BINARY_DIR}/public-headers/${module_slug}/${header}.cpp")
        file(GENERATE OUTPUT "${check_source}" CONTENT "#include \"${header}\"\n")
        target_sources(${header_target} PRIVATE "${check_source}")
    endforeach()
    add_dependencies(StarPublicHeadersCheck ${header_target})
endforeach()

add_test(NAME public-headers COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
    --target StarPublicHeadersCheck --config "$<CONFIG>")

find_package(Threads REQUIRED)
target_sources(StarCoreTests PRIVATE config_tests.cpp containers_tests.cpp cvar_tests.cpp delegate_tests.cpp global_allocator_tests.cpp math_tests.cpp small_vector_tests.cpp)
target_sources(StarCoreTests PRIVATE spsc_queue_tests.cpp)
target_link_libraries(StarCoreTests PRIVATE Threads::Threads)
target_sources(StarJobsTests PRIVATE task_graph_tests.cpp)
add_executable(StarJobsWorkerCountTests jobs_worker_count_tests.cpp test_main.cpp)
target_link_libraries(StarJobsWorkerCountTests PRIVATE Star::Jobs)
set_target_properties(StarJobsWorkerCountTests PROPERTIES OUTPUT_NAME "star-jobs-worker-count-tests")
add_test(NAME jobs-worker-count COMMAND StarJobsWorkerCountTests)
set_tests_properties(jobs-worker-count PROPERTIES ENVIRONMENT "STAR_JOBS_WORKERS=2")
target_sources(StarPlatformTests PRIVATE platform_thread_tests.cpp)
target_link_libraries(StarPlatformTests PRIVATE Threads::Threads)

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
