// Copyright 2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef FFTCOPY_HH_
#define FFTCOPY_HH_

/** @brief Copy FFT bins from one FFT to another while mixing up.
 * @tparam T The type of signal values
 * @param in Pointer to input FFT
 * @param Ni Size of input FFT
 * @param N Number of bins to copy from input FFT
 * @param out Pointer to output FFT
 * @param No Size of output FFT
 * @param Nrot Number of (output) FFT bins to rotate
 * @param k Multiplicative constant to apply when copying FFT
 */
template<class T>
void fftmixup(const T* in, unsigned Ni, unsigned N, T* out, unsigned No, unsigned Nrot, T k)
{
    assert(No >= Ni);
    assert(N <= Ni);

    assert(Nrot >= 0);
    assert(Nrot < No);

    if (Nrot == 0) {
        // If we aren't rotating bins, things are easy
        xsimd::transform(in,
                         in + N/2,
                         out,
                         [&](const auto& x) { return k*x; });
        xsimd::transform(in + Ni - N/2,
                         in + Ni,
                         out + No - N/2,
                         [&](const auto& x) { return k*x; });
    } else {
        // Handle bottom half of the FFT, which may be rotated far enough to
        // require splitting it across both the top of the output FFT and the
        // bottom of the output FFT.
        ssize_t out_lo = Nrot; // First destination bin for bottom half of input FFT

        if (out_lo + N/2 <= No) {
            xsimd::transform(in,
                             in + N/2,
                             out + out_lo,
                             [&](const auto& x) { return k*x; });
        } else {
            ssize_t split_at = No - out_lo;

            xsimd::transform(in,
                             in + split_at,
                             out + out_lo,
                             [&](const auto& x) { return k*x; });
            xsimd::transform(in + split_at,
                             in + N/2,
                             out,
                             [&](const auto& x) { return k*x; });
        }

        // Handle the top half of the input FFT. It may be rotated far enough to
        // move it entirely to the beginning of the output FFT, or we may have
        // to split it across both the top half of the output FFT and the bottom
        // half of the output FFT.
        ssize_t out_hi = No - N/2 + Nrot; // First destination bin for top half of input FFT

        if (out_hi >= No) {
            out_hi -= No;

            xsimd::transform(in + Ni - N/2,
                             in + Ni,
                             out + out_hi,
                             [&](const auto& x) { return k*x; });
        } else {
            ssize_t split_at = No - out_hi;

            xsimd::transform(in + Ni - N/2,
                             in + Ni - N/2 + split_at,
                             out + out_hi,
                             [&](const auto& x) { return k*x; });
            xsimd::transform(in + Ni - N/2 + split_at,
                             in + Ni,
                             out,
                             [&](const auto& x) { return k*x; });
        }
    }
}

/** @brief Copy FFT bins from one FFT to another while mixing down.
 * @tparam T The type of signal values
 * @param in Pointer to input FFT
 * @param Ni Size of input FFT
 * @param N Number of bins to copy from input FFT
 * @param Nrot Number of (input) FFT bins to rotate
 * @param out Pointer to output FFT
 * @param No Size of output FFT
 * @param k Multiplicative constant to apply when copying FFT
 */
template<class T>
void fftmixdown(const T* in, unsigned Ni, unsigned N, unsigned Nrot, T* out, unsigned No, T k)
{
    assert(Ni >= No);
    assert(N <= No);

    assert(Nrot >= 0);
    assert(Nrot < Ni);

    if (Nrot == 0) {
        // If we aren't rotating bins, things are easy
        xsimd::transform(in,
                         in + N/2,
                         out,
                         [&](const auto& x) { return k*x; });
        xsimd::transform(in + Ni - N/2,
                         in + Ni,
                         out + No - N/2,
                         [&](const auto& x) { return k*x; });
    } else {
        // Handle bottom half of the FFT, which may be rotated far enough to
        // require splitting it across both the top of the input FFT and the
        // bottom of the input FFT.
        ssize_t in_lo = Nrot; // First source bin for bottom half of input FFT

        if (in_lo + N/2 <= Ni) {
            xsimd::transform(in + in_lo,
                             in + in_lo + N/2,
                             out,
                             [&](const auto& x) { return k*x; });
        } else {
            ssize_t split_at = Ni - in_lo;

            xsimd::transform(in + in_lo,
                             in + Ni,
                             out,
                             [&](const auto& x) { return k*x; });
            xsimd::transform(in,
                             in + N/2 - split_at,
                             out + Ni - in_lo,
                             [&](const auto& x) { return k*x; });
        }

        // Handle the top half of the input FFT. It may be rotated far enough to
        // move it entirely to the beginning of the input FFT, or we may have to
        // split it across both the top half of the input FFT and the bottom
        // half of the input FFT.
        ssize_t in_hi = Ni - N/2 + Nrot; // First destination bin for top half of input FFT

        if (in_hi >= Ni) {
            in_hi -= Ni;

            xsimd::transform(in + in_hi,
                             in + in_hi + N/2,
                             out + No - N/2,
                             [&](const auto& x) { return k*x; });
        } else {
            ssize_t split_at = Ni - in_hi;

            xsimd::transform(in + in_hi,
                             in + Ni,
                             out + No - N/2,
                             [&](const auto& x) { return k*x; });
            xsimd::transform(in,
                             in + N/2 - split_at,
                             out + No - N/2 + split_at,
                             [&](const auto& x) { return k*x; });
        }
    }
}

#endif /* FFTCOPY_HH_ */
