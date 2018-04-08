// DWSL - full radio stack

#include <deque>

#include "MAC.hh"
#include "USRP.hh"

MAC::MAC(std::shared_ptr<FloatIQTransport> t,
         std::shared_ptr<NET> net,
         std::shared_ptr<PHY> phy,
         double frame_size,
         double pad_size)
  : t(t), net(net), phy(phy),
    modQueue(net, phy),
    frame_size(frame_size), pad_size(pad_size),
    continue_running(true)
{
    slot_size = frame_size/net->getNumNodes();

    // start the rx thread
    rx_worker_thread = std::thread(&MAC::rx_worker, this);
}

MAC::~MAC()
{
    continue_running = false;
    rx_worker_thread.join();
}

void MAC::rx_worker(void)
{
    while(continue_running)
    {
        size_t num_samps_to_deliver = (size_t)((t->get_rx_rate())*(slot_size))+(t->get_rx_rate()*(pad_size))*2.0;

        // calculate time to wait for streaming (precisely time beginning of each slot)
        double time_now;
        double wait_time;

        time_now = t->get_time_now();
        wait_time = frame_size - 1.0*fmod(time_now,frame_size)-(pad_size);

        phy->burstRX(time_now+wait_time, num_samps_to_deliver);
    }
}

void MAC::run(void)
{
    size_t slot_samps = t->get_tx_rate()*(slot_size - pad_size);

    modQueue.setWatermark(slot_samps);

    while (continue_running) {
        double time_now;
        double frame_pos;
        double wait_time;

        time_now = t->get_time_now();
        frame_pos = fmod(time_now,frame_size);
        wait_time = net->getNodeId()*slot_size - frame_pos;
        if (wait_time<0) {
            printf("MISS\n");
            wait_time += frame_size;
        }

        txSlot(time_now+wait_time, slot_samps);

        // wait out the rest of the slot
        double new_time_now = t->get_time_now();

        while((new_time_now-(time_now+wait_time))<(frame_size-pad_size))
        {
            usleep(10);
            new_time_now = t->get_time_now();
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

    phy->burstTX(when, txBuf);
}
