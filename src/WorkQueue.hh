#ifndef WORKQUEUE_H_
#define WORKQUEUE_H_

#include <functional>
#include <future>
#include <thread>
#include <type_traits>
#include <vector>

#include "SafeQueue.hh"

/** A work queue. */
template <typename W, typename T>
class WorkQueue {
public:
    template <typename... WArgs>
    explicit WorkQueue(const unsigned int nthreads, WArgs&&... args);

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

template <typename W, typename T>
template <typename... WArgs>
WorkQueue<W, T>::WorkQueue(const unsigned int nthreads, WArgs&&... args) :
    done(false)
{
    for (unsigned int i = 0; i < nthreads; ++i) {
        workers.emplace_back(std::make_unique<W>(std::forward<WArgs>(args)...));
        threads.emplace_back(std::thread(&WorkQueue<W, T>::run_worker, this, std::ref(*workers.back())));
    }
}

template <typename W, typename T>
WorkQueue<W, T>::~WorkQueue()
{
}

template <typename W, typename T>
void WorkQueue<W, T>::stop(void)
{
    done = true;

    work_q.stop();

    for (unsigned int i = 0; i < threads.size(); ++i) {
        if (threads[i].joinable())
            threads[i].join();
    }
}

template <typename W, typename T>
void WorkQueue<W, T>::submit(const T& item)
{
    work_q.emplace(item);
}

template <typename W, typename T>
void WorkQueue<W, T>::submit(T&& item)
{
    work_q.emplace(std::move(item));
}

template <typename W, typename T>
void WorkQueue<W, T>::run_worker(W& worker)
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
