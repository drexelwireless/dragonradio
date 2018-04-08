// DWSL - full radio stack

#include "MAC.hh"
#include "USRP.hh"

MAC::MAC(std::shared_ptr<FloatIQTransport> t,
         std::shared_ptr<NET> net,
         std::shared_ptr<PHY> phy,
         double frame_size,
         float pad_size,
         unsigned int packets_per_slot)
  : t(t), net(net), phy(phy)
{
    this->frame_size = frame_size;
    this->slot_size = frame_size/((double)(net->getNumNodes()));
    this->pad_size = pad_size;
    this->packets_per_slot = packets_per_slot;

    this->continue_running = true;

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
    phy->prepareTXBurst(packets_per_slot);

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

        phy->burstTX(time_now+wait_time);

        // readyNextBuffer
        phy->prepareTXBurst(packets_per_slot);

        // wait out the rest of the slot
        double new_time_now = t->get_time_now();

        while((new_time_now-(time_now+wait_time))<(frame_size-pad_size))
        {
            usleep(10);
            new_time_now = t->get_time_now();
        }
    }
}
