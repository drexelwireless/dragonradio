// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef WINDOW_H_
#define WINDOW_H_

#include <vector>

#include <xsimd/xsimd.hpp>

// See:
//   http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
inline uint32_t nextPowerOfTwo(uint32_t x)
{
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x++;

    return x;
}

template <class T>
class Window
{
public:
    using size_type = typename std::vector<T>::size_type;

    explicit Window(size_type n)
    {
        resize(n);
    }

    Window() = delete;

    ~Window() = default;

    /** @brief Return window size */
    size_type size(void)
    {
        return n_;
    }

    /** @brief Resize window
     * @param n New window size
     */
    void resize(size_type n)
    {
        n_ = n;
        len_ = nextPowerOfTwo(n_);
        mask_ = len_ - 1;
        w_.resize(len_ + xsimd::simd_type<T>::size - 1);
        reset();
    }

    /** @brief Reset window elements to 0 */
    void reset(void)
    {
        read_idx_ = 0;
        std::fill(w_.begin(), w_.end(), 0);
    }

    /** @brief Add one value to the window
     * @param x The value to add to the window.
     */
    void add(T x)
    {
        w_[read_idx_++] = 0;
        w_[(read_idx_ + n_ - 1) & mask_] = x;
        read_idx_ &= mask_;
    }

    /** @brief Compute dot product of window
     * @param ys The second argument to the dot product
     * @param _tag An xsimd alignment tag indicating the alignment of ys.
     */
    /** The array `ys` must have an integral multiple of `xsimd::simd_type<C>`
     * elements. Any elements in `ys` beyond the first `n`, where `n` is the
     * window size, must be zero. These invariants allow us to implement the dot
     * product very efficiently with vector instructions.
     */
    template<class C, class Tag>
    T dotprod(const C *ys, Tag _tag)
    {
        using tvec_t = xsimd::simd_type<T>;
        using cvec_t = xsimd::simd_type<C>;
        using size_t = typename std::vector<T>::size_type;

        tvec_t acc{};

        // Calculate dot product with first portion of window
        const size_t n1 = std::min(n_, len_ - read_idx_);

        for (size_t i = 0; i < n1; i += tvec_t::size) {
            tvec_t xvec = xsimd::load_unaligned(&w_[read_idx_+i]);
            cvec_t cvec = xsimd::load(&ys[i], Tag());

            acc += xvec*cvec;
        }

        // Calculate dot product with wrapped portion of window
        const size_t n2 = n_ - n1;

        for (size_t i = 0; i < n2; i += tvec_t::size) {
            tvec_t xvec = xsimd::load_aligned(&w_[i]);
            cvec_t cvec = xsimd::load_unaligned(&ys[n1+i]);

            acc += xvec*cvec;
        }

        return hadd(acc);
    }

    std::vector<T> get(void) const
    {
        std::vector<T> xs(n_);

        // Copy first portion of window
        size_t n1 = std::min(n_, len_ - read_idx_);
        std::copy(w_.begin()+read_idx_, w_.begin()+read_idx_+n1, xs.begin());

        // Copy wrapped portion of window
        size_t n2 = n_ - n1;
        std::copy(w_.begin(), w_.begin()+n2, xs.begin()+n1);

        return xs;
    }

protected:
    /** @brief Window size */
    size_type n_;

    /** @brief Window size rounded up to next power of two */
    size_type len_;

    /** @brief Mask for window read index */
    size_type mask_;

    /** @brief Read index */
    size_type read_idx_;

    /** @brief Samples in our window */
    std::vector<T, xsimd::aligned_allocator<T>> w_;
};

#endif /* WINDOW_H_ */
