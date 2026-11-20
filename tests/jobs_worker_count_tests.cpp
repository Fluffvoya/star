#include "test_support.hpp"

#include "jobs/jobs.hpp"

STAR_TEST(configuredWorkerCountIsUsedAtSchedulerStartup) { STAR_CHECK(star::Jobs::getWorkerCount() == 2); }
