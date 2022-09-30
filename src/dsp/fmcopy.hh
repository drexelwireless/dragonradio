// Copyright 2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef FMCOPY_HH_
#define FMCOPY_HH_

#include <algorithm>

#include <xsimd/xsimd.hpp>
#include <xsimd/stl/algorithms.hpp>

namespace dragonradio::signal {

/** @brief Floating multiply-copy
 * @tparam InputIt Input iterator
 * @tparam OutputIt Output iterator
 * @tparam T Type of multiplicative constant
 * @param first Beginning of source range
 * @param last End of source range
 * @param k Multiplicative constant
 */
template<class InputIt, class OutputIt, typename T>
void fmcopy(InputIt first, InputIt last, OutputIt d_first, T k)
{
    if (k == static_cast<T>(1))
        std::copy(first, last, d_first);
    else
        xsimd::transform(first, last, d_first,
                         [k](const auto& x) { return k*x; });
}

}

#endif /* FMCOPY_HH_ */
