#ifndef BARRIER_HH
#define BARRIER_HH

#include <atomic>
#include <thread>

class barrier
{
public:
    barrier(unsigned count)
      : count_(count)
      , arrived_(0)
      , generation_(0)
    {
    }

    barrier() = delete;

    ~barrier() = default;

    void wait(void)
    {
        const unsigned generation = generation_;

        if (arrived_.fetch_add(1, std::memory_order_release)+1 == count_) {
            arrived_.store(0, std::memory_order_release);
            generation_.fetch_add(1, std::memory_order_release);
        } else {
            while (generation == generation_.load(std::memory_order_acquire))
                std::this_thread::yield();
        }
    }

private:
    /** @brief Number of threads in barrier syncrhonization group */
    const unsigned count_;

    /** @brief Number of threads that have arrived at the barrier */
    std::atomic<unsigned> arrived_;

    /** @brief Current barrier generation */
    std::atomic<unsigned> generation_;
};

#endif /* BARRIER_HH */
