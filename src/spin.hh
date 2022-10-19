// Copyright 2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef SPIN_HH_
#define SPIN_HH_

#include <thread>

template <typename F, int max_spin_count=16>
void spin_until(F&& pred)
{
    for (;;) {
        if (pred(std::memory_order_acquire))
            break;

        for (int spin_count = 0; !pred(std::memory_order_relaxed); ++spin_count) {
            if (spin_count < max_spin_count)
                _mm_pause();
            else {
                std::this_thread::yield();
                spin_count = 0;
            }
        }
    }
}

#endif /* SPIN_HH_ */
