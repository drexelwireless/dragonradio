#ifndef TDMA_H_
#define TDMA_H_

#include <liquid/liquid.h>

#include <vector>
#include <complex>

#include "NET.hh"
#include "PacketDemodulator.hh"
#include "PacketModulator.hh"
#include "USRP.hh"
#include "phy/PHY.hh"
#include "mac/MAC.hh"

/** @brief A TDMA MAC. */
class TDMA : public MAC
{
public:
    TDMA(std::shared_ptr<USRP> usrp,
         std::shared_ptr<NET> net,
         std::shared_ptr<PHY> phy,
         std::shared_ptr<PacketModulator> modulator,
         std::shared_ptr<PacketDemodulator> demodulator,
         double bandwidth,
         double slot_size,
         double guard_size);
    virtual ~TDMA();

    void stop(void) override;

private:
    /** @brief Our USRP device. */
    std::shared_ptr<USRP> _usrp;

    /** @brief The network we interact with. */
    std::shared_ptr<NET> _net;

    /** @brief Our PHY. */
    std::shared_ptr<PHY> _phy;

    /** @brief Our packet modulator. */
    std::shared_ptr<PacketModulator> _modulator;

    /** @brief Our packet demodulator. */
    std::shared_ptr<PacketDemodulator> _demodulator;

    /** @brief Bandwidth */
    double _bandwidth;

    /** @brief RX rate */
    double _rx_rate;

    /** @brief TX rate */
    double _tx_rate;

    /** @brief Length of TDMA frame (sec) */
    double _frame_size;

    /** @brief Length of a single TDMA slot, *including* guard (sec) */
    double _slot_size;

    /** @brief Length of inter-slot guard (sec) */
    double _guard_size;

    /** @brief Flag indicating if we should stop processing packets */
    bool _done;

    /** @brief Thread running rxWorker */
    std::thread _rxThread;

    /** @brief Worker receiving packets */
    void rxWorker(void);

    /** @brief Thread running txWorker */
    std::thread _txThread;

    /** @brief Worker transmitting packets */
    void txWorker(void);

    /** @brief Transmit one slot's worth of samples */
    void txSlot(Clock::time_point when, size_t maxSamples);
};

#endif /* TDMA_H_ */
