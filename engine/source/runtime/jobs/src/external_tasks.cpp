#include "jobs/external_tasks.hpp"

#include <memory>

namespace star {

class ExternalTaskHandle final {
  public:
    JobHandle job;
};

ExternalTaskHandle *ExternalTasks::enqueue(Callback callback, void *context, JobPriority priority) noexcept {
    if (callback == nullptr) {
        return nullptr;
    }

    try {
        auto handle = std::make_unique<ExternalTaskHandle>();
        handle->job = Jobs::run([callback, context] { callback(context); }, priority);
        return handle.release();
    } catch (...) {
        // Jobs::run throws only before accepting the callback, so inline execution is safe here.
        callback(context);
        return nullptr;
    }
}

void ExternalTasks::finish(ExternalTaskHandle *handle) noexcept {
    const std::unique_ptr<ExternalTaskHandle> task(handle);
    if (task) {
        task->job.wait();
    }
}

} // namespace star
