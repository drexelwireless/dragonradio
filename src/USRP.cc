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
}

USRP::~USRP()
{
}

double USRP::get_time_now(void)
{
    uhd::time_spec_t uhd_system_time_now = usrp->get_time_now(0);

    return (double)(uhd_system_time_now.get_full_secs()) + (double)(uhd_system_time_now.get_frac_secs());
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

void USRP::burstTX(double when, std::deque<std::unique_ptr<IQBuffer>>& bufs)
{
    double full;
    double frac;

    frac = modf(when, &full);

    for (auto it = bufs.begin(); it != bufs.end(); ++it) {
        uhd::tx_metadata_t tx_md;
        IQBuffer&          iqbuf = **it;

        tx_md.time_spec = uhd::time_spec_t((time_t) full, frac);
        tx_md.has_time_spec = true;

        if (it == bufs.begin())
            tx_md.start_of_burst = true;
        else if (std::next(it) == bufs.end())
            tx_md.end_of_burst = true;

        tx_stream->send(&iqbuf[0], iqbuf.size(), tx_md);
    }
}

std::unique_ptr<IQBuffer> USRP::burstRX(double when, size_t nsamps)
{
    size_t                    ndelivered = 0;
    const size_t              maxSamps = usrp->get_device()->get_max_recv_samps_per_packet();
    double                    full;
    double                    frac;
    std::unique_ptr<IQBuffer> buf(new IQBuffer);

    frac = modf(when, &full);

    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_MORE);

    stream_cmd.stream_now = false;
    stream_cmd.time_spec = uhd::time_spec_t((time_t) full, frac);
    rx_stream->issue_stream_cmd(stream_cmd);

    while (ndelivered < nsamps) {
        uhd::rx_metadata_t rx_md;
        ssize_t            n;

        buf->resize(ndelivered + maxSamps);

        n = usrp->get_device()->recv(&(*buf)[ndelivered], maxSamps, rx_md,
                                     uhd::io_type_t::COMPLEX_FLOAT32,
                                     uhd::device::RECV_MODE_ONE_PACKET);

        if (rx_md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE)
            fprintf(stderr, "RX error: %s\n", rx_md.strerror().c_str());

        ndelivered += n;
    }

    buf->resize(ndelivered);

    return buf;
}
