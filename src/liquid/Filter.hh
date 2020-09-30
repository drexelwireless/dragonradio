#ifndef LIQUID_FILTER_HH_
#define LIQUID_FILTER_HH_

#include <complex>
#include <tuple>
#include <vector>

#include <liquid/liquid.h>

#include "dsp/Filter.hh"

#if defined(DOXYGEN)
#define final
#endif /* defined(DOXYGEN) */

namespace Liquid {

using C = std::complex<float>;
using F = float;

/** @brief An FIR filter */
template <class I, class O, class C>
class FIR : public ::FIR<I,O,C>
{
public:
    FIR() = delete;

    FIR(FIR &&f)
    {
        static_assert(sizeof(I) == 0, "Only specializations of Liquid::FIR can be used");
    }

    FIR(const std::vector<C> &h)
    {
        static_assert(sizeof(I) == 0, "Only specializations of Liquid::FIR can be used");
    }

    virtual ~FIR()
    {
        static_assert(sizeof(I) == 0, "Only specializations of Liquid::FIR can be used");
    }

    FIR& operator=(const std::vector<C> &h)
    {
        static_assert(sizeof(I) == 0, "Only specializations of Liquid::FIR can be used");
    }

    FIR& operator=(FIR &&f)
    {
        static_assert(sizeof(I) == 0, "Only specializations of Liquid::FIR can be used");
    }

    FIR& operator=(const FIR &) = delete;

    float getGroupDelay(float fc) const override final
    {
        static_assert(sizeof(I) == 0, "Only specializations of Liquid::FIR can be used");
        return 0;
    }

    void reset(void) override final
    {
        static_assert(sizeof(I) == 0, "Only specializations of Liquid::FIR can be used");
    }

    /** @brief Execute the filter */
    void execute(const I *in, O *out, size_t n) override final
    {
        static_assert(sizeof(I) == 0, "Only specializations of Liquid::FIR can be used");
    }

    float getDelay(void) const override final
    {
        static_assert(sizeof(I) == 0, "Only specializations of Liquid::FIR can be used");
        return 0;
    }

    const std::vector<C> &getTaps(void) const override final
    {
        static_assert(sizeof(I) == 0, "Only specializations of Liquid::FIR can be used");
    }

    void setTaps(const std::vector<C> &taps) override final
    {
        static_assert(sizeof(I) == 0, "Only specializations of Liquid::FIR can be used");
    }

    /** @brief Get output scaling for filter
     * @return Scaling factor applied to each output sample
     */
    C getScale() const
    {
        static_assert(sizeof(I) == 0, "Only specializations of Liquid::FIR can be used");
        return 0;
    }

    /** @brief Set output scaling for filter
     * @param scale Scaling factor applied to each output sample
     */
    void setScale(C scale)
    {
        static_assert(sizeof(I) == 0, "Only specializations of Liquid::FIR can be used");
    }

    /** @brief Print filter object information to stdout */
    void print(void) const
    {
        static_assert(sizeof(I) == 0, "Only specializations of Liquid::FIR can be used");
    }
};

template <>
class FIR<C, C, C> : public ::FIR<C,C,C>
{
public:
    FIR() = delete;

    FIR(FIR &&f)
    {
        firfilt_cccf_destroy(q_);
        q_ = f.q_;
        f.q_ = nullptr;
    }

    FIR(const std::vector<C> &taps)
    {
        setTaps(taps);
    }

    virtual ~FIR()
    {
        if (q_)
            firfilt_cccf_destroy(q_);
    }

    FIR& operator=(const std::vector<C> &taps)
    {
        setTaps(taps);
        return *this;
    }

    FIR& operator=(FIR &&f)
    {
        firfilt_cccf_destroy(q_);
        q_ = f.q_;
        f.q_ = nullptr;

        return *this;
    }

    FIR& operator=(const FIR &) = delete;

    float getGroupDelay(float fc) const override final
    {
        return firfilt_cccf_groupdelay(q_, fc);
    }

    void reset(void) override final
    {
        firfilt_cccf_reset(q_);
    }

    void execute(const C *in, C *out, size_t n) override final
    {
        firfilt_cccf_execute_block(q_,
                                   const_cast<C*>(in),
                                   n,
                                   out);
    }

    float getDelay(void) const override final
    {
        return delay_;
    }

    const std::vector<C> &getTaps(void) const override final
    {
        return taps_;
    }

    void setTaps(const std::vector<C> &taps) override final
    {
        taps_ = taps;
        firfilt_cccf_recreate(q_, const_cast<C*>(taps_.data()), taps_.size());
        delay_ = (taps_.size() - 1.0)/2.0;
    }

    C getScale() const
    {
        C scale;

        firfilt_cccf_get_scale(q_, &scale);
        return scale;
    }

    void setScale(C scale)
    {
        firfilt_cccf_set_scale(q_, scale);
    }

    void print(void) const
    {
        firfilt_cccf_print(q_);
    }

protected:
    /** @brief Filter taps */
    std::vector<C> taps_;

    /** @brief Liquid filter object */
    firfilt_cccf q_;

    /** @brief Filter delay */
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

/** @brief An IIR filter */
template <class I, class O, class C>
class IIR : public ::IIR<I,O,C>
{
public:
    IIR() = delete;

    IIR(IIR &&f)
    {
        static_assert(sizeof(I) == 0, "Only specializations of Liquid::IIR can be used");
    }

    /** @brief Initialize filter with feedforward and feedback constants */
    IIR(const C *b, unsigned Nb,
        const C *a, unsigned Na)
    {
        static_assert(sizeof(I) == 0, "Only specializations of Liquid::IIR can be used");
    }

    /** @brief Initialize filter with second-order-sections */
    IIR(const C *sos, unsigned N)
    {
        static_assert(sizeof(I) == 0, "Only specializations of Liquid::IIR can be used");
    }

    virtual ~IIR()
    {
        static_assert(sizeof(I) == 0, "Only specializations of Liquid::IIR can be used");
    }

    IIR& operator=(const IIR &) = delete;
    IIR& operator=(IIR &&) = delete;

    float getGroupDelay(float fc) const override final
    {
        static_assert(sizeof(I) == 0, "Only specializations of Liquid::IIR can be used");
        return 0;
    }

    void reset(void) override final
    {
        static_assert(sizeof(I) == 0, "Only specializations of Liquid::IIR can be used");
    }

    void execute(const C *in, C *out, size_t n) override final
    {
        static_assert(sizeof(I) == 0, "Only specializations of Liquid::IIR can be used");
    }

    /** @brief Print filter object information to stdout */
    void print(void) const
    {
        static_assert(sizeof(I) == 0, "Only specializations of Liquid::IIR can be used");
    }
};

template <>
class IIR<C,C,C> : public ::IIR<C,C,C>
{
public:
    IIR() = delete;

    IIR(IIR &&f)
    {
        q_ = f.q_;
        f.q_ = nullptr;
    }

    IIR(const C *b, unsigned Nb,
        const C *a, unsigned Na)
    {
        q_ = iirfilt_cccf_create(const_cast<C*>(b), Nb,
                                 const_cast<C*>(a), Na);
    }

    IIR(const C *sos, unsigned N)
    {
        std::vector<C> b(3*N);
        std::vector<C> a(3*N);

        for (unsigned i = 0; i < N; ++i) {
            for (unsigned j = 0; j < 3; ++j) {
                b[3*i + j] = sos[6*i + j];
                a[3*i + j] = sos[6*i + j + 3];
            }
        }

        q_ = iirfilt_cccf_create_sos(b.data(), a.data(), N);
    }

    virtual ~IIR()
    {
        if (q_)
            iirfilt_cccf_destroy(q_);
    }

    IIR& operator=(const IIR &) = delete;
    IIR& operator=(IIR &&) = delete;

    float getGroupDelay(float fc) const override final
    {
        return iirfilt_cccf_groupdelay(q_, fc);
    }

    void reset(void) override final
    {
        iirfilt_cccf_reset(q_);
    }

    void execute(const C *in, C *out, size_t n) override final
    {
        iirfilt_cccf_execute_block(q_,
                                   const_cast<C*>(in),
                                   n,
                                   out);
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
