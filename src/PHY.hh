#ifndef PHY_H_
#define PHY_H_

#include <vector>
#include <complex>

#include <liquid/liquid.h>
#include <liquid/multichannelrx.h>
#include <liquid/multichanneltx.h>

#include "ModPacket.hh"
#include "NET.hh"
#include "Node.hh"
#include "PHY.hh"
#include "SafeQueue.hh"
#include "USRP.hh"

class Modulator
{
public:
    Modulator(size_t minPacketSize);
    ~Modulator();

    Modulator(const Modulator&) = delete;
    Modulator(Modulator&& other) :
        minPacketSize(other.minPacketSize),
        mctx(std::move(other.mctx))
        {}

    Modulator& operator=(const Modulator&) = delete;
    Modulator& operator=(Modulator&&) = delete;

    std::unique_ptr<ModPacket> modulate(std::unique_ptr<NetPacket> pkt);

private:
    size_t minPacketSize;

    std::unique_ptr<multichanneltx> mctx;
};

class Demodulator
{
public:
    Demodulator(std::shared_ptr<NET> net);
    ~Demodulator();

    Demodulator(const Demodulator&) = delete;
    Demodulator(Demodulator&& other) :
        net(other.net),
        mcrx(std::move(other.mcrx)),
        pkts(std::move(other.pkts))
        {}

    Demodulator& operator=(const Demodulator&) = delete;
    Demodulator& operator=(Demodulator&&) = delete;

    void demodulate(std::unique_ptr<IQQueue> buf, std::queue<std::unique_ptr<RadioPacket>>& q);

private:
    std::shared_ptr<NET> net;

    std::unique_ptr<multichannelrx> mcrx;

    std::queue<std::unique_ptr<RadioPacket>>* pkts;

    static int liquidRxCallback(unsigned char *  _header,
                                int              _header_valid,
                                unsigned char *  _payload,
                                unsigned int     _payload_len,
                                int              _payload_valid,
                                framesyncstats_s _stats,
                                void *           _userdata,
                                liquid_float_complex* G,
                                liquid_float_complex* G_hat,
                                unsigned int M);


    int rxCallback(unsigned char *  _header,
                   int              _header_valid,
                   unsigned char *  _payload,
                   unsigned int     _payload_len,
                   int              _payload_valid,
                   framesyncstats_s _stats,
                   liquid_float_complex* G,
                   liquid_float_complex* G_hat,
                   unsigned int M);
};

class PHY
{
public:
    PHY(std::shared_ptr<NET> net,
        double bandwidth,
        size_t minPacketSize) :
        net(net),
        bandwidth(bandwidth),
        minPacketSize(minPacketSize)
    {
    }

    // MultiChannel TX/RX requires oversampling by a factor of 2
    double getRxRate(void) const
    {
        return 2*bandwidth;
    }

    double getTxRate(void) const
    {
        return 2*bandwidth;
    }

    Demodulator make_demodulator(void) const
    {
        return Demodulator(net);
    }

    Modulator make_modulator(void) const
    {
        return Modulator(minPacketSize);
    }

private:
    std::shared_ptr<NET> net;
    double bandwidth;
    size_t minPacketSize;
};

#endif /* PHY_H_ */
