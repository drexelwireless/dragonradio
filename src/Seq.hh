// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#ifndef SEQ_HH_
#define SEQ_HH_

#include <sys/types.h>

#include <limits>

struct Seq {
    using uint_type = uint16_t;
    using int_type = int16_t;

    Seq() = default;

    Seq(const Seq&) = default;
    Seq(Seq&&) = default;

    Seq& operator=(const Seq&) = default;
    Seq& operator=(Seq&&) = default;

    bool operator ==(const Seq& other) const { return seq_ == other.seq_; }
    bool operator !=(const Seq& other) const { return seq_ != other.seq_; }

    bool operator <(const Seq& other) const
      { return static_cast<int_type>(seq_ - other.seq_) < 0; }

    bool operator <=(const Seq& other) const
      { return static_cast<int_type>(seq_ - other.seq_) <= 0; }

    bool operator >(const Seq& other) const
      { return static_cast<int_type>(seq_ - other.seq_) > 0; }

    bool operator >=(const Seq& other) const
      { return static_cast<int_type>(seq_ - other.seq_) >= 0; }

    Seq operator ++() { seq_++; return *this; }
    Seq operator ++(int) { seq_++; return Seq { static_cast<uint_type>(seq_ - 1) }; }

    Seq operator --() { seq_--; return *this; }
    Seq operator --(int) { seq_--; return Seq { static_cast<uint_type>(seq_ + 1) }; }

    Seq operator +(int i) { return Seq { static_cast<uint_type>(seq_ + i) }; }
    Seq operator -(int i) { return Seq { static_cast<uint_type>(seq_ - i) }; }

    operator uint_type() const { return seq_; }

    static uint_type max(void)
    {
        return std::numeric_limits<uint_type>::max();
    }

    uint_type seq_;
};

#endif /* SEQ_HH_ */
