// Per-thread work-time accounting (P4 observability). See WorkMeter.hh.
#include <mutex>

#include "util/WorkMeter.hh"

namespace {

std::mutex &registryMutex(void)
{
    static std::mutex m;
    return m;
}

std::map<std::string, std::shared_ptr<std::atomic<int64_t>>> &registry(void)
{
    static std::map<std::string, std::shared_ptr<std::atomic<int64_t>>> r;
    return r;
}

}

WorkMeter::WorkMeter(const std::string &name)
{
    std::lock_guard<std::mutex> lock(registryMutex());
    auto &slot = registry()[name];

    if (!slot)
        slot = std::make_shared<std::atomic<int64_t>>(0);
    ns_ = slot;
}

std::map<std::string, double> WorkMeter::snapshot(void)
{
    std::lock_guard<std::mutex> lock(registryMutex());
    std::map<std::string, double> out;

    for (const auto &[name, ns] : registry())
        out[name] = ns->load(std::memory_order_relaxed)*1e-9;
    return out;
}
