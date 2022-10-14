// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "WorkQueue.hh"

WorkQueue work_queue;

WorkQueue::WorkQueue(const unsigned int nthreads)
{
    done_.store(false, std::memory_order_release);

    addThreads(nthreads);
}

WorkQueue::~WorkQueue()
{
    stop();
}

void WorkQueue::addThreads(unsigned int nthreads)
{
    for (unsigned int i = 0; i < nthreads; ++i)
        threads_.emplace_back(&WorkQueue::run_worker, this);
}

void WorkQueue::stop(void)
{
    done_.store(true, std::memory_order_release);

    work_q_.disable();

    for (unsigned int i = 0; i < threads_.size(); ++i) {
        if (threads_[i].joinable())
            threads_[i].join();
    }
}

void WorkQueue::run_worker(void)
{
    std::function<void(void)> item;

    while (!done_.load(std::memory_order_acquire)) {
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
