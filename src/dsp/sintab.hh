// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef SINTAB_HH_
#define SINTAB_HH_

#include <complex>

template<int INTBITS>
class sintab {
public:
    /** @brief A binary radian */
    /** A binary radian (brad) is a fractional value, where 1 brad is 2 pi
     * radians.
     */
    using brad_t = uint32_t;

    /** @brief Number of bits used to represent a brad */
    static constexpr int BRADBITS = 8*sizeof(brad_t);

    /** @brief Number of bits used to represent fractional part of table index */
    static constexpr int FRACBITS = BRADBITS - INTBITS;

    /** @brief Size of table */
    static constexpr int N = 1 << INTBITS;

    /** @brief Number of brad's per table entry */
    static constexpr brad_t ONE = static_cast<brad_t>(1) << sintab::FRACBITS;

    /** @brief Binary radian representation of pi/2 */
    static constexpr brad_t PIDIV2 = static_cast<brad_t>(1) << (BRADBITS-2);

    /** @brief Reciprocal of dx between two entries in the table */
    static constexpr float DTHETA = 1.0f/static_cast<float>(ONE);

    sintab()
    {
        for (unsigned i = 0; i < N; ++i)
            sintab_[i] = ::sin(2.0*M_PI*static_cast<double>(i)/static_cast<double>(N));
    }

    ~sintab()
    {
    }

    /** @brief Convert an angle in radians to binary radians */
    static brad_t to_brad(double x)
    {
        // Note we divide by pi instead of 2*pi and then shift left by
        // BRADBITS-1 instead of BRADBITS, which would overflow anyway, to
        // compensate.
        return (x/M_PI)*(static_cast<brad_t>(1) << (BRADBITS-1));
    }

    /** @brief Convert an angle in binary radians to radians */
    static double from_brad(brad_t x)
    {
        return x/(static_cast<brad_t>(1) << (BRADBITS-1)) * M_PI;
    }

    float operator [](brad_t pos)
    {
        float y1 = sintab_[pos >> FRACBITS];
        float y2 = sintab_[(pos + ONE) >> FRACBITS];

        return sintab_[pos >> FRACBITS] + (pos & (ONE - 1))*DTHETA*(y2-y1);
    }

    float sin(brad_t theta)
    {
        return (*this)[theta];
    }

    float cos(brad_t theta)
    {
        return sin(theta + PIDIV2);
    }

private:
    float sintab_[N];
};

#endif /* SINTAB_HH_ */
