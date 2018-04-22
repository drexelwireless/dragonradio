#ifndef WORKQUEUE_H_
#define WORKQUEUE_H_

#include <functional>
#include <future>
#include <thread>
#include <type_traits>
#include <vector>

#include "SafeQueue.hh"

/** A work queue. */
template <typename T, typename W>
class WorkQueue {
public:
    template <typename F, typename... Args>
    explicit WorkQueue(const unsigned int nthreads, F&& f, Args&&... args);

    WorkQueue(const WorkQueue&) = delete;
    WorkQueue(WorkQueue&&) = delete;

    WorkQueue& operator=(const WorkQueue&) = delete;
    WorkQueue& operator=(WorkQueue&&) = delete;

    ~WorkQueue();

    void stop(void);

    void submit(const T& item);
    void submit(T&& item);

private:
    bool done;
    std::vector<std::unique_ptr<W>> workers;
    std::vector<std::thread> threads;
    SafeQueue<T> work_q;

    void run_worker(W& worker);
};

template <typename T, typename W>
template <typename F, typename... Args>
WorkQueue<T, W>::WorkQueue(const unsigned int nthreads, F&& f, Args&&... args) :
    done(false)
{
    for (unsigned int i = 0; i < nthreads; ++i) {
#if __cplusplus >= 201703L
        workers.emplace_back(std::invoke(std::forward<F>(f), std::forward<Args>(args)...));
#else /*  __cplusplus < 201703 */
        workers.emplace_back(std::forward<F>(f)(std::forward<Args>(args)...));
#endif /*  __cplusplus < 201703 */
        threads.emplace_back(std::thread(&WorkQueue<T, W>::run_worker, this, std::ref(*workers.back())));
    }
}

template <typename T, typename W>
WorkQueue<T, W>::~WorkQueue()
{
}

template <typename T, typename W>
void WorkQueue<T, W>::stop(void)
{
    done = true;

    work_q.stop();

    for (unsigned int i = 0; i < threads.size(); ++i) {
        if (threads[i].joinable())
            threads[i].join();
    }
}

template <typename T, typename W>
void WorkQueue<T, W>::submit(const T& item)
{
    work_q.emplace(item);
}

template <typename T, typename W>
void WorkQueue<T, W>::submit(T&& item)
{
    work_q.emplace(std::move(item));
}

template <typename T, typename W>
void WorkQueue<T, W>::run_worker(W& worker)
{
    T item;

    while (!done) {
        work_q.pop(item);
        if (done)
            break;

        worker(item);
    }
}

#endif /* WORKQUEUE_H_ */
