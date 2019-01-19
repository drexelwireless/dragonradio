#include "phy/PHY.hh"
#include "phy/ChannelModulator.hh"

void ChannelModulator::modulate(const Channel &channel,
                                std::shared_ptr<NetPacket> pkt,
                                ModPacket &mpkt)
{
    // Modulate the packet
    mod_->modulate(std::move(pkt), mpkt);

    // Upsample if needed
    if (rad_ != 0.0 || rate_ != 1.0) {
        // Get samples from ModPacket
        auto iqbuf = std::move(mpkt.samples);

        // Append zeroes to compensate for delay
        iqbuf->append(ceil(resamp_.getDelay()));

        // Resample and mix up
        auto     iqbuf_up = std::make_shared<IQBuf>(resamp_.neededOut(iqbuf->size()));
        unsigned nw;

        nw = resamp_.resampleMixUp(iqbuf->data(), iqbuf->size(), iqbuf_up->data());
        assert(nw <= iqbuf_up->size());
        iqbuf_up->resize(nw);

        // Indicate delay
        iqbuf_up->delay = floor(resamp_.getRate()*resamp_.getDelay());

        // Put samples back into ModPacket
        mpkt.samples = std::move(iqbuf_up);
    }

    // Set channel
    mpkt.channel = channel;
}
