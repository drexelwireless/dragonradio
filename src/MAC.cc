// DWSL - full radio stack

#include <errno.h>

#include <cstring>
#include <deque>

#include <uhd/utils/thread_priority.hpp>

#include "Clock.hh"
#include "MAC.hh"
#include "USRP.hh"

MAC::MAC(std::shared_ptr<USRP> usrp,
         std::shared_ptr<NET> net,
         std::shared_ptr<PHY> phy,
         std::shared_ptr<Logger> logger,
         double bandwidth,
         double frame_size,
         double guard_size,
         size_t rx_pool_size)
  : usrp(usrp),
    net(net),
    logger(logger),
    modQueue(net, phy),
    demodQueue(net, phy, logger, false, rx_pool_size),
    _bandwidth(bandwidth),
    frame_size(frame_size),
    slot_size(frame_size/net->getNumNodes()),
    guard_size(guard_size),
    done(false)
{
    usrp->set_rx_rate(_bandwidth*phy->getRxRateOversample());
    usrp->set_tx_rate(_bandwidth*phy->getTxRateOversample());

    phy->setRxRate(_bandwidth*phy->getRxRateOversample());
    phy->setTxRate(_bandwidth*phy->getTxRateOversample());

    rxThread = std::thread(&MAC::rxWorker, this);
    txThread = std::thread(&MAC::txWorker, this);

    demodQueue.setDemodParameters(0.5*guard_size*bandwidth*phy->getRxRateOversample(),
                                  (slot_size - 0.5*guard_size)*bandwidth*phy->getRxRateOversample());
}

MAC::~MAC()
{
}

void MAC::stop(void)
{
    done = true;

    if (rxThread.joinable())
        rxThread.join();

    if (txThread.joinable())
        txThread.join();

    modQueue.stop();
    demodQueue.stop();
}

void MAC::rxWorker(void)
{
    Clock::time_point t_now;        // Current time
    Clock::time_point t_cur_slot;   // Time at which current slot starts
    Clock::time_point t_next_slot;  // Time at which next slot starts
    double            t_slot_pos;   // Offset into the current slot (sec)
    size_t            slot_samps;   // Number of samples in a slot
    int               slot;         // Curent slot index in the frame
    double            txRate;       // TX rate in Hz

    uhd::set_thread_priority_safe();

    txRate = usrp->get_rx_rate();
    slot_samps = txRate * slot_size;

    while (!done) {
        // Set up streaming starting at *next* slot
        t_now = Clock::now();
        t_slot_pos = fmod(t_now.get_real_secs(), slot_size);
        t_next_slot = t_now + slot_size - t_slot_pos;
        slot = fmod(t_now.get_real_secs(), frame_size) / slot_size;

        usrp->startRXStream(t_next_slot);

        while (!done) {
            // Update times
            t_now = Clock::now();
            t_cur_slot = t_next_slot;
            t_next_slot += slot_size;

            // Read samples for current slot
            auto curSlot = std::make_shared<IQBuf>(slot_samps + USRP::MAXSAMPS);

            demodQueue.push(curSlot);

            usrp->burstRX(t_cur_slot, slot_samps, *curSlot);

            // Move to the next slot
            ++slot;
        }

        usrp->stopRXStream();
    }
}

void MAC::txWorker(void)
{
    Clock::time_point t_now;       // Current time
    Clock::time_point t_send_slot; // Time at which *our* slot starts
    double            t_frame_pos; // Offset into the current frame (sec)
    size_t            slot_samps;  // Number of samples to send in a slot

    uhd::set_thread_priority_safe();

    slot_samps = usrp->get_tx_rate()*(slot_size - guard_size);

    modQueue.setWatermark(slot_samps);

    while (!done) {
        // Figure out when our next send slot is
        t_now = Clock::now();
        t_frame_pos = fmod(t_now.get_real_secs(), frame_size);
        t_send_slot = t_now + net->getNodeId()*slot_size - t_frame_pos;

        while (t_send_slot < t_now) {
            printf("MISS\n");
            t_send_slot += frame_size;
        }

        // Schedule transmission for start of our slot
        txSlot(t_send_slot, slot_samps);

        // Wait out the rest of the frame
        struct timespec   ts;
        Clock::time_point t_sleep;

        t_now = Clock::now();
        t_sleep = t_send_slot + frame_size - guard_size - t_now;

        ts.tv_sec = t_sleep.get_full_secs();
        ts.tv_nsec = t_sleep.get_frac_secs()*1e9;

        if (nanosleep(&ts, NULL) < 0)
            perror("txWorker: slumber interrupted");
    }
}

void MAC::txSlot(Clock::time_point when, size_t maxSamples)
{
    std::deque<std::shared_ptr<IQBuf>> txBuf;

    while (maxSamples > 0) {
        std::shared_ptr<ModPacket> mpkt = modQueue.pop(maxSamples);

        if (not mpkt)
            break;

        maxSamples -= mpkt->samples->size();

        if (logger) {
            Header hdr;

            hdr.pkt_id = mpkt->pkt->pkt_id;
            hdr.src = mpkt->pkt->src;
            hdr.dest = mpkt->pkt->dest;

            logger->logSend(when, hdr, mpkt->samples);
        }

        txBuf.emplace_back(std::move(mpkt->samples));
    }

    usrp->burstTX(when, txBuf);
}
