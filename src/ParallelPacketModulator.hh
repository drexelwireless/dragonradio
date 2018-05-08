#ifndef PARALLELPACKETMODULATOR_H_
#define PARALLELPACKETMODULATOR_H_

#include <condition_variable>
#include <mutex>
#include <queue>

#include "NET.hh"
#include "PacketModulator.hh"
#include "phy/PHY.hh"

/** @brief A parallel packet modulator. */
class ParallelPacketModulator : public PacketModulator
{
public:
    ParallelPacketModulator(std::shared_ptr<NET> net,
                            std::shared_ptr<PHY> phy,
                            size_t nthreads);
    virtual ~ParallelPacketModulator();

    size_t getWatermark(void) override;

    void setWatermark(size_t watermark) override;

    void pop(std::list<std::unique_ptr<ModPacket>>& pkts, size_t maxSamples);

    /** @brief Get the data validity check used by the flexframe. */
    crc_scheme get_check(void)
    {
        return _check;
    }

    /** @brief Set the data validity check used by the flexframe. */
    void set_check(crc_scheme check)
    {
        _check = check;
    }

    /** @brief Get the inner FEC used by the flexframe. */
    fec_scheme get_fec0(void)
    {
        return _fec0;
    }

    /** @brief Set the inner FEC used by the flexframe. */
    void set_fec0(fec_scheme fec0)
    {
        _fec0 = fec0;
    }

    /** @brief Get the outer FEC used by the flexframe. */
    fec_scheme get_fec1(void)
    {
        return _fec1;
    }

    /** @brief Set the outer FEC used by the flexframe. */
    void set_fec1(fec_scheme fec1)
    {
        _fec1 = fec1;
    }

    /** @brief Get the modulation scheme used by the flexframe. */
    modulation_scheme get_mod_scheme(void)
    {
        return _ms;
    }

    /** @brief Set the modulation scheme used by the flexframe. */
    void set_mod_scheme(modulation_scheme ms)
    {
        _ms = ms;
    }

    /** @brief Get soft TX gain (dB). */
    float getSoftTXGain(void)
    {
        return 20.0*logf(_g)/logf(10.0);
    }

    /** @brief Set soft TX gain.
     * @param dB The soft gain (dB).
     */
    void setSoftTXGain(float dB)
    {
        _g = powf(10.0f, dB/20.0f);
    }

    /** @brief Stop modulating. */
    void stop(void);

private:
    /** @brief Our network. */
    std::shared_ptr<NET> net;

    /** @brief Our PHY. */
    std::shared_ptr<PHY> phy;

    // PHY parameters
    crc_scheme        _check;
    fec_scheme        _fec0;
    fec_scheme        _fec1;
    modulation_scheme _ms;

    /** @brief Soft TX gain */
    float _g;

    /** @brief Flag indicating if we should stop processing packets */
    bool done;

    /** @brief Thread running modWorker */
    std::vector<std::thread> mod_threads;

    /** @brief Thread modulating packets */
    void mod_worker(void);

    /** @brief Number of modulated samples we want to have on-hand at all times. */
    size_t watermark;

    /** @brief Number of modulated samples we have */
    size_t nsamples;

    /** @brief Mutex to serialize access to the network */
    std::mutex net_mutex;

    /* @brief Mutex protecting queue of modulated packets */
    std::mutex m;

    /* @brief Condition variable used to signal modulation workers */
    std::condition_variable prod;

    /* @brief Queue of modulated packets */
    std::queue<std::unique_ptr<ModPacket>> q;
};

#endif /* PARALLELPACKETMODULATOR_H_ */
