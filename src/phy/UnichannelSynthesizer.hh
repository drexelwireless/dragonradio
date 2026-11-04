// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef UNICHANNELSYNTHESIZER_HH_
#define UNICHANNELSYNTHESIZER_HH_

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

#include "phy/Channel.hh"
#include "phy/PHY.hh"
#include "phy/SlotSynthesizer.hh"

/** @brief A single-channel synthesizer. */
template <class ChannelModulator>
class UnichannelSynthesizer : public SlotSynthesizer
{
public:
    UnichannelSynthesizer(std::shared_ptr<PHY> phy,
                          double tx_rate,
                          const Channels &channels,
                          size_t nthreads);

    virtual ~UnichannelSynthesizer();

    void modulate(const std::shared_ptr<Slot> &slot) override;

    void reconfigure(void) override;

    void stop(void) override;

protected:
    /** @brief Flag indicating if we should stop processing packets */
    std::atomic<bool> done_;

    /** @brief Reconfiguration flags */
    std::vector<std::atomic<bool>> mod_reconfigure_;

    /** @brief Lock for current slot */
    std::mutex curslot_mutex_;

    /** @brief Condition variable for current slot */
    std::condition_variable curslot_cond_;

    /** @brief Current slot that need to be synthesized */
    std::shared_ptr<Slot> curslot_;

    /** @brief Threads running modWorker */
    std::vector<std::thread> mod_threads_;

    /** @brief Thread modulating packets */
    void modWorker(std::atomic<bool> &reconfig, unsigned tid);

    /** @brief Kicks the sink at a deadline: bounds the wait for an OFDMA
     * burst partner without changing the queues' blocking pull.
     */
    struct Kicker {
        std::mutex m;
        std::condition_variable cv;
        std::optional<std::chrono::steady_clock::time_point> deadline;
        bool stop = false;
        std::function<void(void)> kick;
        std::thread thread;

        ~Kicker() { shutdown(); }

        void run(void);
        void arm(double sec);
        void cancel(void);
        void shutdown(void);
    };

    Kicker kicker_;

    /** @brief Pull a packet, waiting at most sec (false on timeout). */
    bool pullWithin(std::shared_ptr<NetPacket> &pkt, double sec);
};

#endif /* UNICHANNELSYNTHESIZER_HH_ */
