// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef UTIL_CAPABILITIES_HH_
#define UTIL_CAPABILITIES_HH_

#include <sys/capability.h>


class Caps {
public:
    Caps() : caps_(cap_init())
    {
        if (caps_ == NULL)
            throw std::runtime_error(strerror(errno));
    }

    Caps(const Caps& other) : caps_(cap_dup(other.caps_))
    {
        if (caps_ == NULL)
            throw std::runtime_error(strerror(errno));
    }

    Caps(Caps&& other) noexcept : caps_(other.caps_)
    {
        other.caps_ = NULL;
    }

    Caps(cap_t caps) : caps_(caps)
    {
    }

    ~Caps()
    {
        if (caps_ != NULL)
            cap_free(caps_);
    }

    Caps& operator=(const Caps& other)
    {
        if ((caps_ = cap_dup(other.caps_)) == NULL)
            throw std::runtime_error(strerror(errno));

        return *this;
    }

    Caps& operator=(Caps&& other) noexcept
    {
        caps_ = other.caps_;
        other.caps_ = NULL;

        return *this;
    }

    Caps& operator=(cap_t caps) noexcept
    {
        if (caps_ != NULL)
            cap_free(caps_);

        caps_ = caps;

        return *this;
    }

    void set_proc()
    {
        if (cap_set_proc(caps_) != 0)
            throw std::runtime_error(strerror(errno));
    }

    void clear()
    {
        if (cap_clear(caps_) != 0)
            throw std::runtime_error(strerror(errno));
    }

    cap_flag_value_t get_flag(cap_value_t cap, cap_flag_t flag)
    {
        cap_flag_value_t value;

        if (cap_get_flag(caps_, cap, flag, &value) != 0)
            throw std::runtime_error(strerror(errno));

        return value;
    }

    void set_flag(cap_flag_t flag, const std::vector<cap_value_t>& caps)
    {
        if (cap_set_flag(caps_, flag, caps.size(), &caps[0], CAP_SET) != 0)
            throw std::runtime_error(strerror(errno));
    }

    void clear_flag(cap_flag_t flag)
    {
        if (cap_clear_flag(caps_, flag) != 0)
            throw std::runtime_error(strerror(errno));
    }

    void clear_flag(cap_flag_t flag, const std::vector<cap_value_t>& caps)
    {
        if (cap_set_flag(caps_, flag, caps.size(), &caps[0], CAP_CLEAR) != 0)
            throw std::runtime_error(strerror(errno));
    }

protected:
    cap_t caps_;
};

class RaiseCaps
{
public:
    RaiseCaps(const std::vector<cap_value_t>& caps)
      : orig_caps_(cap_get_proc())
    {
        Caps new_caps(cap_get_proc());

        new_caps.set_flag(CAP_EFFECTIVE, caps);
        new_caps.set_proc();
    }

    RaiseCaps() = delete;
    RaiseCaps(const RaiseCaps&) = delete;
    RaiseCaps(RaiseCaps&&) = delete;

    ~RaiseCaps()
    {
        orig_caps_.set_proc();
    }

    RaiseCaps& operator=(const RaiseCaps&) = delete;
    RaiseCaps& operator=(RaiseCaps&& ) = delete;

protected:
    Caps orig_caps_;
};

#endif /* UTIL_CAPABILITIES_HH_ */
