// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef MULTICHANNELSYNTHESIZER_H_
#define MULTICHANNELSYNTHESIZER_H_

#include "dsp/FDUpsampler.hh"
#include "phy/PHY.hh"
#include "phy/SlotSynthesizer.hh"

/** @brief A frequency-domain, per-channel synthesizer. */
class MultichannelSynthesizer : public SlotSynthesizer
{
public:
    using Upsampler = dragonradio::signal::FDUpsampler<C>;

    static constexpr auto P = Upsampler::P;
    static constexpr auto N = Upsampler::N;
    static constexpr auto L = Upsampler::L;
    static constexpr auto O = Upsampler::O;

    MultichannelSynthesizer(const std::vector<PHYChannel> &channels,
                            double tx_rate,
                            size_t nthreads);
    virtual ~MultichannelSynthesizer();

    void modulate(const std::shared_ptr<Slot> &slot) override;

    void finalize(Slot &slot) override;

    void stop(void) override;

private:
    class MultichannelModulator;

    /** @brief Number of synthesizer threads. */
    unsigned nthreads_;

    /** @brief Channel state for demodulation. */
    std::vector<std::unique_ptr<MultichannelModulator>> mods_;

    /** @brief Current slot that need to be synthesized */
    std::shared_ptr<Slot> curslot_;

    /** @brief Threads running modWorker */
    std::vector<std::thread> mod_threads_;

    /** @brief OLS time domain converter */
    Upsampler::ToTimeDomain timedomain_;

    /** @brief Gain necessary to compensate for simultaneous transmission */
    float g_multichan_;

    /** @brief Thread modulating packets */
    void modWorker(unsigned tid);

    void reconfigure(void) override;

    void wake_dependents() override;
};

#endif /* MULTICHANNELSYNTHESIZER_H_ */
