// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "mac/MAC.hh"
#include "util/threads.hh"

MAC::MAC(std::shared_ptr<USRP> usrp,
         std::shared_ptr<PHY> phy,
         std::shared_ptr<Controller> controller,
         std::shared_ptr<SnapshotCollector> collector,
         std::shared_ptr<Channelizer> channelizer,
         std::shared_ptr<Synthesizer> synthesizer,
         double rx_period)
  : usrp_(usrp)
  , phy_(phy)
  , controller_(controller)
  , snapshot_collector_(collector)
  , channelizer_(channelizer)
  , synthesizer_(synthesizer)
  , done_(false)
  , can_transmit_(true)
  , rx_period_(rx_period)
  , rx_period_samps_(0)
  , rx_bufsize_(0)
  , logger_(logger)
{
    rx_rate_ = usrp->getRXRate();
    tx_rate_ = usrp->getTXRate();
}

void MAC::reconfigure(void)
{
    rx_rate_ = usrp_->getRXRate();
    tx_rate_ = usrp_->getTXRate();

    if (usrp_->getTXRate() == usrp_->getRXRate())
        tx_fc_off_ = std::nullopt;
    else
        tx_fc_off_ = usrp_->getTXFrequency() - usrp_->getRXFrequency();

    rx_period_samps_ = rx_rate_*rx_period_;
    rx_bufsize_ = usrp_->getRecommendedBurstRXSize(rx_period_samps_);
}

void MAC::rxWorker(void)
{
    WallClock::time_point t_cur_period;   // Time at which current period starts
    WallClock::time_point t_next_period;  // Time at which next period starts
    double                t_period_pos;   // Offset into the current period (sec)
    unsigned              seq = 0;        // Current IQ buffer sequence number

    while (!done_) {
        // Wait for period to be known
        if (rx_period_samps_ == 0) {
            doze(100e-3);
            continue;
        }

        // Set up streaming starting at *next* period
        {
            WallClock::time_point t_now = WallClock::now();

            t_period_pos = fmod(t_now, rx_period_);
            t_next_period = t_now + rx_period_ - t_period_pos;
        }

        // Bump the sequence number to indicate a discontinuity
        seq++;

        usrp_->startRXStream(WallClock::to_mono_time(t_next_period));

        while (!done_) {
            // Update times
            t_cur_period = t_next_period;
            t_next_period += rx_period_;

            // Create buffer for period
            auto iqbuf = std::make_shared<IQBuf>(rx_bufsize_);

            iqbuf->seq = seq++;

            // Push the buffer if we're snapshotting
            bool do_snapshot;

            if (snapshot_collector_)
                do_snapshot = snapshot_collector_->push(iqbuf);
            else
                do_snapshot = false;

            // Put the buffer into the channelizer's queue so it can start
            // working now
            channelizer_->push(iqbuf);

            // Read samples for current period. The demodulator will do its
            // thing as we continue to read samples.
            bool ok = usrp_->burstRX(WallClock::to_mono_time(t_cur_period), rx_period_samps_, *iqbuf);

            // Update snapshot offset by finalizing this snapshot
            if (do_snapshot)
                snapshot_collector_->finalizePush();

            // If there was an RX error, break and set up the RX stream again.
            if (!ok)
                break;
        }

        // Attempt to deal with RX errors
        logMAC(LOGERROR, "attempting to reset RX loop");
        usrp_->stopRXStream();
    }
}

void MAC::txNotifier(void)
{
    TXRecord record;

    while (!done_) {
        // Get TX record
        {
            std::unique_lock<std::mutex> lock(tx_records_mutex_);

            tx_records_cond_.wait(lock, [this]{ return done_ || !tx_records_.empty(); });

            // If we're done, we're done
            if (done_)
                return;

            record = std::move(tx_records_.front());
            tx_records_.pop();
        }

        if (record.timestamp) {
            // Timestamp packets
            for (auto it = record.mpkts.begin(); it != record.mpkts.end(); ++it)
                (*it)->pkt->tx_timestamp = *record.timestamp + (record.delay + (*it)->start)/tx_rate_;

            // Record the record's load
            {
                std::lock_guard<std::mutex> lock(load_mutex_);

                for (auto it = record.mpkts.begin(); it != record.mpkts.end(); ++it) {
                    unsigned chanidx = (*it)->chanidx;

                    if (chanidx < load_.nsamples.size())
                        load_.nsamples[chanidx] += (*it)->nsamples;
                }

                load_.end = WallClock::to_wall_time(*record.timestamp) + (record.delay + record.nsamples)/tx_rate_;
            }
        }

        // Log the transmissions
        if (logger_ && logger_->getCollectSource(Logger::kSentPackets)) {
            std::shared_ptr<IQBuf> &first = record.iqbufs.front();

            for (auto it = record.mpkts.begin(); it != record.mpkts.end(); ++it) {
                (*it)->pkt->fc = tx_fc_off_ ? *tx_fc_off_ : (*it)->channel.fc;
                (*it)->pkt->bw = tx_rate_;
                (*it)->pkt->offset = (*it)->offset;
                (*it)->pkt->nsamples = (*it)->nsamples;

                if (logger_->getCollectSource(Logger::kSentIQ))
                    (*it)->pkt->samples = (*it)->samples ? (*it)->samples : first;

                logger_->logSend((*it)->pkt);
            }
        }

        // Inform the controller of the transmission
        controller_->transmitted(record.mpkts);

        // Tell the snapshot collector about local self-transmissions
        if (snapshot_collector_ && record.timestamp) {
            for (auto it = record.mpkts.begin(); it != record.mpkts.end(); ++it) {
                MonoClock::time_point timestamp = *record.timestamp + (*it)->start/tx_rate_;

                snapshot_collector_->selfTX(timestamp,
                                            rx_rate_,
                                            tx_rate_,
                                            (*it)->channel.bw,
                                            (*it)->nsamples,
                                            tx_fc_off_ ? *tx_fc_off_ : (*it)->channel.fc);
            }
        }

        // Log the TX record
        if (logger_)
            logger_->logTXRecord(record.timestamp, record.nsamples, tx_rate_);
    }
}
