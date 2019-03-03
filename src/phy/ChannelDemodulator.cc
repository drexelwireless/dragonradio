#include "phy/PHY.hh"
#include "phy/ChannelDemodulator.hh"

void ChannelDemodulator::demodulate(IQBuf &resamp_buf,
                                    const std::complex<float>* data,
                                    size_t count,
                                    std::function<void(std::unique_ptr<RadioPacket>)> callback)
{
    if (rad_ != 0.0 || rate_ != 1.0) {
        // Resample. Note that we can't very well mix without a frequency shift,
        // so we are guaranteed that the resampler's rate is not 1 here.
        unsigned nw;

        resamp_buf.resize(resamp_.neededOut(count));
        nw = resamp_.resampleMixDown(data, count, resamp_buf.data());
        resamp_buf.resize(nw);

        // Demodulate resampled data.
        demod_->demodulate(resamp_buf.data(), resamp_buf.size(), callback);
    } else
        demod_->demodulate(data, count, callback);
}
