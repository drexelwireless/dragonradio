// In-process statistical CPU profiler for the P4 benchmarks.
//
// perf is not available in the build container (no binary, and
// kernel.perf_event_paranoid = 4), so this samples one thread's CPU time
// instead: a POSIX timer on CLOCK_THREAD_CPUTIME_ID delivers SIGPROF to the
// profiled thread every `period_us` of CPU it consumes, and the handler
// stores the interrupted program counter and a backtrace in a preallocated
// buffer. After the run, addresses are symbolized with dladdr and
// `addr2line -f -i -C` (inline chains, so header-only code is attributed to
// the inlined function, not only to its caller).
//
// Limits (recorded in docs/performance.rst): backtrace() unwinds with the
// libgcc unwinder, which is not formally async-signal-safe; it is warmed up
// before the timer starts and the profiled code throws no exceptions in the
// measured loops. Sampling granularity is the kernel tick for CPU-time
// timers (1 kHz on this kernel), so shares below about 1% are noise.
#ifndef BENCH_SAMPLER_HH_
#define BENCH_SAMPLER_HH_

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#ifndef sigev_notify_thread_id
#define sigev_notify_thread_id _sigev_un._tid
#endif

namespace bench {

class Sampler {
public:
    static constexpr int kDepth = 24;
    struct Sample { int depth; void *pc[kDepth]; };

    explicit Sampler(size_t capacity = 1 << 18) : samples_(capacity) {}

    // Start sampling the calling thread.
    void start(long period_us = 1000)
    {
        void *warm[4];
        backtrace(warm, 4);   // load the unwinder outside the handler
        count_.store(0);
        dropped_ = 0;
        active_ = this;
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = &Sampler::handler;
        sa.sa_flags = SA_SIGINFO | SA_RESTART;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGPROF, &sa, nullptr);
        struct sigevent sev;
        std::memset(&sev, 0, sizeof(sev));
        sev.sigev_notify = SIGEV_THREAD_ID;
        sev.sigev_signo = SIGPROF;
        sev.sigev_notify_thread_id = static_cast<pid_t>(syscall(SYS_gettid));
        if (timer_create(CLOCK_THREAD_CPUTIME_ID, &sev, &timer_) != 0) {
            std::perror("timer_create");
            std::exit(1);
        }
        struct itimerspec its;
        its.it_interval.tv_sec = period_us / 1000000;
        its.it_interval.tv_nsec = (period_us % 1000000) * 1000;
        its.it_value = its.it_interval;
        timer_settime(timer_, 0, &its, nullptr);
    }

    void stop()
    {
        struct itimerspec its;
        std::memset(&its, 0, sizeof(its));
        timer_settime(timer_, 0, &its, nullptr);
        timer_delete(timer_);
        active_ = nullptr;
    }

    size_t size() const { return std::min(count_.load(), samples_.size()); }
    const Sample &operator[](size_t i) const { return samples_[i]; }

    // Symbolized inline chains per sample, innermost function first.
    std::vector<std::vector<std::string>> symbolize() const
    {
        std::set<void *> unique;
        for (size_t i = 0; i < size(); ++i)
            for (int d = 0; d < samples_[i].depth; ++d)
                unique.insert(samples_[i].pc[d]);
        // Group by object for batched addr2line.
        struct Obj { std::string path; uintptr_t base; std::vector<void *> addrs; };
        std::map<std::string, Obj> objects;
        std::map<void *, std::vector<std::string>> names;
        Dl_info self;
        dladdr(reinterpret_cast<void *>(&Sampler::handler), &self);
        for (void *a : unique) {
            Dl_info info;
            if (!dladdr(a, &info) || !info.dli_fname) {
                names[a] = {"?"};
                continue;
            }
            std::string path = info.dli_fname;
            if (info.dli_fbase == self.dli_fbase) path = "/proc/self/exe";
            auto &o = objects[path];
            o.path = path;
            o.base = reinterpret_cast<uintptr_t>(info.dli_fbase);
            o.addrs.push_back(a);
            // Exported-symbol fallback (shared libraries without debug info).
            const std::string tag = path == "/proc/self/exe" ? "" : basename(path) + "!";
            if (info.dli_sname) names[a] = {tag + info.dli_sname};
            else names[a] = {tag + "?"};
        }
        for (auto &[path, o] : objects) {
            // Only objects with DWARF line info benefit; addr2line prints ?? otherwise.
            std::string list = "/tmp/sampler-addrs-" + std::to_string(getpid()) + ".txt";
            FILE *f = std::fopen(list.c_str(), "w");
            for (void *a : o.addrs) {
                uintptr_t off = reinterpret_cast<uintptr_t>(a) - o.base;
                std::fprintf(f, "0x%lx\n", static_cast<unsigned long>(off));
            }
            std::fclose(f);
            std::string exe = path == "/proc/self/exe" ? self_path() : path;
            std::string cmd = "addr2line -f -i -C -a -e '" + exe + "' < " + list;
            FILE *p = popen(cmd.c_str(), "r");
            if (!p) continue;
            char line[4096];
            std::vector<std::string> chain;
            size_t idx = 0;
            bool first = true;
            auto flush = [&] {
                if (!first && idx <= o.addrs.size() && idx > 0) {
                    bool known = false;
                    for (auto &c : chain) if (c != "??") known = true;
                    if (known) {
                        const std::string tag = path == "/proc/self/exe" ? "" : basename(path) + "!";
                        for (auto &c : chain) c = tag + c;
                        names[o.addrs[idx - 1]] = chain;
                    }
                }
                chain.clear();
            };
            int state = 0;   // 0: expect function, 1: expect file:line
            while (std::fgets(line, sizeof(line), p)) {
                std::string s(line);
                if (!s.empty() && s.back() == '\n') s.pop_back();
                if (s.rfind("0x", 0) == 0 && state == 0) {
                    flush();
                    first = false;
                    ++idx;
                    continue;
                }
                if (state == 0) { chain.push_back(strip(s)); state = 1; }
                else state = 0;
            }
            flush();
            pclose(p);
            std::remove(list.c_str());
        }
        std::vector<std::vector<std::string>> out(size());
        for (size_t i = 0; i < size(); ++i)
            for (int d = 0; d < samples_[i].depth; ++d) {
                const auto &n = names[samples_[i].pc[d]];
                out[i].insert(out[i].end(), n.begin(), n.end());
            }
        return out;
    }

private:
    static std::string strip(const std::string &s)
    {
        // Drop argument lists and templates for readable function names.
        std::string r;
        int depth = 0;
        for (char c : s) {
            if (c == '(' || c == '<') { if (depth++ == 0 && c == '(') break; continue; }
            if (c == ')' || c == '>') { if (depth > 0) --depth; continue; }
            if (depth == 0) r += c;
        }
        return r.empty() ? s : r;
    }
    static std::string basename(const std::string &p)
    {
        auto k = p.rfind('/');
        return k == std::string::npos ? p : p.substr(k + 1);
    }
    static std::string self_path()
    {
        char buf[4096];
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n <= 0) return "/proc/self/exe";
        buf[n] = 0;
        return buf;
    }

    static void handler(int, siginfo_t *, void *uc)
    {
        Sampler *s = active_;
        if (!s) return;
        size_t i = s->count_.fetch_add(1);
        if (i >= s->samples_.size()) { ++s->dropped_; return; }
        Sample &out = s->samples_[i];
        auto *u = static_cast<ucontext_t *>(uc);
        void *frames[kDepth + 3];
        int n = backtrace(frames, kDepth + 3);
        // frames[0] is this handler, frames[1] the signal trampoline, then
        // the interrupted function (its return address is the PC itself).
        out.pc[0] = reinterpret_cast<void *>(u->uc_mcontext.gregs[REG_RIP]);
        int d = 1;
        for (int k = 3; k < n && d < kDepth; ++k)
            out.pc[d++] = static_cast<char *>(frames[k]) - 1;   // call site, not return address
        out.depth = d;
    }

    std::vector<Sample> samples_;
    std::atomic<size_t> count_{0};
    size_t dropped_ = 0;
    timer_t timer_{};
    static inline Sampler *active_ = nullptr;
};

}  // namespace bench

#endif  // BENCH_SAMPLER_HH_
