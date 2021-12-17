// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <sys/types.h>
#include <sys/time.h>
#include <time.h>

#include <atomic>
#include <random>

#include "logging.hh"
#include "Clock.hh"
#include "USRP.hh"
#include "util/capabilities.hh"

USRP::USRP(const std::string& addr,
           const std::optional<std::string>& tx_subdev,
           const std::optional<std::string>& rx_subdev,
           double freq,
           const std::string& tx_ant,
           const std::string& rx_ant,
           float tx_gain,
           float rx_gain)
  : auto_dc_offset_(false)
  , done_(false)
{
    RaiseCaps caps({CAP_SYS_NICE});

    usrp_ = uhd::usrp::multi_usrp::make(addr);

    tx_underflow_count_.store(0, std::memory_order_release);
    tx_late_count_.store(0, std::memory_order_release);

    determineDeviceType();

    usrp_->set_tx_antenna(tx_ant);
    usrp_->set_rx_antenna(rx_ant);

    usrp_->set_tx_gain(tx_gain);
    usrp_->set_rx_gain(rx_gain);

    // Set subdevice specifications
    if (tx_subdev)
        usrp_->set_tx_subdev_spec(*tx_subdev);

    if (rx_subdev)
        usrp_->set_rx_subdev_spec(*rx_subdev);

    // Set up clock
    Clock::setUSRP(usrp_);

    // Set RX and TX frequencies
    setRXFrequency(freq);
    setTXFrequency(freq);

    // Get TX and RX rates
    tx_rate_ = usrp_->get_tx_rate();
    rx_rate_ = usrp_->get_rx_rate();

    // Get TX and RX frequencies
    tx_freq_ = usrp_->get_tx_freq();
    rx_freq_ = usrp_->get_rx_freq();

    // Set up USRP streaming
    uhd::stream_args_t stream_args("fc32");

    tx_stream_ = usrp_->get_tx_stream(stream_args);
    rx_stream_ = usrp_->get_rx_stream(stream_args);

    // Set maximum number of samples we attempt to TX/RX.
    if (device_type_ == kUSRPX310) {
        setMaxTXSamps(8*tx_stream_->get_max_num_samps());
        setMaxRXSamps(8*rx_stream_->get_max_num_samps());
    } else {
        setMaxTXSamps(512);
        setMaxRXSamps(2048);
    }

    // Start thread that receives TX errors
    tx_error_thread_ = std::thread(&USRP::txErrorWorker, this);
}

USRP::~USRP()
{
    stop();
}

void USRP::syncTime(bool random_bias)
{
    // Set offset relative to system NTP time
    struct timespec t;
    int    err;

    if ((err = clock_gettime(CLOCK_REALTIME, &t)) != 0) {
        fprintf(stderr, "clock_gettime failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    uhd::time_spec_t now(t.tv_sec, ((double)t.tv_nsec)/1e9);

    if (random_bias) {
        std::random_device               rd;
        std::mt19937                     gen(rd());
        std::uniform_real_distribution<> dist(0.0, 10.0);
        double                           offset = dist(gen);

        fprintf(stderr, "CLOCK: offset=%g\n", offset);

        now += offset;
    }

    usrp_->set_time_now(now);
}

// See the following for X310 LO offset advice:
//   https://sc2colosseum.freshdesk.com/support/solutions/articles/22000220403-optimizing-srn-usrp-performance
//
// See the following for instructions of waiting for LO to settle:
//   https://files.ettus.com/manual/page_general.html

const int kMaxLOLockCount = 100;

void USRP::setTXFrequency(double freq)
{
    int count;

  retry:
    if (device_type_ == kUSRPX310) {
        double lo_offset = -42.0e6;
        usrp_->set_tx_freq(uhd::tune_request_t(freq, lo_offset));
    } else
        usrp_->set_tx_freq(freq);

    count = 0;

    while (!usrp_->get_tx_sensor("lo_locked").to_bool()) {
        if (count++ > kMaxLOLockCount) {
            logUSRP(LOGDEBUG, "Could not attain TX LO lock");
            goto retry;
        }

        usleep(100);
    }

    tx_freq_ = usrp_->get_tx_freq();

    logUSRP(LOGDEBUG, "TX frequency set to %g", freq);
}

void USRP::setRXFrequency(double freq)
{
    int count;

  retry:
    if (device_type_ == kUSRPX310) {
        double lo_offset = +42.0e6;
        usrp_->set_rx_freq(uhd::tune_request_t(freq, lo_offset));
    } else
        usrp_->set_rx_freq(freq);

    count = 0;

    while (!usrp_->get_rx_sensor("lo_locked").to_bool()) {
        if (count++ > kMaxLOLockCount) {
            logUSRP(LOGDEBUG, "Could not attain RX LO lock");
            goto retry;
        }

        usleep(100);
    }

    rx_freq_ = usrp_->get_rx_freq();

    logUSRP(LOGDEBUG, "RX frequency set to %g", freq);
}

void USRP::burstTX(std::optional<MonoClock::time_point> when_,
                   bool start_of_burst,
                   bool end_of_burst,
                   std::list<std::shared_ptr<IQBuf>>& bufs)
{
    uhd::tx_metadata_t tx_md; // TX metadata for UHD
    size_t             n;     // Size of next send

    if (start_of_burst) {
        if (when_) {
            tx_md.time_spec = when_->t;
            tx_md.has_time_spec = true;

            t_next_tx_ = *when_;
        }

        tx_md.start_of_burst = true;
        tx_md.end_of_burst = false;
    }

    // We walk through the supplied queue of buffers and transmit each in chunks
    // whose size is no more than tx_max_samps_ bytes, which is the maximum size
    // of a USRP TX packet. This allows us to avoid being "late" even when we
    // have a very large buffer to send.
    for (auto it = bufs.begin(); it != bufs.end(); ++it) {
        IQBuf& iqbuf = **it; // Current buffer we are sending

        if (t_next_tx_)
            iqbuf.timestamp = *t_next_tx_ - iqbuf.delay/tx_rate_;

        for (size_t off = iqbuf.delay; off < iqbuf.size(); off += n) {
            // Compute how many samples we will send in this transmission
            n = std::min(tx_max_samps_, iqbuf.size() - off);

            // If this is the last segment of the current buffer *and* this is
            // the last buffer, mark this transmission as the end of the burst.
            tx_md.end_of_burst = end_of_burst
                              && off + n == iqbuf.size()
                              && std::next(it) == bufs.end();

            // Send the buffer segment and update the offset into the current
            // buffer.
            n = tx_stream_->send(&iqbuf[off], n, tx_md);

            // Future transmissions do not have time specs and are not the start
            // of a burst.
            tx_md.has_time_spec = false;
            tx_md.start_of_burst = false;
        }

        if (t_next_tx_)
            *t_next_tx_ += static_cast<double>(iqbuf.size() - iqbuf.delay)/tx_rate_;
    }
}

void USRP::stopTXBurst(void)
{
    uhd::tx_metadata_t tx_md; // TX metadata for UHD

    tx_md.end_of_burst = true;

    tx_stream_->send((char *) nullptr, 0, tx_md);

    t_next_tx_ = std::nullopt;
}

void USRP::startRXStream(MonoClock::time_point when)
{
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);

    stream_cmd.stream_now = false;
    stream_cmd.num_samps = 0;
    stream_cmd.time_spec = when.t;
    rx_stream_->issue_stream_cmd(stream_cmd);
}

void USRP::stopRXStream(void)
{
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);

    rx_stream_->issue_stream_cmd(stream_cmd);
}

bool USRP::burstRX(MonoClock::time_point t_start, size_t nsamps, IQBuf& buf)
{
    uhd::time_spec_t t_end = t_start.t + static_cast<double>(nsamps)/rx_rate_;
    size_t           ndelivered = 0;

    buf.fc = rx_freq_;
    buf.fs = rx_rate_;

    for (;;) {
        uhd::rx_metadata_t rx_md;
        ssize_t            n;

        n = rx_stream_->recv(&buf[ndelivered], rx_max_samps_, rx_md, 1, false);

        if (rx_md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
            if (rx_md.has_time_spec)
                logUSRPAt(MonoClock::time_point { rx_md.time_spec },
                          LOGWARNING, "RX error: %s", rx_md.strerror().c_str());
            else
                logUSRP(LOGWARNING, "RX error: %s", rx_md.strerror().c_str());

            if (rx_md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
                // Mark the buffer as complete.
                buf.complete.store(true, std::memory_order_release);

                // We're done, and we've failed
                return false;
            }
        }

        if (n == 0 || rx_md.time_spec < t_start.t)
            continue;

        if (ndelivered == 0) {
            buf.timestamp = MonoClock::time_point { rx_md.time_spec };
            buf.undersample = (rx_md.time_spec - t_start.t).get_real_secs() * rx_rate_;
        }

        ndelivered += n;

        // If we have received enough samples to move us past t_end, stop
        // receiving.
        if (rx_md.time_spec + static_cast<double>(n)/rx_rate_ >= t_end) {
            // Set proper buffer size
            buf.resize(ndelivered);

            // Determine how much we oversampled. Why do we *add* the number of
            // undersamples? Because this represents how many samples "late" we
            // started sampling. If we sample exactly nsamps starting at
            // undersample, then we have sampled undersample too many samples!
            buf.oversample = (ndelivered - nsamps) + buf.undersample;

            // One last store to the atomic nsamples field to ensure write
            // ordering.
            buf.nsamples.store(ndelivered, std::memory_order_release);

            // Mark the buffer as complete.
            buf.complete.store(true, std::memory_order_release);

            // We're done
            return true;
        } else {
            buf.nsamples.store(ndelivered, std::memory_order_release);

            // It's also possible that we don't have enough buffer space to hold
            // upcoming samples if RX started before we expected it to, in which
            // case we also need to exit. We can't just resize the buffer
            // because it may have already been passed to a demodulator running
            // in another thread, and we may not move memory out from under that
            // demodulator. In this case, buf.oversample remains 0. This also
            // leads to us *missing samples*, which is BAD BAD BAD, but we can't
            // dynamically resize the IQ buffer.
            if (buf.size() < ndelivered + rx_max_samps_) {
                logUSRP(LOGERROR,
                    "WARNING: buffer too small to read entire slot: bufsize=%lu, ndelivered=%lu; rx_max_samps=%lu",
                    buf.size(),
                    ndelivered,
                    rx_max_samps_);

                // Mark the buffer as complete.
                buf.complete.store(true, std::memory_order_release);

                // We're done
                return true;
            }
        }

    }
}

void USRP::stop(void)
{
    done_ = true;

    if (tx_error_thread_.joinable())
        tx_error_thread_.join();
}

void USRP::determineDeviceType(void)
{
    std::string mboard = usrp_->get_mboard_name();

    if (mboard.find("N210") == 0)
        device_type_ = kUSRPN210;
    else if (mboard.find("X310") == 0)
        device_type_ = kUSRPX310;
    else
        device_type_ = kUSRPUnknown;
}

void USRP::txErrorWorker(void)
{
    uhd::async_metadata_t async_md;
    const char *msg;

    while (!done_) {
        msg = nullptr;

        if (tx_stream_->recv_async_msg(async_md, 0.1)) {
            switch(async_md.event_code){
                case uhd::async_metadata_t::EVENT_CODE_BURST_ACK:
                    break;

                case uhd::async_metadata_t::EVENT_CODE_UNDERFLOW:
                    msg = "TX error: an internal send buffer has emptied";
                    tx_underflow_count_.fetch_add(1, std::memory_order_relaxed);
                    break;

                case uhd::async_metadata_t::EVENT_CODE_SEQ_ERROR:
                    msg = "TX error: packet loss between host and device";
                    break;

                case uhd::async_metadata_t::EVENT_CODE_TIME_ERROR:
                    msg = "TX error: packet had time that was late";
                    tx_late_count_.fetch_add(1, std::memory_order_relaxed);
                    break;

                case uhd::async_metadata_t::EVENT_CODE_UNDERFLOW_IN_PACKET:
                    msg = "TX error: underflow occurred inside a packet";
                    tx_underflow_count_.fetch_add(1, std::memory_order_relaxed);
                    break;

                case uhd::async_metadata_t::EVENT_CODE_SEQ_ERROR_IN_BURST:
                    msg = "TX error: packet loss within a burst";
                    break;

                case uhd::async_metadata_t::EVENT_CODE_USER_PAYLOAD:
                    msg = "TX error: some kind of custom user payload";
                    break;
            }
        }

        if (msg) {
            if (async_md.has_time_spec)
                logUSRPAt(MonoClock::time_point { async_md.time_spec },
                          LOGWARNING, "%s", msg);
            else
                logUSRP(LOGWARNING, "%s", msg);
        }
    }
}
