// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "Clock.hh"
#include "Radio.hh"
#include "mac/FDMA.hh"

FDMA::FDMA(std::shared_ptr<Radio> radio,
           std::shared_ptr<Controller> controller,
           std::shared_ptr<SnapshotCollector> collector,
           std::shared_ptr<Channelizer> channelizer,
           std::shared_ptr<ChannelSynthesizer> synthesizer,
           double period)
  : MAC(radio,
        controller,
        collector,
        channelizer,
        synthesizer,
        period,
        4)
  , premod_(period)
  , accurate_tx_timestamps_(false)
  , channel_synthesizer_(synthesizer)
{
    rx_thread_ = std::thread(&FDMA::rxWorker, this);
    tx_thread_ = std::thread(&FDMA::txWorker, this);
    tx_notifier_thread_ = std::thread(&FDMA::txNotifier, this);

    modify([&]() { reconfigure(); });
}

FDMA::~FDMA()
{
    stop();
}

void FDMA::stop(void)
{
    if (modify([&]() { done_ = true; })) {
        // Join on all threads
        if (rx_thread_.joinable())
            rx_thread_.join();

        if (tx_thread_.joinable())
            tx_thread_.join();

        if (tx_notifier_thread_.joinable())
            tx_notifier_thread_.join();
    }
}

void FDMA::txWorker(void)
{
    std::optional<MonoClock::time_point> t_next_tx; // Time at which next transmission starts
    bool                                 next_slot_start_of_burst = true;

    for (;;) {
        ChannelSynthesizer::container_type mpkts;
        size_t                             nsamples;

        if (next_slot_start_of_burst)
            nsamples = channel_synthesizer_->pop(mpkts);
        else
            nsamples = channel_synthesizer_->try_pop(mpkts);

        // Synchronize on state change
        if (needs_sync()) {
            sync();

            if (done_)
                return;
        }

        // If we don't have any data to send, we're done. If this slot was not
        // the start of a burst, then it is part of an in-flight burst, in which
        // case we need to stop the burst.
        if (nsamples == 0) {
            if (!next_slot_start_of_burst) {
                radio_->stopTXBurst();
                next_slot_start_of_burst = true;
            }

            continue;
        }

        // Collect IQ buffers. We keep track of whether or not this batch of
        // modulate packets needs an accurate timestamp.
        std::list<std::shared_ptr<IQBuf>> iqbufs;
        bool accurate_timestamp = accurate_tx_timestamps_;

        for (auto it = mpkts.begin(); it != mpkts.end(); ++it) {
            if ((*it)->pkt->timestamp_seq)
                accurate_timestamp = true;

            iqbufs.emplace_back((*it)->samples);
        }

        // Determine time of next transmission. If this is *not* the start of a
        // burst, then t_next_tx has already been updated. Otherwise, we
        // initialize it with the current time since we are starting a new
        // burst. If we need an accurate timestamp, we use a timed burst, in
        // which case we need to add a slight delay to the transmission time.
        if (next_slot_start_of_burst && accurate_timestamp)
            t_next_tx = MonoClock::now() + radio_->getTXLeadTime();
        else
            t_next_tx = radio_->getNextTXTime();

        // Send IQ buffers
        radio_->burstTX(next_slot_start_of_burst && accurate_timestamp ? t_next_tx : std::nullopt,
                       next_slot_start_of_burst,
                       false,
                       iqbufs);

        next_slot_start_of_burst = false;

        // Hand-off TX record to TX notification thread
        {
            std::lock_guard<std::mutex> lock(tx_records_mutex_);

            tx_records_.emplace(t_next_tx, 0, nsamples, std::move(iqbufs), std::move(mpkts));
        }

        tx_records_cond_.notify_one();

        // Start a new TX burst if there was an underflow
        if (radio_->getTXUnderflowCount() != 0) {
            radio_->stopTXBurst();
            next_slot_start_of_burst = true;
        }
    }
}

void FDMA::wake_dependents()
{
    MAC::wake_dependents();

    // Disable the channel synthesizer
    channel_synthesizer_->disable();
}

void FDMA::reconfigure(void)
{
    MAC::reconfigure();

    // Determine whether or not we can transmit
    can_transmit_ = false;

    for (size_t chan = 0; chan < schedule_.nchannels(); ++chan) {
        // Check for valid FDMA schedule, i.e., we only have one slot for each channel
        if (schedule_[chan].size() != 1)
            throw std::out_of_range("Schedule is not an FDMA schedule: schedule has more than one slot");

        if (schedule_[chan][0]) {
            can_transmit_ = true;
            break;
        }
    }

    // Set synthesizer's high water mark
    channel_synthesizer_->setHighWaterMark(premod_*tx_rate_);

    // Re-enable the channel synthesizer
    channel_synthesizer_->enable();
}
