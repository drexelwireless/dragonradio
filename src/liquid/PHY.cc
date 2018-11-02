#include "liquid/PHY.hh"

int Liquid::Demodulator::liquid_callback(unsigned char *  header_,
                                         int              header_valid_,
                                         unsigned char *  payload_,
                                         unsigned int     payload_len_,
                                         int              payload_valid_,
                                         framesyncstats_s stats_,
                                         void *           userdata_)
{
    if (userdata_) {
        Demodulator *demod = static_cast<Demodulator*>(userdata_);

        return demod->callback(header_,
                               header_valid_,
                               payload_,
                               payload_len_,
                               payload_valid_,
                               stats_);
    } else
        return 0;
}

int Liquid::Demodulator::callback(unsigned char *  header_,
                                  int              header_valid_,
                                  unsigned char *  payload_,
                                  unsigned int     payload_len_,
                                  int              payload_valid_,
                                  framesyncstats_s stats_)
{
    cb_(header_valid_,
        reinterpret_cast<const Header*>(header_),
        payload_valid_,
        payload_,
        payload_len_,
        stats_);

    return 0;
}

void Liquid::Demodulator::demodulate(const std::complex<float> *in,
                                     const size_t n,
                                     callback_t cb)
{
    cb_ = cb;

    try {
        demodulateSamples(in, n);
    } catch(...) {
        cb_ = nullptr;
        throw;
    }

    cb_ = nullptr;
}
