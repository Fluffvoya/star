#pragma once

#include "jobs/jobs.hpp"

namespace star {

class ExternalTaskHandle;

class STAR_JOBS_API ExternalTasks final {
  public:
    using Callback = void (*)(void *context) noexcept;

    // Enqueues a callback on Jobs. A null return means the callback completed inline or was null.
    // The context must remain valid until finish() returns. Callbacks may run on any waiting thread.
    [[nodiscard]] static ExternalTaskHandle *enqueue(Callback callback, void *context,
                                                     JobPriority priority = JobPriority::High) noexcept;

    // Consumes the handle and waits for its callback. A null handle is allowed.
    // Call once for each non-null handle and never wait on a task from its own callback.
    static void finish(ExternalTaskHandle *handle) noexcept;
};

} // namespace star
