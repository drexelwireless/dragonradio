#ifndef PHY_HH_
#define PHY_HH_

#include <complex>
#include <mutex>
#include <optional>

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

#include <liquid/liquid.h>

#include "phy/MCS.hh"

namespace py = pybind11;

extern std::mutex liquid_mutex;

struct Header {
    Header()
      : curhop(0)
      , nexthop(0)
      , flags(0)
      , seq(0)
      , data_len(0)
    {
    }

    Header(uint8_t curhop,
           uint8_t nexthop,
           uint16_t flags,
           uint16_t seq,
           uint16_t data_len)
      : curhop(curhop)
      , nexthop(nexthop)
      , flags(flags)
      , seq(seq)
      , data_len(data_len)
    {
    }

    uint8_t curhop;
    uint8_t nexthop;
    uint16_t flags;
    uint16_t seq;
    uint16_t data_len;
};

struct FrameStats {
    float evm;
    float rssi;
    float cfo;

    unsigned int mod_scheme;
    unsigned int mod_bps;
    unsigned int check;
    unsigned int fec0;
    unsigned int fec1;
};

inline void stats2framestats(const framesyncstats_s &src, FrameStats &dest)
{
    dest.evm = src.evm;
    dest.rssi = src.rssi;
    dest.cfo = src.cfo;
    dest.mod_scheme = src.mod_scheme;
    dest.mod_bps = src.mod_bps;
    dest.check = src.check;
    dest.fec0 = src.fec0;
    dest.fec1 = src.fec1;
}

class Modulator {
public:
    Modulator()
    {
    }

    virtual ~Modulator()
    {
    }

    const MCS &getHeaderMCS() const
    {
        return header_mcs_;
    }

    virtual void setHeaderMCS(const MCS &mcs) = 0;

    const MCS &getPayloadMCS() const
    {
        return payload_mcs_;
    }

    virtual void setPayloadMCS(const MCS &mcs) = 0;

    py::array_t<std::complex<float>> modulate(const Header &hdr, py::buffer payload);

protected:
    MCS header_mcs_;
    MCS payload_mcs_;

    virtual void assemble(const void *header, const void *payload, const size_t payload_len) = 0;

    virtual size_t maxModulatedSamples(void) = 0;

    virtual bool modulateSamples(std::complex<float> *buf, size_t &nw) = 0;
};

class Demodulator {
public:
    using demod_vec = std::vector<std::tuple<std::optional<Header>,
                                  std::optional<py::bytes>,
                                  FrameStats>>;

    Demodulator(bool soft_header,
                bool soft_payload)
      : soft_header_(soft_header)
      , soft_payload_(soft_payload)
    {
    }

    virtual ~Demodulator()
    {
    }

    const MCS &getHeaderMCS() const
    {
        return header_mcs_;
    }

    virtual void setHeaderMCS(const MCS &mcs) = 0;

    bool getSoftHeader() const
    {
        return soft_header_;
    }

    bool getSoftPayload() const
    {
        return soft_payload_;
    }

    virtual void reset(void) = 0;

    virtual demod_vec demodulate(py::array_t<std::complex<float>, py::array::c_style | py::array::forcecast> sig);

protected:
    MCS header_mcs_;
    MCS payload_mcs_;

    const bool soft_header_;
    const bool soft_payload_;

    demod_vec *packets_;

    virtual void demodulateSamples(const std::complex<float> *buf, const size_t n) = 0;

    static int liquid_callback(unsigned char *  header_,
                               int              header_valid_,
                               unsigned char *  payload_,
                               unsigned int     payload_len_,
                               int              payload_valid_,
                               framesyncstats_s stats_,
                               void *           userdata_);

    virtual int callback(unsigned char *  header_,
                         int              header_valid_,
                         unsigned char *  payload_,
                         unsigned int     payload_len_,
                         int              payload_valid_,
                         framesyncstats_s stats_);
};

#endif /* PHY_HH_ */
