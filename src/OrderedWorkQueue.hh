#ifndef ORDEREDWORKQUEUE_H_
#define ORDEREDWORKQUEUE_H_

#include <functional>
#include <future>
#include <thread>
#include <vector>

#include "SafeQueue.hh"

/** A work queue where work results are made available in the order in which
  * the corresponding work tasks are added to the queue.
  */
template <typename T, typename U, typename W>
class OrderedWorkQueue {
public:
    template <typename F, typename... Args>
    explicit OrderedWorkQueue(const unsigned int nthreads, F&& f, Args&&... args);

    OrderedWorkQueue(const OrderedWorkQueue&) = delete;
    OrderedWorkQueue(OrderedWorkQueue&&) = delete;

    OrderedWorkQueue& operator=(const OrderedWorkQueue&) = delete;
    OrderedWorkQueue& operator=(OrderedWorkQueue&&) = delete;

    ~OrderedWorkQueue();

    void stop(void);

    void submit(const T& item);
    void submit(T&& item);

private:
    bool done;

    std::mutex m;
    std::condition_variable work_cond;
    std::condition_variable result_cond;
    std::queue<std::pair<T, std::promise<U>>> work_q;
    std::queue<std::future<U>> result_q;

    std::vector<std::unique_ptr<W>> workers;
    std::vector<std::thread> threads;

    void run_worker(W& worker);

    std::thread resultThread;

    void result_worker(void);

    bool empty_work(void) const;
    bool empty_result(void) const;

    void pop_work(std::pair<T, std::promise<U>>& val);
    void pop_result(std::future<U>& f);
};

template <typename T, typename U, typename W>
template <typename F, typename... Args>
OrderedWorkQueue<T, U, W>::OrderedWorkQueue(const unsigned int nthreads, F&& f, Args&&... args) :
    done(false)
{
    resultThread = std::thread(&OrderedWorkQueue<T, U, W>::result_worker, this);

    for (unsigned int i = 0; i < nthreads; ++i) {
        // XXX should use std::invoke here, but GCC 5.4 doesn't yet support it...
        workers.emplace_back(std::forward<F>(f)(std::forward<Args>(args)...));
        threads.emplace_back(std::thread(&OrderedWorkQueue<T, U, W>::run_worker, this, std::ref(*workers.back())));
    }
}

template <typename T, typename U, typename W>
OrderedWorkQueue<T, U, W>::~OrderedWorkQueue()
{
}

template <typename T, typename U, typename W>
void OrderedWorkQueue<T, U, W>::stop(void)
{
    done = true;

    work_cond.notify_all();
    result_cond.notify_all();

    if (resultThread.joinable())
        resultThread.join();

    for (unsigned int i = 0; i < threads.size(); ++i) {
        if (threads[i].joinable())
            threads[i].join();
    }
}

template <typename T, typename U, typename W>
void OrderedWorkQueue<T, U, W>::submit(const T& item)
{
    std::lock_guard<std::mutex> lock(m);

    work_q.emplace(item, std::promise<U>());
    result_q.emplace(work_q.back().second.get_future());
    work_cond.notify_one();
    result_cond.notify_one();
}

template <typename T, typename U, typename W>
void OrderedWorkQueue<T, U, W>::submit(T&& item)
{
    std::lock_guard<std::mutex> lock(m);

    work_q.emplace(std::move(item), std::promise<U>());
    result_q.emplace(work_q.back().second.get_future());
    work_cond.notify_one();
    result_cond.notify_one();
}

template <typename T, typename U, typename W>
void OrderedWorkQueue<T, U, W>::run_worker(W& worker)
{
    std::pair<T, std::promise<U>> item;

    while (!done) {
        pop_work(item);
        if (done)
            break;

        item.second.set_value(worker(item.first));
    }
}

template <typename T, typename U, typename W>
void OrderedWorkQueue<T, U, W>::result_worker(void)
{
    std::future<U> item;

    while (!done) {
        pop_result(item);
        if (done)
            break;

        W::result(item.get());
    }
}

template <typename T, typename U, typename W>
bool OrderedWorkQueue<T, U, W>::empty_work(void) const
{
    std::lock_guard<std::mutex> lock(m);

    return work_q.empty();
}

template <typename T, typename U, typename W>
bool OrderedWorkQueue<T, U, W>::empty_result(void) const
{
    std::lock_guard<std::mutex> lock(m);

    return result_q.empty();
}

template <typename T, typename U, typename W>
void OrderedWorkQueue<T, U, W>::pop_work(std::pair<T, std::promise<U>>& val)
{
    std::unique_lock<std::mutex> lock(m);

    work_cond.wait(lock, [this]{ return done || !work_q.empty(); });
    if (done)
        return;
    val = std::move(work_q.front());
    work_q.pop();
}

template <typename T, typename U, typename W>
void OrderedWorkQueue<T, U, W>::pop_result(std::future<U>& f)
{
    std::unique_lock<std::mutex> lock(m);

    result_cond.wait(lock, [this]{ return done || !result_q.empty(); });
    if (done)
        return;
    f = std::move(result_q.front());
    result_q.pop();
}

#endif /* ORDEREDWORKQUEUE_H_ */
