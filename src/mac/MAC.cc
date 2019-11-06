#include "mac/MAC.hh"
#include "Util.hh"

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
{
    rx_rate_ = usrp->getRXRate();
    tx_rate_ = usrp->getTXRate();
}

void MAC::reconfigure(void)
{
    rx_rate_ = usrp_->getRXRate();
    tx_rate_ = usrp_->getTXRate();

    rx_period_samps_ = rx_rate_*rx_period_;
    rx_bufsize_ = usrp_->getRecommendedBurstRXSize(rx_period_samps_);
}

void MAC::rxWorker(void)
{
    Clock::time_point t_cur_period;   // Time at which current period starts
    Clock::time_point t_next_period;  // Time at which next period starts
    double            t_period_pos;   // Offset into the current period (sec)
    unsigned          seq = 0;        // Current IQ buffer sequence number

    makeThisThreadHighPriority();

    while (!done_) {
        // Wait for period to be known
        if (rx_period_samps_ == 0) {
            doze(100e-3);
            continue;
        }

        // Set up streaming starting at *next* period
        {
            Clock::time_point t_now = Clock::now();

            t_period_pos = fmod(t_now, rx_period_);
            t_next_period = t_now + rx_period_ - t_period_pos;
        }

        // Bump the sequence number to indicate a discontinuity
        seq++;

        usrp_->startRXStream(Clock::to_mono_time(t_next_period));

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
            bool ok = usrp_->burstRX(Clock::to_mono_time(t_cur_period), rx_period_samps_, *iqbuf);

            // Update snapshot offset by finalizing this snapshot
            if (do_snapshot)
                snapshot_collector_->finalizePush();

            // If there was an RX error, break and set up the RX stream again.
            if (!ok)
                break;
        }

        // Attempt to deal with RX errors
        logEvent("MAC: attempting to reset RX loop");
        usrp_->stopRXStream();
    }
}
