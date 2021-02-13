// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "Clock.hh"
#include "USRP.hh"
#include "mac/FDMA.hh"

// Set to 1 to transmit bursts immediately. The down side of setting this to 1
// is that we will have less accurate TX timestamps.
#define TX_IMMEDIATE 1

FDMA::FDMA(std::shared_ptr<USRP> usrp,
           std::shared_ptr<PHY> phy,
           std::shared_ptr<Controller> controller,
           std::shared_ptr<SnapshotCollector> collector,
           std::shared_ptr<Channelizer> channelizer,
           std::shared_ptr<ChannelSynthesizer> synthesizer,
           double period)
  : MAC(usrp,
        phy,
        controller,
        collector,
        channelizer,
        synthesizer,
        period)
  , premod_(period)
  , channel_synthesizer_(synthesizer)
{
    rx_thread_ = std::thread(&FDMA::rxWorker, this);
    tx_thread_ = std::thread(&FDMA::txWorker, this);
    tx_notifier_thread_ = std::thread(&FDMA::txNotifier, this);
}

FDMA::~FDMA()
{
    stop();
}

void FDMA::stop(void)
{
    done_ = true;

    synthesizer_->stop();

    tx_records_cond_.notify_all();

    if (rx_thread_.joinable())
        rx_thread_.join();

    if (tx_thread_.joinable())
        tx_thread_.join();

    if (tx_notifier_thread_.joinable())
        tx_notifier_thread_.join();
}

void FDMA::reconfigure(void)
{
    MAC::reconfigure();

    // Determine whether or not we can transmit
    bool can_transmit = false;

    for (size_t chan = 0; chan < schedule_.size(); ++chan) {
        // Check for valid FDMA schedule, i.e., we only have one slot for each channel
        if (schedule_[chan].size() != 1)
            throw std::out_of_range("Schedule is not an FDMA schedule: schedule has mor ethan one slot");

        if (schedule_[chan][0]) {
            can_transmit = true;
            break;
        }
    }

    can_transmit_ = can_transmit;

    // Set synthesizer's high water mark
    channel_synthesizer_->setHighWaterMark(premod_*tx_rate_);
}

void FDMA::txWorker(void)
{
    WallClock::time_point t_now;
    WallClock::time_point t_next_tx; // Time at which next transmission starts
    bool                  next_slot_start_of_burst = true;

    while (!done_) {
        ChannelSynthesizer::container_type mpkts;
        size_t                             nsamples;

        if (next_slot_start_of_burst)
            nsamples = channel_synthesizer_->pop(mpkts);
        else
            nsamples = channel_synthesizer_->try_pop(mpkts);

        // If we don't have any data to send, we're done. If this slot was not
        // the start of a burst, then it is part of an in-flight burst, in which
        // case we need to stop the burst.
        if (nsamples == 0) {
            if (!next_slot_start_of_burst) {
                usrp_->stopTXBurst();
                next_slot_start_of_burst = true;
            }

            continue;
        }

        // Collect IQ buffers
        std::list<std::shared_ptr<IQBuf>> iqbufs;

        for (auto it = mpkts.begin(); it != mpkts.end(); ++it)
            iqbufs.emplace_back((*it)->samples);

        // Send IQ buffers
        if (next_slot_start_of_burst)
            t_next_tx = WallClock::now() + 200e-6;

#if TX_IMMEDIATE
        usrp_->burstTX(std::nullopt,
#else
        usrp_->burstTX(WallClock::to_mono_time(t_next_tx),
#endif
                       next_slot_start_of_burst,
                       false,
                       iqbufs);

        next_slot_start_of_burst = false;

        // Hand-off TX record to TX notification thread
        {
            std::lock_guard<std::mutex> lock(tx_records_mutex_);

            tx_records_.emplace(TXRecord { t_next_tx, 0, nsamples, std::move(iqbufs), std::move(mpkts) });
        }

        tx_records_cond_.notify_one();

        // Handle TX underflow
        if (usrp_->getTXUnderflowCount() != 0) {
            usrp_->stopTXBurst();
            next_slot_start_of_burst = true;
        } else
            t_next_tx += nsamples / tx_rate_;
    }
}
