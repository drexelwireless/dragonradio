#include <sys/types.h>
#include <sys/time.h>

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
    while (not usrp->get_rx_sensor("lo_locked").to_bool())
        //sleep for a short time in milliseconds
        usleep(10);

    // Set USRP time relative to system NTP time
    timeval tv;

    gettimeofday(&tv, NULL);

    usrp->set_time_now(uhd::time_spec_t(tv.tv_sec, ((double)tv.tv_usec)/1e6));

    // Set up USRP streaming
    uhd::stream_args_t stream_args("fc32");

    tx_stream = usrp->get_tx_stream(stream_args);
    rx_stream = usrp->get_rx_stream(stream_args);

    // Turn on DC offset correction
    usrp->set_tx_dc_offset(true);
    usrp->set_rx_dc_offset(true);
}

USRP::~USRP()
{
}

uhd::time_spec_t USRP::get_time_now(void)
{
    return usrp->get_time_now(0);
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

void USRP::burstTX(uhd::time_spec_t when, std::deque<std::shared_ptr<IQBuf>>& bufs)
{
    for (auto it = bufs.begin(); it != bufs.end(); ++it) {
        uhd::tx_metadata_t tx_md;
        IQBuf&             iqbuf = **it;

        tx_md.time_spec = when;
        tx_md.has_time_spec = true;

        if (it == bufs.begin())
            tx_md.start_of_burst = true;
        else if (std::next(it) == bufs.end())
            tx_md.end_of_burst = true;

        tx_stream->send(&iqbuf[0], iqbuf.size(), tx_md);
    }
}
void USRP::startRXStream(uhd::time_spec_t when)
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

const size_t MAXSAMPS = 2048;

std::unique_ptr<IQBuf> USRP::burstRX(uhd::time_spec_t t_start, size_t nsamps)
{
    const double     txRate = usrp->get_rx_rate(); // TX rate in Hz
    uhd::time_spec_t t_end = t_start + static_cast<double>(nsamps)/txRate;
    size_t           ndelivered = 0;
    auto             buf = std::make_unique<IQBuf>(nsamps + MAXSAMPS);

    for (;;) {
        uhd::rx_metadata_t rx_md;
        ssize_t            n;

        n = rx_stream->recv(&(*buf)[ndelivered], MAXSAMPS, rx_md, 0.1, false);

        if (rx_md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE)
            fprintf(stderr, "RX error: %s\n", rx_md.strerror().c_str());

        if (ndelivered == 0)
            buf->set_timestamp(rx_md.time_spec);

        ndelivered += n;

        if (rx_md.time_spec + static_cast<double>(n)/txRate >= t_end)
            break;
    }

    buf->resize(ndelivered);

    return buf;
}
