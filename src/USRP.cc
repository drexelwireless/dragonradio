#include <sys/types.h>
#include <sys/time.h>

#include "USRP.hh"

USRP::USRP(const std::string& addr,
           double center_freq,
           const std::string& tx_ant,
           const std::string& rx_ant,
           float tx_gain,
           float rx_gain)
  : usrp(uhd::usrp::multi_usrp::make(addr))
{
    usrp->set_tx_antenna(tx_ant);
    usrp->set_rx_antenna(rx_ant);

    usrp->set_tx_gain(tx_gain);
    usrp->set_rx_gain(rx_gain);

    usrp->set_tx_freq(center_freq);
    usrp->set_rx_freq(center_freq);

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

size_t USRP::get_max_send_samps_per_packet(void)
{
    return usrp->get_device()->get_max_send_samps_per_packet();
}

size_t USRP::get_max_recv_samps_per_packet(void)
{
    return usrp->get_device()->get_max_recv_samps_per_packet();
}

void USRP::recv_at(double when)
{
    double full;
    double frac;

    frac = modf(when, &full);

    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_MORE);

    stream_cmd.stream_now = false;
    stream_cmd.time_spec = uhd::time_spec_t((time_t) full, frac);
    rx_stream->issue_stream_cmd(stream_cmd);
}

size_t USRP::recv(const std::complex<float>* buf, size_t count)
{
    uhd::rx_metadata_t rx_md;

    return usrp->get_device()->recv(buf, count, rx_md,
        uhd::io_type_t::COMPLEX_FLOAT32,
        uhd::device::RECV_MODE_ONE_PACKET);
}

void USRP::start_burst(void)
{
    tx_md.start_of_burst = true;
}

void USRP::end_burst(void)
{
    tx_md.end_of_burst = true;
}

size_t USRP::send(double when, const std::complex<float>* buf, size_t count)
{
    double full;
    double frac;
    size_t ret;

    frac = modf(when, &full);

    tx_md.time_spec = uhd::time_spec_t((time_t) full, frac);
    tx_md.has_time_spec = true;

    ret = tx_stream->send(buf, count, tx_md);

    tx_md.start_of_burst = false;
    tx_md.end_of_burst = false;

    return ret;
}
