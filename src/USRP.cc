#include <sys/types.h>
#include <sys/time.h>

#include <atomic>

#include "Clock.hh"
#include "USRP.hh"

USRP::USRP(const std::string& addr,
           bool x310,
           double center_freq,
           const std::string& tx_ant,
           const std::string& rx_ant,
           float tx_gain,
           float rx_gain)
  : usrp(uhd::usrp::multi_usrp::make(addr)), x310(x310)
{
    usrp->set_tx_antenna(tx_ant);
    usrp->set_rx_antenna(rx_ant);

    usrp->set_tx_gain(tx_gain);
    usrp->set_rx_gain(rx_gain);

    // See:
    //   https://sc2colosseum.freshdesk.com/support/solutions/articles/22000220403-optimizing-srn-usrp-performance
    if (x310) {
        double lo_offset = -42.0e6;
        usrp->set_tx_freq(uhd::tune_request_t(center_freq, lo_offset));
    } else
        usrp->set_tx_freq(center_freq);

    if (x310) {
        double lo_offset = +42.0e6;
        usrp->set_rx_freq(uhd::tune_request_t(center_freq, lo_offset));
    } else
        usrp->set_rx_freq(center_freq);

    // See:
    //   https://files.ettus.com/manual/page_general.html
    while (not usrp->get_tx_sensor("lo_locked").to_bool())
        //sleep for a short time in milliseconds
        usleep(10);

    while (not usrp->get_rx_sensor("lo_locked").to_bool())
        usleep(10);

    // Set up clock
    Clock::setUSRP(usrp);

    // Set up USRP streaming
    uhd::stream_args_t stream_args("fc32");

    tx_stream = usrp->get_tx_stream(stream_args);
    rx_stream = usrp->get_rx_stream(stream_args);

    // Turn on DC offset correction
    usrp->set_tx_dc_offset(true);
    usrp->set_rx_dc_offset(true);

    // During TX and RX, attempt to send/receive 8x the maximum number of
    // samples in a packet
    _tx_max_samps = 8*tx_stream->get_max_num_samps();
    _rx_max_samps = 8*rx_stream->get_max_num_samps();
}

USRP::~USRP()
{
}

double USRP::get_tx_rate(void)
{
    return usrp->get_tx_rate();
}

void USRP::set_tx_rate(double rate)
{
    usrp->set_tx_rate(rate);
}

double USRP::get_rx_rate(void)
{
    return usrp->get_rx_rate();
}

void USRP::set_rx_rate(double rate)
{
    usrp->set_rx_rate(rate);
}

void USRP::burstTX(Clock::time_point when, std::list<std::shared_ptr<IQBuf>>& bufs)
{
    uhd::tx_metadata_t tx_md; // TX metadata for UHD
    size_t             n;     // Size of next send

    tx_md.time_spec = when;
    tx_md.has_time_spec = true;
    tx_md.start_of_burst = true;

    // We walk through the supplied queue of buffers and trasmit each in chunks
    // whose size is no more than _tx_max_samps bytes, which is the maximum size
    // of a USRP TX packet. This allows us to avoid being "late" even when we
    // have a very large buffer to send.
    for (auto it = bufs.begin(); it != bufs.end(); ++it) {
        IQBuf& iqbuf = **it; // Current buffer we are sending

        for (size_t off = 0; off < iqbuf.size(); off += n) {
            // Compute how many samples we will send in this transmission
            n = std::min(_tx_max_samps, iqbuf.size() - off);

            // If this is the last segment of the current buffer *and* this is
            // the last buffer, mark this transmission as the end of the burst.
            tx_md.end_of_burst = off + n == iqbuf.size()
                              && std::next(it) == bufs.end();

            // Send the buffer segment and update the offset into the current
            // buffer.
            n = tx_stream->send(&iqbuf[off], n, tx_md);

            // Future transmissions do not have time specs and are not the start
            // of a burst.
            tx_md.has_time_spec = false;
            tx_md.start_of_burst = false;
        }
    }
}

void USRP::startRXStream(Clock::time_point when)
{
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);

    stream_cmd.stream_now = false;
    stream_cmd.num_samps = 0;
    stream_cmd.time_spec = when;
    rx_stream->issue_stream_cmd(stream_cmd);
}

void USRP::stopRXStream(void)
{
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);

    rx_stream->issue_stream_cmd(stream_cmd);
}

void USRP::burstRX(Clock::time_point t_start, size_t nsamps, IQBuf& buf)
{
    const double      txRate = usrp->get_rx_rate(); // TX rate in Hz
    Clock::time_point t_end = t_start + static_cast<double>(nsamps)/txRate;
    size_t            ndelivered = 0;

    buf.resize(nsamps + _rx_max_samps);

    for (;;) {
        uhd::rx_metadata_t rx_md;
        ssize_t            n;

        n = rx_stream->recv(&buf[ndelivered], _rx_max_samps, rx_md, 0.1, false);

        if (rx_md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE)
            fprintf(stderr, "RX error: %s\n", rx_md.strerror().c_str());

        if (ndelivered == 0) {
            buf.timestamp = rx_md.time_spec;
            buf.undersample = (buf.timestamp - t_start).get_real_secs() * txRate;
        }

        ndelivered += n;

        // If we have received enough samples to move us past t_end, stop
        // receiving.
        if (rx_md.time_spec + static_cast<double>(n)/txRate >= t_end) {
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
        } else
            buf.nsamples.store(ndelivered, std::memory_order_release);
    }
}
