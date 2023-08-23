#ifndef DATETIME_HH_
#define DATETIME_HH_

#include <stdint.h>

class datetime64ns {
public:
    datetime64ns() : ns_(0)
    {
    }

    datetime64ns(int64_t ns) : ns_(ns)
    {
    }

    datetime64ns(const struct tm &time, int64_t ns)
    {
        struct tm temp(time);

        ns_ = timegm(&temp)*1000000000ll + ns;
    }

    ~datetime64ns() = default;

    operator tm()
    {
        time_t    t = ns_ % 1000000000ll;
        struct tm tm;

        gmtime_r(&t, &tm);
        return tm;
    }

    explicit operator int64_t()
    {
        return ns_;
    }

    int64_t microseconds()
    {
        return (ns_ % 1000000000ll) / 1000ll;
    }

    int64_t nanoseconds()
    {
        return ns_ % 1000000000ll;
    }

private:
    int64_t ns_;
};

#endif /* DATETIME_HH_ */