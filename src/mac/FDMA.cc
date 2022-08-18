// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "Clock.hh"
#include "Radio.hh"
#include "mac/FDMA.hh"

FDMA::FDMA(std::shared_ptr<Radio> radio,
           std::shared_ptr<Controller> controller,
           std::shared_ptr<SnapshotCollector> collector,
           std::shared_ptr<Channelizer> channelizer,
           std::shared_ptr<Synthesizer> synthesizer,
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
    for (;;) {
        TXRecord txrecord;

        // Pop packets to send.
        if (radio_->inTXBurst()) {
            std::optional<MonoClock::time_point> t_next_tx = radio_->getNextTXTime();

            if (t_next_tx)
                txrecord = synthesizer_->pop_until(*t_next_tx - radio_->getTXLeadTime());
            else
                txrecord = synthesizer_->try_pop();
        } else {
            txrecord = synthesizer_->pop();
        }

        // Synchronize on state change
        if (needs_sync()) {
            sync();

            if (done_)
                return;
        }

        // If we don't have any data to send, we're done. If this slot was not
        // the start of a burst, then it is part of an in-flight burst, in which
        // case we need to stop the burst.
        if (txrecord.nsamples == 0) {
            radio_->stopTXBurst();

            continue;
        }

        // Collect IQ buffers. We keep track of whether or not this batch of
        // modulate packets needs an accurate timestamp.
        bool accurate_timestamp = accurate_tx_timestamps_;

        for (auto it = txrecord.mpkts.begin(); it != txrecord.mpkts.end(); ++it) {
            if ((*it)->pkt->timestamp_seq) {
                accurate_timestamp = true;
                break;
            }
        }

        // Determine time of next transmission.
        //
        // If we need an accurate timestamp, we first attempt to use the radio's
        // "next TX" timestamp. If that isn't available and we are in a TX
        // burst, stop the burst. Then, start a new burst with a timestamp. We
        // set the start of the new burst to be time in the very near future.
        std::optional<MonoClock::time_point> t_next_tx = radio_->getNextTXTime();

        if (accurate_timestamp && !t_next_tx) {
            if (radio_->inTXBurst())
                radio_->stopTXBurst();

            t_next_tx = MonoClock::now() + radio_->getTXLeadTime();
        }

        // Send IQ buffers. We always start a TX burst if we are not already in
        // one.
        radio_->burstTX(t_next_tx,
                        !radio_->inTXBurst(),
                        false,
                        txrecord.iqbufs);

        // Hand-off TX record to TX notification thread
        txrecord.timestamp = t_next_tx;

        pushTXRecord(std::move(txrecord));
    }
}

void FDMA::wake_dependents()
{
    MAC::wake_dependents();

    // Disable the channel synthesizer
    synthesizer_->disable();
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
    synthesizer_->setHighWaterMark(premod_*tx_rate_);

    // Re-enable the channel synthesizer
    synthesizer_->enable();
}
