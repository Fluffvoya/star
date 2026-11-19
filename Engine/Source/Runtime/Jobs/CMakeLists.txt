star_add_module(
    Jobs
    TYPE RUNTIME
    DEPENDS_PUBLIC Core
)

target_sources(
    StarJobs
    PRIVATE
        Public/Jobs/Jobs.hpp
)
