// DWSL - full radio stack

#include <deque>

#include "MAC.hh"
#include "USRP.hh"

MAC::MAC(std::shared_ptr<IQTransport> t,
         std::shared_ptr<NET> net,
         std::shared_ptr<PHY> phy,
         double frame_size,
         double pad_size)
  : t(t), net(net), phy(phy),
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
    while (!done) {
        size_t num_samps_to_deliver = (size_t)((t->get_rx_rate())*(slot_size))+(t->get_rx_rate()*(pad_size))*2.0;

        // calculate time to wait for streaming (precisely time beginning of each slot)
        double time_now;
        double wait_time;

        time_now = t->get_time_now();
        wait_time = frame_size - 1.0*fmod(time_now,frame_size)-(pad_size);

        std::unique_ptr<IQBuffer> buf = t->burstRX(time_now+wait_time, num_samps_to_deliver);

        phy->demodulate(std::move(buf));
    }
}

void MAC::txWorker(void)
{
    size_t slot_samps = t->get_tx_rate()*(slot_size - pad_size);
    double time_now;
    double frame_pos;
    double wait_time;
    double slot_start_time;

    modQueue.setWatermark(slot_samps);

    while (!done) {
        // Schedule transmission for start of our slot
        time_now = t->get_time_now();
        frame_pos = fmod(time_now, frame_size);
        wait_time = net->getNodeId()*slot_size - frame_pos;

        if (wait_time < 0) {
            printf("MISS\n");
            wait_time += frame_size;
        }

        slot_start_time = time_now + wait_time;

        txSlot(slot_start_time, slot_samps);

        // Wait out the rest of the frame
        time_now = t->get_time_now();

        while (time_now - slot_start_time < frame_size - pad_size) {
            usleep(10);
            time_now = t->get_time_now();
        }
    }
}

void MAC::txSlot(double when, size_t maxSamples)
{
    std::deque<std::unique_ptr<IQBuffer>> txBuf;

    while (maxSamples > 0) {
        std::unique_ptr<ModPacket> mpkt = modQueue.pop(maxSamples);

        if (not mpkt)
            break;

        maxSamples -= mpkt->nsamples;

        txBuf.insert(txBuf.end(),
                     std::make_move_iterator(mpkt->samples.begin()),
                     std::make_move_iterator(mpkt->samples.end()));
    }

    t->burstTX(when, txBuf);
}
