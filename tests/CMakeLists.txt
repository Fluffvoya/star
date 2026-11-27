if(CMAKE_GENERATOR MATCHES "^Ninja")
    set(star_test_ninja "${CMAKE_MAKE_PROGRAM}")
else()
    find_program(star_test_ninja NAMES ninja ninja-build REQUIRED)
endif()

add_custom_target(StarPublicHeadersCheck)

foreach(module IN ITEMS Core Reflection Serialization Scene RHI Jobs Platform Asset AssetPipeline)
    get_target_property(module_output Star${module} OUTPUT_NAME)
    string(REGEX REPLACE "^star-" "" module_slug "${module_output}")
    set(target "Star${module}Tests")
    set(test_name "${module_slug}-tests")

    get_target_property(module_source_dir Star${module} SOURCE_DIR)
    if(module STREQUAL "AssetPipeline")
        set(test_source "${module_source_dir}/tests/asset_database_tests.cpp")
        list(APPEND test_source "${module_source_dir}/tests/derived_data_cache_tests.cpp")
        list(APPEND test_source "${module_source_dir}/tests/texture_importer_tests.cpp")
        list(APPEND test_source "${module_source_dir}/tests/model_importer_tests.cpp")
    elseif(module MATCHES "^(Serialization|Scene|Asset)$")
        set(test_source "${module_source_dir}/tests/${module_slug}_tests.cpp")
    else()
        set(test_source "${module_slug}_tests.cpp")
    endif()
    add_executable(${target} ${test_source} test_main.cpp)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${target} PRIVATE Star::${module})
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "star-${test_name}")

    add_test(NAME ${test_name} COMMAND ${target})

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

if(TARGET StarHeaderTool)
    add_executable(StarGeneratedReflectionTests generated_reflection_tests.cpp test_main.cpp)
    target_link_libraries(StarGeneratedReflectionTests PRIVATE Star::Serialization)
    set_target_properties(StarGeneratedReflectionTests PROPERTIES OUTPUT_NAME star-generated-reflection-tests)
    star_generate_reflection(StarGeneratedReflectionTests generated_tests 37
        HEADERS fixtures/reflected_types.hpp
        INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/engine/source/runtime/reflection/include")
    add_test(NAME generated-reflection COMMAND StarGeneratedReflectionTests)
    add_test(NAME generated-reflection-incremental
        COMMAND "${CMAKE_COMMAND}"
            "-Dwork=${CMAKE_CURRENT_BINARY_DIR}/generated-reflection-incremental"
            "-Dtool=$<TARGET_FILE:StarHeaderTool>"
            "-Droot=${PROJECT_SOURCE_DIR}"
            "-Dcompiler=${CMAKE_CXX_COMPILER}"
            "-Dninja=${star_test_ninja}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/generated_reflection_incremental.cmake")
endif()

target_sources(StarAssetTests PRIVATE "${PROJECT_SOURCE_DIR}/engine/source/runtime/asset/tests/material_tests.cpp")

find_package(Threads REQUIRED)
find_package(SDL3 3.2 REQUIRED CONFIG)
target_sources(StarCoreTests PRIVATE config_tests.cpp containers_tests.cpp cvar_tests.cpp delegate_tests.cpp global_allocator_tests.cpp math_tests.cpp small_vector_tests.cpp)
target_sources(StarCoreTests PRIVATE spsc_queue_tests.cpp)
if(STAR_MIMALLOC_AVAILABLE)
    target_compile_definitions(StarCoreTests PRIVATE STAR_WITH_MIMALLOC=1)
endif()
target_link_libraries(StarCoreTests PRIVATE Threads::Threads)
target_sources(StarJobsTests PRIVATE task_graph_tests.cpp)
add_executable(StarJobsWorkerCountTests jobs_worker_count_tests.cpp test_main.cpp)
target_link_libraries(StarJobsWorkerCountTests PRIVATE Star::Jobs)
set_target_properties(StarJobsWorkerCountTests PROPERTIES OUTPUT_NAME "star-jobs-worker-count-tests")
add_test(NAME jobs-worker-count COMMAND StarJobsWorkerCountTests)
set_tests_properties(jobs-worker-count PROPERTIES ENVIRONMENT "STAR_JOBS_WORKERS=2")
target_sources(StarPlatformTests PRIVATE platform_thread_tests.cpp)
target_link_libraries(StarPlatformTests PRIVATE Threads::Threads SDL3::SDL3)

add_library(StarPlatformTestModule MODULE platform_test_module.cpp)
set_target_properties(StarPlatformTestModule PROPERTIES OUTPUT_NAME star-platform-test-module)
add_dependencies(StarPlatformTests StarPlatformTestModule)
target_compile_definitions(StarPlatformTests PRIVATE STAR_PLATFORM_TEST_MODULE="$<TARGET_FILE_NAME:StarPlatformTestModule>")
file(GENERATE OUTPUT "$<TARGET_FILE_DIR:StarPlatformTestModule>/star-platform-map.txt"
    CONTENT "Star mapping\n" NEWLINE_STYLE UNIX)
file(GENERATE OUTPUT "$<TARGET_FILE_DIR:StarPlatformTestModule>/star-platform-map-empty.txt" CONTENT "")

set_tests_properties(jobs-tests jobs-worker-count platform-tests PROPERTIES TIMEOUT 30)
set_tests_properties(platform-tests PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:StarPlatformTestModule>"
)

if(STAR_TEST_WINDOWS_VULKAN)
    if(NOT TARGET StarVulkanMesh OR NOT STAR_SPIRV_VAL)
        message(FATAL_ERROR "Windows Vulkan acceptance requires the Vulkan mesh sample, slangc and spirv-val")
    endif()

    add_test(NAME windows-vulkan-rhi COMMAND StarRHITests)
    add_test(NAME windows-vulkan-present COMMAND StarVulkanMesh --validation --frames=12)
    set_tests_properties(windows-vulkan-rhi windows-vulkan-present PROPERTIES
        LABELS "windows-vulkan"
        TIMEOUT 60
        FAIL_REGULAR_EXPRESSION "\\[error\\] Vulkan:;Validation Error:"
    )
    set(sync_validation "VK_LAYER_VALIDATE_SYNC=1")
    set_tests_properties(windows-vulkan-rhi PROPERTIES
        ENVIRONMENT "STAR_TEST_REQUIRE_VULKAN=1;STAR_TEST_VULKAN_VALIDATION=1;${sync_validation}")
    set_tests_properties(windows-vulkan-present PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=windows;${sync_validation}")
endif()

if(NOT STAR_MONOLITHIC)
    set(game_fixture_source "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/game-module")
    set(game_fixture_build "${CMAKE_CURRENT_BINARY_DIR}/game-module/$<CONFIG>")
    set(game_fixture_file "${game_fixture_build}/star-m3-game-module${CMAKE_SHARED_MODULE_SUFFIX}")
    set(game_fixture_sdk)
    foreach(module IN ITEMS Scene Core Reflection Serialization)
        string(TOLOWER "${module}" module_slug)
        string(TOUPPER "${module}" module_upper)
        get_target_property(module_source Star${module} SOURCE_DIR)
        list(APPEND game_fixture_sdk
            "-DSTAR_${module_upper}_INCLUDE=${module_source}/include"
            "-DSTAR_${module_upper}_GENERATED=${CMAKE_BINARY_DIR}/generated/${module_slug}"
            "-DSTAR_${module_upper}_LINK=$<TARGET_LINKER_FILE:Star${module}>")
    endforeach()
    add_custom_command(
        OUTPUT "${game_fixture_file}"
        COMMAND "${CMAKE_COMMAND}" -S "${game_fixture_source}" -B "${game_fixture_build}" -G Ninja
            "-DCMAKE_MAKE_PROGRAM=${star_test_ninja}"
            "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DCMAKE_BUILD_TYPE=$<CONFIG>"
            "-DSTAR_MODULE_SUFFIX=${CMAKE_SHARED_MODULE_SUFFIX}"
            ${game_fixture_sdk}
        COMMAND "${CMAKE_COMMAND}" --build "${game_fixture_build}"
        DEPENDS StarScene StarCore StarReflection StarSerialization
            "${game_fixture_source}/CMakeLists.txt"
            "${game_fixture_source}/game_module_fixture.cpp"
            "${game_fixture_source}/game_module_fixture.hpp"
        VERBATIM)
    add_custom_target(StarIndependentGameModuleFixture DEPENDS "${game_fixture_file}")
    add_executable(StarGameModuleTests game_module_tests.cpp test_main.cpp)
    target_link_libraries(StarGameModuleTests PRIVATE Star::Scene ${CMAKE_DL_LIBS})
    target_compile_definitions(StarGameModuleTests PRIVATE "STAR_GAME_MODULE_PATH=\"${game_fixture_file}\"")
    set_target_properties(StarGameModuleTests PROPERTIES OUTPUT_NAME star-game-module-tests)
    add_dependencies(StarGameModuleTests StarIndependentGameModuleFixture)
    add_test(NAME game-module-tests COMMAND StarGameModuleTests)
endif()
