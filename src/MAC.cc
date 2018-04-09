// DWSL - full radio stack

#include <deque>

#include "MAC.hh"
#include "USRP.hh"

MAC::MAC(std::shared_ptr<USRP> usrp,
         std::shared_ptr<NET> net,
         std::shared_ptr<PHY> phy,
         double frame_size,
         double pad_size)
  : usrp(usrp), net(net), phy(phy),
    modQueue(net, phy),
    frame_size(frame_size),
    slot_size(frame_size/net->getNumNodes()),
    pad_size(pad_size),
    done(false)
{

    rxThread = std::thread(&MAC::rxWorker, this);
    txThread = std::thread(&MAC::txWorker, this);
}

MAC::~MAC()
{
}

void MAC::join(void)
{
    done = true;
    rxThread.join();
    txThread.join();
    modQueue.join();
}

void MAC::rxWorker(void)
{
    uhd::time_spec_t time_now;
    uhd::time_spec_t wait_time;
    uhd::time_spec_t pre_slot_start_time;
    size_t nsamps;

    while (!done) {
        nsamps = (size_t)((usrp->get_rx_rate())*(slot_size))+(usrp->get_rx_rate()*(pad_size))*2.0;

        // Time rx to start at pad before next slot
        time_now = usrp->get_time_now();
        wait_time = frame_size - fmod(time_now.get_real_secs(), frame_size) - pad_size;

        if (wait_time < 0.0)
            continue;

        pre_slot_start_time = time_now + wait_time;

        std::unique_ptr<IQQueue> buf(new IQQueue());

        buf->push_back(usrp->burstRX(pre_slot_start_time, nsamps));

        phy->demodulate(std::move(buf));
    }
}

void MAC::txWorker(void)
{
    size_t           slot_samps = usrp->get_tx_rate()*(slot_size - pad_size);
    uhd::time_spec_t time_now;
    uhd::time_spec_t frame_pos;
    uhd::time_spec_t wait_time;
    uhd::time_spec_t slot_start_time;

    modQueue.setWatermark(slot_samps);

    while (!done) {
        // Schedule transmission for start of our slot
        time_now = usrp->get_time_now();
        frame_pos = fmod(time_now.get_real_secs(), frame_size);
        wait_time = net->getNodeId()*slot_size - frame_pos;

        if (wait_time < 0.0) {
            printf("MISS\n");
            wait_time += frame_size;
        }

        slot_start_time = time_now + wait_time;

        txSlot(slot_start_time, slot_samps);

        // Wait out the rest of the frame
        time_now = usrp->get_time_now();

        while (time_now - slot_start_time < frame_size - pad_size) {
            usleep(10);
            time_now = usrp->get_time_now();
        }
    }
}

void MAC::txSlot(uhd::time_spec_t when, size_t maxSamples)
{
    std::deque<std::unique_ptr<IQBuf>> txBuf;

    while (maxSamples > 0) {
        std::unique_ptr<ModPacket> mpkt = modQueue.pop(maxSamples);

        if (not mpkt)
            break;

        maxSamples -= mpkt->nsamples;

        txBuf.insert(txBuf.end(),
                     std::make_move_iterator(mpkt->samples.begin()),
                     std::make_move_iterator(mpkt->samples.end()));
    }

    usrp->burstTX(when, txBuf);
}
