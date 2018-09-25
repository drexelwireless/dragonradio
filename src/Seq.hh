#ifndef SEQ_HH_
#define SEQ_HH_

#include <sys/types.h>

struct Seq {
    using uint_type = uint16_t;
    using int_type = int16_t;

    Seq() = default;
    Seq(uint_type seq) : seq_(seq) {};

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
    Seq operator ++(int) { seq_++; return seq_ - 1; }

    Seq operator --() { seq_--; return *this; }
    Seq operator --(int) { seq_--; return seq_ + 1; }

    Seq operator +(int i) { return seq_ + i; }
    Seq operator -(int i) { return seq_ - i; }

    operator uint_type() const { return seq_; }

    static uint_type max(void)
    {
        return std::numeric_limits<uint_type>::max();
    }

    uint_type seq_;
};

#endif /* SEQ_HH_ */
