find_package(Threads REQUIRED)

star_add_module(Jobs jobs
    include/jobs/external_tasks.hpp
    include/jobs/jobs.hpp
    include/jobs/task_graph.hpp
    src/external_tasks.cpp
    src/jobs.cpp
)

target_link_libraries(StarJobs PUBLIC Star::Core PRIVATE Threads::Threads)
