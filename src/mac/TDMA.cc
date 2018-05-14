#include <uhd/utils/thread_priority.hpp>

#include "Clock.hh"
#include "Logger.hh"
#include "USRP.hh"
#include "Util.hh"
#include "mac/TDMA.hh"

TDMA::TDMA(std::shared_ptr<USRP> usrp,
           std::shared_ptr<PHY> phy,
           std::shared_ptr<PacketModulator> modulator,
           std::shared_ptr<PacketDemodulator> demodulator,
           double bandwidth,
           size_t nslots,
           double slot_size,
           double guard_size)
  : usrp_(usrp),
    phy_(phy),
    modulator_(modulator),
    demodulator_(demodulator),
    bandwidth_(bandwidth),
    slot_size_(slot_size),
    guard_size_(guard_size),
    done_(false)
{
    slots_.resize(nslots, false);

    rx_rate_ = bandwidth_*phy->getRXRateOversample();
    tx_rate_ = bandwidth_*phy->getTXRateOversample();

    usrp->setRXRate(rx_rate_);
    usrp->setTXRate(tx_rate_);

    phy->setRXRate(rx_rate_);
    phy->setTXRate(tx_rate_);

    if (logger) {
        logger->setAttribute("tx_bandwidth", tx_rate_);
        logger->setAttribute("rx_bandwidth", rx_rate_);
    }

    rx_thread_ = std::thread(&TDMA::rxWorker, this);
    tx_thread_ = std::thread(&TDMA::txWorker, this);

    reconfigure();
}

TDMA::~TDMA()
{
    stop();
}

double TDMA::getSlotSize(void)
{
    return slot_size_;
}

void TDMA::setSlotSize(double t)
{
    slot_size_ = t;
    reconfigure();
}

double TDMA::getGuardSize(void)
{
    return guard_size_;
}

void TDMA::setGuardSize(double t)
{
    guard_size_ = t;
    reconfigure();
}

TDMA::slots_type::size_type TDMA::size(void)
{
    return slots_.size();
}

void TDMA::resize(TDMA::slots_type::size_type n)
{
    slots_.resize(n, false);
    reconfigure();
}

TDMA::slots_type::reference TDMA::operator [](TDMA::slots_type::size_type i)
{
    return slots_.at(i);
}

TDMA::slots_type::iterator TDMA::begin(void)
{
    return slots_.begin();
}

TDMA::slots_type::iterator TDMA::end(void)
{
    return slots_.end();
}

void TDMA::stop(void)
{
    done_ = true;

    if (rx_thread_.joinable())
        rx_thread_.join();

    if (tx_thread_.joinable())
        tx_thread_.join();
}

void TDMA::rxWorker(void)
{
    Clock::time_point t_now;        // Current time
    Clock::time_point t_cur_slot;   // Time at which current slot starts
    Clock::time_point t_next_slot;  // Time at which next slot starts
    double            t_slot_pos;   // Offset into the current slot (sec)
    int               slot;         // Curent slot index in the frame

    uhd::set_thread_priority_safe();

    while (!done_) {
        // Set up streaming starting at *next* slot
        t_now = Clock::now();
        t_slot_pos = fmod(t_now.get_real_secs(), slot_size_);
        t_next_slot = t_now + slot_size_ - t_slot_pos;
        slot = fmod(t_now.get_real_secs(), frame_size_) / slot_size_;

        usrp_->startRXStream(t_next_slot);

        while (!done_) {
            // Update times
            t_now = Clock::now();
            t_cur_slot = t_next_slot;
            t_next_slot += slot_size_;

            // Read samples for current slot
            auto curSlot = std::make_shared<IQBuf>(rx_slot_samps_ + usrp_->getMaxRXSamps());

            demodulator_->push(curSlot);

            usrp_->burstRX(t_cur_slot, rx_slot_samps_, *curSlot);

            // Move to the next slot
            ++slot;
        }

        usrp_->stopRXStream();
    }
}

void TDMA::txWorker(void)
{
    Clock::time_point t_now;       // Current time
    Clock::time_point t_next_slot; // Time at which our next slot starts

    uhd::set_thread_priority_safe();

    while (!done_) {
        // Figure out when our next send slot is.
        t_now = Clock::now();

        if (!findNextSlot(t_now, t_next_slot)) {
            // Sleep for 100ms if we don't yet have a slot
            doze(100e-3);
            continue;
        }

        // Schedule transmission for start of our slot
        txSlot(t_next_slot, tx_slot_samps_);

        // Sleep until the end of our transmission
        doze((t_next_slot - t_now).get_real_secs() + slot_size_);
    }
}

bool TDMA::findNextSlot(Clock::time_point t, Clock::time_point &t_next)
{
    double t_secs = t.get_real_secs(); // Time t in seconds
    double t_slot_pos;                 // Offset into the current slot (sec)
    size_t cur_slot;                   // Current slot index
    size_t tx_slot;                    // Slots before we can TX

    t_slot_pos = fmod(t_secs, slot_size_);
    cur_slot = fmod(t_secs, frame_size_) / slot_size_;

    for (tx_slot = 1; tx_slot <= slots_.size(); ++tx_slot) {
        if (slots_[(cur_slot + tx_slot) % slots_.size()]) {
            t_next = t + (tx_slot*slot_size_ - t_slot_pos);
            return true;
        }
    }

    return false;
}

void TDMA::txSlot(Clock::time_point when, size_t maxSamples)
{
    std::list<std::shared_ptr<IQBuf>>     txBuf;
    std::list<std::unique_ptr<ModPacket>> modBuf;

    modulator_->pop(modBuf, maxSamples);

    if (!modBuf.empty()) {
        for (auto it = modBuf.begin(); it != modBuf.end(); ++it) {
            if (logger) {
                Header hdr;

                hdr.pkt_id = (*it)->pkt->pkt_id;
                hdr.src = (*it)->pkt->src;
                hdr.dest = (*it)->pkt->dest;

                logger->logSend(when, hdr, (*it)->samples);
            }

            txBuf.emplace_back(std::move((*it)->samples));
        }

        usrp_->burstTX(when, txBuf);
    }
}

void TDMA::reconfigure(void)
{
    frame_size_ = slot_size_*slots_.size();
    rx_slot_samps_ = rx_rate_*slot_size_;
    tx_slot_samps_ = tx_rate_*(slot_size_ - guard_size_);

    modulator_->setLowWaterMark(tx_slot_samps_);

    demodulator_->setWindowParameters(0.5*guard_size_*rx_rate_,
                                      (slot_size_ - 0.5*guard_size_)*tx_rate_);
}
