find_package(Threads REQUIRED)

star_add_module(Jobs jobs
    include/jobs/jobs.hpp
    src/jobs.cpp
)

target_link_libraries(StarJobs PUBLIC Star::Core PRIVATE Threads::Threads)
