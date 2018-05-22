#ifndef SPINLOCK_MUTEX_HH
#define SPINLOCK_MUTEX_HH

#include <atomic>

class spinlock_mutex
{
public:
    spinlock_mutex() : flag_(ATOMIC_FLAG_INIT) {}

    void lock(void)
    {
        while(flag_.test_and_set(std::memory_order_acquire))
            ;
    }

    void unlock(void)
    {
        flag_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag_;
};

#endif /* SPINLOCK_MUTEX_HH */
