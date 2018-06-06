#include "WorkQueue.hh"

WorkQueue work_queue;

WorkQueue::WorkQueue(const unsigned int nthreads)
  : done_(false)
{
    addThreads(nthreads);
}

WorkQueue::~WorkQueue()
{
    stop();
}

void WorkQueue::addThreads(unsigned int nthreads)
{
    for (unsigned int i = 0; i < nthreads; ++i)
        threads_.emplace_back(std::thread(&WorkQueue::run_worker, this));
}

void WorkQueue::stop(void)
{
    done_ = true;

    work_q_.stop();

    for (unsigned int i = 0; i < threads_.size(); ++i) {
        if (threads_[i].joinable())
            threads_[i].join();
    }
}

void WorkQueue::submit(const std::function<void(void)>& item)
{
    work_q_.emplace(item);
}

void WorkQueue::submit(std::function<void(void)>&& item)
{
    work_q_.emplace(std::move(item));
}

void WorkQueue::run_worker(void)
{
    std::function<void(void)> item;

    while (!done_) {
        if (work_q_.pop(item)) {
            try {
                item();
            } catch (const std::exception &ex) {
                fprintf(stderr, "Worker caught exception: %s\n", ex.what());
            } catch (...) {
                fprintf(stderr, "Worker caught an unknown exception\n");
            }
        }
    }
}
