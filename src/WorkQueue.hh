#ifndef WORKQUEUE_H_
#define WORKQUEUE_H_

#include <functional>
#include <thread>
#include <vector>

#include "SafeQueue.hh"

class WorkQueue;

/** @brief The global work queue. */
extern WorkQueue work_queue;

/** @brief A work queue. */
class WorkQueue {
public:
    explicit WorkQueue(unsigned int nthreads = 0);

    WorkQueue(const WorkQueue&) = delete;
    WorkQueue(WorkQueue&&) = delete;

    WorkQueue& operator=(const WorkQueue&) = delete;
    WorkQueue& operator=(WorkQueue&&) = delete;

    ~WorkQueue();

    void addThreads(unsigned int nthreads);

    void stop(void);

    template <typename F, typename... Args>
    void submit(F&& f, Args&&... args)
    {
        work_q_.emplace(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    }

    void submit(const std::function<void(void)>& item);

    void submit(std::function<void(void)>&& item);

private:
    bool done_;
    std::vector<std::thread> threads_;
    SafeQueue<std::function<void(void)>> work_q_;

    void run_worker(void);
};

#endif /* WORKQUEUE_H_ */
