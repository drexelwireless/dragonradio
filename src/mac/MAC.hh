#ifndef MAC_H_
#define MAC_H_

#include <memory>

#include "USRP.hh"
#include "phy/PHY.hh"
#include "phy/PacketDemodulator.hh"
#include "phy/PacketModulator.hh"
#include "mac/MAC.hh"

/** @brief A MAC protocol. */
class MAC
{
public:
    MAC(std::shared_ptr<USRP> usrp,
        std::shared_ptr<PHY> phy,
        std::shared_ptr<PacketModulator> modulator,
        std::shared_ptr<PacketDemodulator> demodulator,
        double bandwidth);
    virtual ~MAC() = default;

    MAC() = delete;
    MAC(const MAC&) = delete;
    MAC& operator =(const MAC&) = delete;
    MAC& operator =(MAC&&) = delete;

protected:
    /** @brief Our USRP device. */
    std::shared_ptr<USRP> usrp_;

    /** @brief Our PHY. */
    std::shared_ptr<PHY> phy_;

    /** @brief Our packet modulator. */
    std::shared_ptr<PacketModulator> modulator_;

    /** @brief Our packet demodulator. */
    std::shared_ptr<PacketDemodulator> demodulator_;

    /** @brief Bandwidth */
    double bandwidth_;

    /** @brief RX rate */
    double rx_rate_;

    /** @brief TX rate */
    double tx_rate_;
};

#endif /* MAC_H_ */
