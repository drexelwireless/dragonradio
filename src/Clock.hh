#ifndef CLOCK_H_
#define CLOCK_H_

#include <chrono>
#include <memory>

#include <uhd/usrp/multi_usrp.hpp>

class Clock
{
public:
    using duration   = std::chrono::seconds;
    using rep        = duration::rep;
    using period     = duration::period;
    using time_point = uhd::time_spec_t;

    static const bool is_steady = true;

    static time_point now() noexcept
    {
        if (usrp_)
            return usrp_->get_time_now();
        else
            return uhd::time_spec_t(0.0);
    }

    static void setUSRP(uhd::usrp::multi_usrp::sptr usrp);
    static void releaseUSRP(void);

private:
    static uhd::usrp::multi_usrp::sptr usrp_;
};

#endif /* CLOCK_H_ */
