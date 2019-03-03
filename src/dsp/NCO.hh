#ifndef NCO_HH_
#define NCO_HH_

#include <sys/types.h>

#include <complex>
#include <memory>

#include <liquid/liquid.h>

class NCO {
public:
    NCO() = default;
    virtual ~NCO() = default;

    /** @brief Get frequency in radians per sample */
    virtual double getFrequency(void) = 0;

    /** @brief Set frequency in radians per sample */
    virtual void setFrequency(double dtheta) = 0;

    /** @brief Get phase in radians */
    virtual double getPhase(void) = 0;

    /** @brief Set phase in radians */
    virtual void setPhase(double theta) = 0;

    /** @brief Reset NCO state with given frequency in radians per sample */
    virtual void reset(double dtheta) = 0;

    /** @brief Mix a single sample up */
    virtual std::complex<float> mix_up(const std::complex<float> in) = 0;

    /** @brief Mix a single sample down */
    virtual std::complex<float> mix_down(const std::complex<float> in) = 0;

    /** @brief Mix a signal up */
    virtual void mix_up(const std::complex<float> *in,
                        std::complex<float> *out,
                        size_t count) = 0;

    /** @brief Mix a signal down */
    virtual void mix_down(const std::complex<float> *in,
                          std::complex<float> *out,
                          size_t count) = 0;
};

#endif /* NCO_HH_ */
