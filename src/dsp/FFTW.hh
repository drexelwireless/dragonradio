// Copyright 2018-2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef FFTW_H_
#define FFTW_H_

#include <complex>
#include <memory>
#include <mutex>
#include <vector>

#include <fftw3.h>

namespace fftw
{
    /** @brief Creation of FFTW plans is not re-entrant, so we need to protect
     * access with a mutex.
     */
    extern std::mutex mutex;

    /**
     * @class allocator
     * @brief Allocator for FFTW-aligned memory.
     *
     * @tparam T type of objects to allocate.
     */
    template <class T>
    class allocator
    {
    public:
        using pointer = T*;
        using const_pointer = const T*;
        using void_pointer = void*;
        using const_void_pointer = const void*;
        using value_type = T;
        using size_type = size_t;
        using difference_type = ptrdiff_t;

        template <class U>
        struct rebind
        {
            using other = allocator<U>;
        };

        allocator() noexcept
        {
        }

        allocator(const allocator& rhs) noexcept
        {
        }

        template <class U>
        allocator(const allocator<U>& rhs) noexcept
        {
        }

        ~allocator()
        {
        }

        pointer allocate(size_type n, const_void_pointer hint = 0)
        {
            pointer res = reinterpret_cast<pointer>(fftw_malloc(sizeof(T) * n));
            if (res == nullptr)
                throw std::bad_alloc();
            return res;
        }

        void deallocate(pointer p, size_type n)
        {
            fftw_free(p);
        }

        size_type max_size() const noexcept
        {
            return size_type(-1) / sizeof(T);
        }

        template <class U, class... Args>
        void construct(U* p, Args&&... args)
        {
            new ((void*)p) U(std::forward<Args>(args)...);
        }

        template <class U>
        void destroy(U* p)
        {
            p->~U();
        }
    };

    template<typename T>
    using vector = std::vector<T, allocator<T>>;

    template <class T>
    class FFT {
    public:
        FFT(unsigned N, int sign, unsigned flags)
        {
            static_assert(sizeof(T) == 0, "Only specializations of fftw::FFTBase can be used");
        }

        virtual ~FFT()
        {
            static_assert(sizeof(T) == 0, "Only specializations of fftw::FFTBase can be used");
        }

        unsigned getSize(void) const
        {
            static_assert(sizeof(T) == 0, "Only specializations of fftw::FFTBase can be used");
            return 0;
        }

        void execute(void)
        {
            static_assert(sizeof(T) == 0, "Only specializations of fftw::FFTBase can be used");
        }

        void execute(const T *in, T *out)
        {
            static_assert(sizeof(T) == 0, "Only specializations of fftw::FFTBase can be used");
        }
    };

    template <>
    class FFT<std::complex<float>> {
    public:
        using C = std::complex<float>;

        FFT(unsigned N_, int sign, unsigned flags=FFTW_MEASURE)
          : N(N_)
          , in(N_)
          , out(N_)
        {
            std::lock_guard<std::mutex> lock(fftw::mutex);

            plan_ = fftwf_plan_dft_1d(N,
                                      reinterpret_cast<fftwf_complex*>(in.data()),
                                      reinterpret_cast<fftwf_complex*>(out.data()),
                                      sign,
                                      flags);
            if (!plan_)
                throw std::runtime_error("Could not create FFTW plan");
        }

        virtual ~FFT()
        {
            std::lock_guard<std::mutex> lock(fftw::mutex);

            fftwf_destroy_plan(plan_);
        }

        unsigned getSize(void) const
        {
            return N;
        }

        void execute(void)
        {
            fftwf_execute(plan_);
        }

        void execute(const C *in, C *out)
        {
            fftwf_execute_dft(plan_,
                              reinterpret_cast<fftwf_complex*>(const_cast<C*>(in)),
                              reinterpret_cast<fftwf_complex*>(out));
        }

        /** @brief Size of FFT */
        const unsigned N;

        /** @brief Input buffer */
        vector<C> in;

        /** @brief Output buffer */
        vector<C> out;

    protected:
        /** @brief FFTW plan */
        fftwf_plan plan_;
    };

    /** @brief Create FFT plans (forward/backwards) for complex float FFT
     * @param N FFT size
     */
    void planFFTs(unsigned N);
}

#endif /* FFTW_H_ */
