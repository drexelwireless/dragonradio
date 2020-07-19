#include "ParallelChannelSynthesizer.hh"

template <class ChannelModulator>
ParallelChannelSynthesizer<ChannelModulator>::ParallelChannelSynthesizer(std::shared_ptr<PHY> phy,
                                                                         double tx_rate,
                                                                         const Channels &channels,
                                                                         size_t nthreads)
  : ChannelSynthesizer(phy, tx_rate, channels)
  , nthreads_(nthreads)
  , done_(false)
  , reconfigure_(true)
  , reconfigure_sync_(nthreads+1)
{
    for (size_t i = 0; i < nthreads; ++i)
        mod_threads_.emplace_back(std::thread(&ParallelChannelSynthesizer::modWorker,
                                              this,
                                              i));

    reconfigure();
}

template <class ChannelModulator>
ParallelChannelSynthesizer<ChannelModulator>::~ParallelChannelSynthesizer()
{
    stop();
}

template <class ChannelModulator>
void ParallelChannelSynthesizer<ChannelModulator>::stop(void)
{
    // XXX We must disconnect the sink in order to stop the modulator threads.
    sink.disconnect();

    done_ = true;

    queue_.stop();

    wake_cond_.notify_all();

    for (size_t i = 0; i < mod_threads_.size(); ++i) {
        if (mod_threads_[i].joinable())
            mod_threads_[i].join();
    }
}

template <class ChannelModulator>
void ParallelChannelSynthesizer<ChannelModulator>::reconfigure(void)
{
    // Determine channel index
    std::optional<size_t> chanidx;

    for (size_t chan = 0; chan < schedule_.size(); ++chan) {
        if (schedule_.canTransmitOnChannel(chan)) {
            chanidx = chan;
            break;
        }
    }

    chanidx_ = chanidx;

    // Tell workers we are reconfiguring
    reconfigure_.store(true, std::memory_order_release);

    // Wake all workers that might be sleeping.
    {
        std::unique_lock<std::mutex> lock(wake_mutex_);

        wake_cond_.notify_all();
    }

    // Wait for workers to be ready for reconfiguration
    reconfigure_sync_.wait();

    // We are done reconfiguring
    reconfigure_.store(false, std::memory_order_release);

    // Wait for workers to resume
    reconfigure_sync_.wait();
}

template <class ChannelModulator>
void ParallelChannelSynthesizer<ChannelModulator>::modWorker(unsigned tid)
{
    std::unique_ptr<ChannelModulator> mod;
    std::shared_ptr<NetPacket>        pkt;
    std::unique_ptr<ModPacket>        mpkt;

    while (!done_) {
        // Reconfigure if necessary
        if (reconfigure_.load(std::memory_order_acquire)) {
            // Wait for reconfiguration to start
            reconfigure_sync_.wait();

            // Wait for reconfiguration to finish
            reconfigure_sync_.wait();

            // If we have no channels, sleep
            if (channels_.size() == 0 || !chanidx_) {
                std::unique_lock<std::mutex> lock(wake_mutex_);

                wake_cond_.wait(lock, [this]{ return done_ || reconfigure_.load(std::memory_order_acquire); });

                continue;
            } else {
                // Reconfigure the modulator
                mod = std::make_unique<ChannelModulator>(*phy_,
                                                         0,
                                                         channels_[*chanidx_].first,
                                                         channels_[*chanidx_].second,
                                                         tx_rate_);
            }
        }

        // Get a packet to modulate
        if (!pkt) {
            if (!sink.pull(pkt))
                continue;
        }

        // Modulate the packet
        std::unique_ptr<ModPacket> mpkt = std::make_unique<ModPacket>();
        float                      g = phy_->mcs_table[pkt->mcsidx].autogain.getSoftTXGain();

        mod->modulate(std::move(pkt), g, *mpkt);

        // Add the packet to the queue
        queue_.push(std::move(mpkt));
    }
}
