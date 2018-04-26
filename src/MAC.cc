// DWSL - full radio stack

#include <errno.h>

#include <cstring>
#include <deque>

#include "MAC.hh"
#include "USRP.hh"

MAC::MAC(std::shared_ptr<USRP> usrp,
         std::shared_ptr<NET> net,
         std::shared_ptr<RadioPacketSink> sink,
         std::shared_ptr<PHY> phy,
         std::shared_ptr<Logger> logger,
         double frame_size,
         double guard_size,
         size_t rx_pool_size)
  : usrp(usrp),
    net(net),
    sink(sink),
    logger(logger),
    modQueue(net, phy),
    demodQueue(net, phy, sink, rx_pool_size),
    frame_size(frame_size),
    slot_size(frame_size/net->getNumNodes()),
    guard_size(guard_size),
    done(false)
{
    slop_size = 0.5*guard_size;

    usrp->set_rx_rate(phy->getBandwidth()*phy->getRxRateOversample());
    usrp->set_tx_rate(phy->getBandwidth()*phy->getTxRateOversample());

    rxThread = std::thread(&MAC::rxWorker, this);
    txThread = std::thread(&MAC::txWorker, this);
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
    uhd::time_spec_t       t_now;          // Current time
    uhd::time_spec_t       t_cur_slot;     // Time at which current slot starts
    uhd::time_spec_t       t_next_slot;    // Time at which next slot starts
    uhd::time_spec_t       t_samp_start;   // Time at which current slot buffer starts
    uhd::time_spec_t       t_samp_end;     // Time at which current slot buffer ends
    double                 t_slot_pos;     // Offset into the current slot (sec)
    size_t                 slot_samps;     // Number of samples in a slot
    size_t                 slop_samps;     // Number of samples in slop
    size_t                 oversample;     // Number of samples we oversampled as part of previous slot
    size_t                 prevslop_samps; // Number of samples from the previous slot that we demodulate with the current slot
    int                    slot;           // Curent slot index in the frame
    double                 txRate;         // TX rate in Hz
    std::shared_ptr<IQBuf> prevSlot;       // IQ buffer for previous slot's data
    std::shared_ptr<IQBuf> curSlot;        // IQ buffer for current slot's data

    txRate = usrp->get_rx_rate();
    slot_samps = txRate * slot_size;
    slop_samps = txRate * slop_size;

    while (!done) {
        // Set up streaming starting at *next* slot
        t_now = usrp->get_time_now();
        t_slot_pos = fmod(t_now.get_real_secs(), slot_size);
        t_next_slot = t_now + slot_size - t_slot_pos;
        slot = fmod(t_now.get_real_secs(), frame_size) / slot_size;
        oversample = 0;

        usrp->startRXStream(t_next_slot);

        while (!done) {
            // Update times
            t_now = usrp->get_time_now();
            t_cur_slot = t_next_slot;
            t_next_slot += slot_size;

            // Read samples for current slot
            curSlot = usrp->burstRX(t_cur_slot, slot_samps);

            // Queue samples for demodulation
            if (curSlot) {
                auto q = std::make_unique<IQQueue>();

                // We demodulate the part of the previous frame that was
                // oversampled plus an additional slop_samps samples to handle
                // the fact that our slots are not perfectly aligned.
                prevslop_samps = oversample + slop_samps;

                if (prevSlot) {
                    if (prevslop_samps > prevSlot->size())
                        // Should never happen!
                        q->push_back(IQSlice(prevSlot));
                    else
                        q->push_back(IQSlice(prevSlot, prevSlot->size() - prevslop_samps, prevslop_samps));
                }

                // Also demodulate the entire current frame
                q->push_back(IQSlice(curSlot, 0, curSlot->size()));

                demodQueue.push(std::move(q));
            }

            // Determine how much we oversampled
            t_samp_start = curSlot->get_timestamp();
            t_samp_end = t_samp_start + static_cast<double>(curSlot->size()) / txRate;
            oversample = (t_samp_end - t_next_slot).get_real_secs() * txRate;

            // Move to the next slot
            prevSlot = curSlot;
            curSlot.reset();
            ++slot;
        }

        usrp->stopRXStream();
    }
}

void MAC::txWorker(void)
{
    uhd::time_spec_t t_now;       // Current time
    uhd::time_spec_t t_send_slot; // Time at which *our* slot starts
    double           t_frame_pos; // Offset into the current frame (sec)
    size_t           slot_samps;  // Number of samples to send in a slot

    slot_samps = usrp->get_tx_rate()*(slot_size - guard_size);

    modQueue.setWatermark(slot_samps);

    while (!done) {
        // Figure out when our next send slot is
        t_now = usrp->get_time_now();
        t_frame_pos = fmod(t_now.get_real_secs(), frame_size);
        t_send_slot = t_now + net->getNodeId()*slot_size - t_frame_pos;

        while (t_send_slot < t_now) {
            printf("MISS\n");
            t_send_slot += frame_size;
        }

        // Schedule transmission for start of our slot
        txSlot(t_send_slot, slot_samps);

        // Wait out the rest of the frame
        struct timespec  ts;
        uhd::time_spec_t t_sleep;

        t_now = usrp->get_time_now();
        t_sleep = t_send_slot + frame_size - guard_size - t_now;

        ts.tv_sec = t_sleep.get_full_secs();
        ts.tv_nsec = t_sleep.get_frac_secs()*1e9;

        if (nanosleep(&ts, NULL) < 0)
            perror("txWorker: slumber interrupted");
    }
}

void MAC::txSlot(uhd::time_spec_t when, size_t maxSamples)
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
