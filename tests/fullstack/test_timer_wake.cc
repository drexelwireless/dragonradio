// TimerQueue start-up race (found 2026-11-04 when test_arq_syn was killed by
// SIGUSR1 on a loaded host). TimerQueue::start() created the worker thread,
// which installs the SIGWAKE (SIGUSR1) handler itself; a timer scheduled
// before the worker ran sent SIGWAKE to a process with no handler, whose
// default action terminates it. A handler is process-wide and stays
// installed, so each trial runs in a fresh child process: start the queue
// and schedule a timer at once. The child is pinned to one CPU, so the new
// worker cannot run before the creating thread schedules the timer (the
// loaded-host case).
#include <sched.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdio>

#include "TimerQueue.hh"

struct Flag : public TimerQueue::Timer {
    std::atomic<bool> fired{false};
    void operator()() override { fired = true; }
};

static int trial(void)
{
    TimerQueue q;
    Flag t;

    q.start();
    q.run_in(t, 0.001);     // wakes the worker immediately
    for (int i = 0; i < 1000 && !t.fired; ++i)
        usleep(1000);
    q.stop();
    return t.fired ? 0 : 2;
}

int main()
{
    const int trials = 200;
    int killed = 0, other = 0;

    for (int i = 0; i < trials; ++i) {
        pid_t pid = fork();

        if (pid == 0) {
            cpu_set_t allowed, one;

            sched_getaffinity(0, sizeof(allowed), &allowed);
            CPU_ZERO(&one);
            for (int c = 0; c < CPU_SETSIZE; ++c)
                if (CPU_ISSET(c, &allowed)) {
                    CPU_SET(c, &one);
                    break;
                }
            sched_setaffinity(0, sizeof(one), &one);
            _exit(trial());
        }

        int status = 0;

        waitpid(pid, &status, 0);
        if (WIFSIGNALED(status))
            ++killed;
        else if (WEXITSTATUS(status) != 0)
            ++other;
    }

    std::printf("  %d trials: %d killed by a signal, %d timers not run\n", trials, killed, other);
    std::printf("%s (%d failures)\n", killed + other ? "FAILED" : "PASSED", killed + other);
    return killed + other ? 1 : 0;
}
