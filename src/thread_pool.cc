#include "thread_pool.h"

#include <cerrno>
#include <cstdint>
#include <system_error>

#include <sys/eventfd.h>
#include <unistd.h>


namespace siren {

void
ThreadPool::initialize()
{
    eventFD_ = eventfd(0, 0);

    if (eventFD_ < 0) {
        throw std::system_error(errno, std::system_category(), "eventfd() failed");
    }
}


void
ThreadPool::finalize()
{
    if (close(eventFD_) < 0 && errno != EINTR) {
        throw std::system_error(errno, std::system_category(), "close() failed");
    }
}


void
ThreadPool::worker()
{
    for (;;) {
        ThreadPoolTask *task;

        {
            std::unique_lock<std::mutex> uniqueLock(mutexes_[0]);

            while (pendingTaskList_.isEmpty()) {
                conditionVariable_.wait(uniqueLock);
            }

            task = static_cast<ThreadPoolTask *>(pendingTaskList_.getHead());

            if (task == &noTask_) {
                return;
            } else {
                task->remove();
            }
        }

        try {
            task->procedure_();
        } catch (...) {
            task->exception_ = std::current_exception();
        }

        {
            std::unique_lock<std::mutex> uniqueLock(mutexes_[1]);
            completedTaskList_.appendNode(task);
        }

        task->state_.store(TaskState::Completed, std::memory_order_release);

        for (;;) {
            std::uint64_t dummy = 1;

            if (write(eventFD_, &dummy, sizeof(dummy)) < 0) {
                if (errno != EINTR) {
                    throw std::system_error(errno, std::system_category(), "write() failed");
                }
            } else {
                break;
            }
        }
    }
}

}
