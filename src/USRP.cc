#include <sys/types.h>
#include <sys/time.h>

#include <atomic>

#include "Clock.hh"
#include "Logger.hh"
#include "RadioConfig.hh"
#include "USRP.hh"

USRP::USRP(const std::string& addr,
           double freq,
           const std::string& tx_ant,
           const std::string& rx_ant,
           float tx_gain,
           float rx_gain)
  : usrp_(uhd::usrp::multi_usrp::make(addr))
  , auto_dc_offset_(false)
  , done_(false)
{
    determineDeviceType();

    usrp_->set_tx_antenna(tx_ant);
    usrp_->set_rx_antenna(rx_ant);

    usrp_->set_tx_gain(tx_gain);
    usrp_->set_rx_gain(rx_gain);

    setRXFrequency(freq);

    // Set up clock
    setTXFrequency(freq);
    Clock::setUSRP(usrp_);

    // Set up USRP streaming
    uhd::stream_args_t stream_args("fc32");

    tx_stream_ = usrp_->get_tx_stream(stream_args);
    rx_stream_ = usrp_->get_rx_stream(stream_args);

    // Set maximum number of samples we attempt to TX/RX.
    if (device_type_ == kUSRPX310) {
        tx_max_samps_ = 8*tx_stream_->get_max_num_samps();
        rx_max_samps_ = 8*rx_stream_->get_max_num_samps();
    } else {
        tx_max_samps_ = 512;
        rx_max_samps_ = 2048;
    }

    // Start thread that receives TX errors
    tx_error_thread_ = std::thread(&USRP::txErrorWorker, this);
}

USRP::~USRP()
{
    stop();
}

// See the following for X310 LO offset advice:
//   https://sc2colosseum.freshdesk.com/support/solutions/articles/22000220403-optimizing-srn-usrp-performance
//
// See the following for instructions of waitign for LO to settle:
//   https://files.ettus.com/manual/page_general.html

void USRP::setTXFrequency(double freq)
{
    if (device_type_ == kUSRPX310) {
        double lo_offset = -42.0e6;
        usrp_->set_tx_freq(uhd::tune_request_t(freq, lo_offset));
    } else
        usrp_->set_tx_freq(freq);

    while (!usrp_->get_tx_sensor("lo_locked").to_bool())
        usleep(10);
}

void USRP::setRXFrequency(double freq)
{
    if (device_type_ == kUSRPX310) {
        double lo_offset = +42.0e6;
        usrp_->set_rx_freq(uhd::tune_request_t(freq, lo_offset));
    } else
        usrp_->set_rx_freq(freq);

    while (!usrp_->get_rx_sensor("lo_locked").to_bool())
        usleep(10);
}

void USRP::burstTX(MonoClock::time_point when, std::list<std::shared_ptr<IQBuf>>& bufs)
{
    const double       txRate = usrp_->get_tx_rate(); // TX rate in Hz
    uhd::tx_metadata_t tx_md; // TX metadata for UHD
    size_t             n;     // Size of next send

    tx_md.time_spec = when.t;
    tx_md.has_time_spec = true;
    tx_md.start_of_burst = true;

    // We walk through the supplied queue of buffers and trasmit each in chunks
    // whose size is no more than tx_max_samps_ bytes, which is the maximum size
    // of a USRP TX packet. This allows us to avoid being "late" even when we
    // have a very large buffer to send.
    for (auto it = bufs.begin(); it != bufs.end(); ++it) {
        IQBuf& iqbuf = **it; // Current buffer we are sending

        iqbuf.timestamp = Clock::to_wall_time(when);

        for (size_t off = iqbuf.delay; off < iqbuf.size(); off += n) {
            // Compute how many samples we will send in this transmission
            n = std::min(tx_max_samps_, iqbuf.size() - off);

            // If this is the last segment of the current buffer *and* this is
            // the last buffer, mark this transmission as the end of the burst.
            tx_md.end_of_burst = off + n == iqbuf.size()
                              && std::next(it) == bufs.end();

            // Send the buffer segment and update the offset into the current
            // buffer.
            n = tx_stream_->send(&iqbuf[off], n, tx_md);

            // Future transmissions do not have time specs and are not the start
            // of a burst.
            tx_md.has_time_spec = false;
            tx_md.start_of_burst = false;
        }

        when += static_cast<double>(iqbuf.size())/txRate;
    }
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
    const double     rxRate = usrp_->get_rx_rate(); // RX rate in Hz
    uhd::time_spec_t t_end = t_start.t + static_cast<double>(nsamps)/rxRate;
    size_t           ndelivered = 0;

    buf.fc = usrp_->get_rx_freq();
    buf.fs = rxRate;

    buf.resize(nsamps + rx_max_samps_);

    for (;;) {
        uhd::rx_metadata_t rx_md;
        ssize_t            n;

        n = rx_stream_->recv(&buf[ndelivered], rx_max_samps_, rx_md, 0.1, false);

        if (rx_md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
            fprintf(stderr, "RX error: %s\n", rx_md.strerror().c_str());

            if (rx_md.has_time_spec)
                logEventAt(Clock::to_wall_time(MonoClock::time_point { rx_md.time_spec }), "RX error: %s", rx_md.strerror().c_str());
            else
                logEvent("RX error: %s", rx_md.strerror().c_str());

            if (rx_md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT)
                return false;
        }

        if (n == 0 || rx_md.time_spec < t_start.t)
            continue;

        if (ndelivered == 0) {
            buf.timestamp = Clock::to_wall_time(MonoClock::time_point { rx_md.time_spec });
            buf.undersample = (rx_md.time_spec - t_start.t).get_real_secs() * rxRate;
        }

        ndelivered += n;

        // If we have received enough samples to move us past t_end, stop
        // receiving.
        if (rx_md.time_spec + static_cast<double>(n)/rxRate >= t_end) {
            // Set proper buffer size
            buf.resize(ndelivered);

            // Determine how much we oversampled. Why do we *add* the number of
            // undersamples? Because this represents how many samples "late" we
            // started sampling. If we sample nsamps starting at undersample,
            // then we have sampled undersample too many samples!
            buf.oversample = (ndelivered - nsamps) + buf.undersample;

            // Mark the buffer as complete.
            buf.complete = true;

            // One last store to the atomic nsamples field to ensure write
            // ordering.
            buf.nsamples.store(ndelivered, std::memory_order_release);
            break;
        } else {
            // It's possible that we don't have enough buffer space to hold
            // upcoming samples if RX started before we expected it to, so
            // resize our buffer if needed.
            if (buf.size() < ndelivered + rx_max_samps_)
                buf.resize(buf.size() + rx_max_samps_);

            buf.nsamples.store(ndelivered, std::memory_order_release);
        }
    }

    return true;
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
                    break;

                case uhd::async_metadata_t::EVENT_CODE_SEQ_ERROR:
                    msg = "TX error: packet loss between host and device";
                    break;

                case uhd::async_metadata_t::EVENT_CODE_TIME_ERROR:
                    msg = "TX error: packet had time that was late";
                    break;

                case uhd::async_metadata_t::EVENT_CODE_UNDERFLOW_IN_PACKET:
                    msg = "TX error: underflow occurred inside a packet";
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
            if (rc.verbose && !rc.debug)
                fprintf(stderr, "%s\n", msg);
            if (async_md.has_time_spec)
                logEventAt(Clock::to_wall_time(MonoClock::time_point { async_md.time_spec }), "%s", msg);
            else
                logEvent("%s", msg);
        }
    }
}
