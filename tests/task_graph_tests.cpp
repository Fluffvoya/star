#include "test_support.hpp"

#include "jobs/task_graph.hpp"

#include <memory>
#include <stdexcept>

namespace {

STAR_TEST(taskGraphRunsDiamondDependenciesAndCanBeReused) {
    star::TaskGraph graph;
    int root_value = 0;
    int left_value = 0;
    int right_value = 0;
    int result = 0;

    const auto root = graph.addTask([&, step = std::make_unique<int>(4)] {
        root_value += *step;
        return true;
    });
    const auto left = graph.addTask([&] {
        left_value = root_value + 1;
        return true;
    });
    const auto right = graph.addTask(
        [&] {
            right_value = root_value + 2;
            return true;
        },
        star::JobPriority::High);
    const auto join = graph.addTask([&] {
        result = left_value + right_value;
        return true;
    });
    STAR_CHECK(root && left && right && join);
    STAR_CHECK(graph.addDependency(*root, *left));
    STAR_CHECK(graph.addDependency(*root, *right));
    STAR_CHECK(graph.addDependency(*left, *join));
    STAR_CHECK(graph.addDependency(*right, *join));
    STAR_CHECK(graph.addDependency(*right, *join));

    STAR_CHECK(graph.run());
    STAR_CHECK(result == 11);
    STAR_CHECK(graph.run());
    STAR_CHECK(result == 19);
}

STAR_TEST(taskGraphRejectsForeignTasksAndCyclesBeforeRunning) {
    star::TaskGraph graph;
    star::TaskGraph other;
    int executions = 0;
    const auto task = [&] {
        ++executions;
        return true;
    };
    const auto first = graph.addTask(task);
    const auto second = graph.addTask(task);
    const auto unrelated = graph.addTask(task);
    const auto foreign = other.addTask(task);
    STAR_CHECK(first && second && unrelated && foreign);

    const auto foreign_error = graph.addDependency(*first, *foreign);
    STAR_CHECK(!foreign_error && foreign_error.error() == star::TaskGraphError::InvalidTask);
    const auto loop = graph.addDependency(*first, *first);
    STAR_CHECK(!loop && loop.error() == star::TaskGraphError::Cycle);

    STAR_CHECK(graph.addDependency(*first, *second));
    STAR_CHECK(graph.addDependency(*second, *first));
    const auto result = graph.run();
    STAR_CHECK(!result && result.error() == star::TaskGraphError::Cycle);
    STAR_CHECK(executions == 0);
}

STAR_TEST(taskGraphPropagatesFailuresAndDrainsIndependentWork) {
    star::TaskGraph graph;
    int skipped = 0;
    bool independent_finished = false;
    const auto failing = graph.addTask([] { return false; });
    const auto throwing = graph.addTask([]() -> bool { throw std::runtime_error("task failure"); });
    const auto independent = graph.addTask([&] {
        independent_finished = true;
        return true;
    });
    const auto should_skip = [&] {
        ++skipped;
        return true;
    };
    const auto blocked = graph.addTask(should_skip);
    const auto descendant = graph.addTask(should_skip);
    STAR_CHECK(failing && throwing && independent && blocked && descendant);
    STAR_CHECK(graph.addDependency(*failing, *blocked));
    STAR_CHECK(graph.addDependency(*independent, *blocked));
    STAR_CHECK(graph.addDependency(*blocked, *descendant));
    STAR_CHECK(graph.addDependency(*throwing, *descendant));

    const auto result = graph.run();
    STAR_CHECK(!result && result.error() == star::TaskGraphError::TaskFailed);
    STAR_CHECK(independent_finished);
    STAR_CHECK(skipped == 0);
}

STAR_TEST(taskGraphSupportsNestedSchedulersAndRejectsReentrantMutation) {
    star::TaskGraph empty;
    STAR_CHECK(empty.run());
    const auto invalid_task = empty.addTask({});
    STAR_CHECK(!invalid_task && invalid_task.error() == star::TaskGraphError::InvalidTask);

    bool finished = false;
    auto parent = star::Jobs::run([&] {
        star::TaskGraph graph;
        const auto task = graph.addTask([&] {
            const auto nested = graph.run();
            const auto mutation = graph.addTask([] { return true; });
            if (nested || nested.error() != star::TaskGraphError::Busy || mutation ||
                mutation.error() != star::TaskGraphError::Busy) {
                return false;
            }
            auto job = star::Jobs::run([&] { finished = true; });
            job.wait();
            return finished;
        });
        finished = task && graph.run() && finished;
    });
    parent.wait();
    STAR_CHECK(finished);
}

} // namespace
