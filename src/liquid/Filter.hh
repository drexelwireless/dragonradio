#ifndef LIQUID_FILTER_HH_
#define LIQUID_FILTER_HH_

#include <complex>

#include <liquid/liquid.h>

#include "dsp/Filter.hh"

namespace Liquid {

class FIRFilter : public ::FIRFilter
{
public:
    FIRFilter() = delete;

    FIRFilter(FIRFilter &&f)
    {
        firfilt_cccf_destroy(q_);
        q_ = f.q_;
        f.q_ = nullptr;
    }

    FIRFilter(const std::vector<std::complex<float>> &h)
    {
        q_ = firfilt_cccf_create(const_cast<std::complex<float>*>(h.data()), h.size());
        delay_ = (h.size() - 1.0)/2.0;
    }

    virtual ~FIRFilter()
    {
        if (q_)
            firfilt_cccf_destroy(q_);
    }

    FIRFilter& operator=(const std::vector<std::complex<float>> &h)
    {
        firfilt_cccf_recreate(q_, const_cast<std::complex<float>*>(h.data()), h.size());
        delay_ = (h.size() - 1.0)/2.0;

        return *this;
    }

    FIRFilter& operator=(FIRFilter &&f)
    {
        firfilt_cccf_destroy(q_);
        q_ = f.q_;
        f.q_ = nullptr;

        return *this;
    }

    FIRFilter& operator=(const FIRFilter &) = delete;

    float getGroupDelay(float fc) override final
    {
        return firfilt_cccf_groupdelay(q_, fc);
    }

    void reset(void) override final
    {
        firfilt_cccf_reset(q_);
    }

    void execute(const std::complex<float> *in, std::complex<float> *out, size_t n) override final
    {
        firfilt_cccf_execute_block(q_,
                                   const_cast<std::complex<float>*>(in),
                                   n,
                                   out);
    }

    float getDelay(void) override final
    {
        return delay_;
    }

    /** @brief Get output scaling for filter
     * @return Scaling factor applied to each output sample
     */
    std::complex<float> getScale() const
    {
        std::complex<float> scale;

        firfilt_cccf_get_scale(q_, &scale);
        return scale;
    }

    /** @brief Set output scaling for filter
     * @param scale Scaling factor applied to each output sample
     */
    void setScale(std::complex<float> scale)
    {
        firfilt_cccf_set_scale(q_, scale);
    }

    /** @brief Print filter object information to stdout */
    void print(void) const
    {
        firfilt_cccf_print(q_);
    }

protected:
    firfilt_cccf q_;
    float delay_;
};

/** @brief Construct a lowpass filter using Liquid's Parks-McClellan implementation
 * @param n Filter length
 * @param fc cutoff frequency
 * @param As stop-band attenuation (dB)
 * @return A vector of FIR coefficients
 */
std::vector<float> parks_mcclellan(unsigned n,
                                   float    fc,
                                   float    As);

/** @brief Construct a lowpass filter using Liquid's Kaiser window implementation
 * @param n Filter length
 * @param fc cutoff frequency
 * @param As stop-band attenuation (dB)
 * @return A vector of FIR coefficients
 */
std::vector<float> kaiser(unsigned n,
                          float    fc,
                          float    As);

class IIRFilter : public ::IIRFilter
{
public:
    IIRFilter() = delete;

    IIRFilter(IIRFilter &&f)
    {
        q_ = f.q_;
        f.q_ = nullptr;
    }

    IIRFilter(const std::complex<float> *b, unsigned Nb,
              const std::complex<float> *a, unsigned Na)
    {
        q_ = iirfilt_cccf_create(const_cast<std::complex<float>*>(b), Nb,
                                 const_cast<std::complex<float>*>(a), Na);
    }

    IIRFilter(const std::complex<float> *sos, unsigned N)
    {
        std::vector<std::complex<float>> b(3*N);
        std::vector<std::complex<float>> a(3*N);

        for (unsigned i = 0; i < N; ++i) {
            for (unsigned j = 0; j < 3; ++j) {
                b[3*i + j] = sos[6*i + j];
                a[3*i + j] = sos[6*i + j + 3];
            }
        }

        q_ = iirfilt_cccf_create_sos(b.data(), a.data(), N);
    }

    virtual ~IIRFilter()
    {
        if (q_)
            iirfilt_cccf_destroy(q_);
    }

    IIRFilter& operator=(const IIRFilter &) = delete;
    IIRFilter& operator=(IIRFilter &&) = delete;

    void reset(void) override final
    {
        iirfilt_cccf_reset(q_);
    }

    void execute(const std::complex<float> *in, std::complex<float> *out, size_t n) override final
    {
        iirfilt_cccf_execute_block(q_,
                                   const_cast<std::complex<float>*>(in),
                                   n,
                                   out);
    }

    float getGroupDelay(float fc) override final
    {
        return iirfilt_cccf_groupdelay(q_, fc);
    }

    /** @brief Print filter object information to stdout */
    void print(void) const
    {
        iirfilt_cccf_print(q_);
    }

protected:
    iirfilt_cccf q_;
};

std::tuple<std::vector<float>, std::vector<float>>
butter_lowpass(unsigned int N,
               float fc,
               float f0,
               float Ap,
               float As);
}

#endif /* LIQUID_FILTER_HH_ */
