// Per-frame receive record for full-stack simulation (phase P2).

#ifndef SIM_RXRECORDER_HH_
#define SIM_RXRECORDER_HH_

#include <cstring>
#include <map>
#include <mutex>
#include <vector>

#include "Packet.hh"
#include "net/Processor.hh"

namespace sim {

/** @brief Pass-through element between the channelizer and the controller.
 *
 * Records every frame the PHY hands up (retransmissions, duplicates, frames
 * with invalid payloads, and invalid headers when PHY.log_invalid_headers is
 * set) and forwards it unchanged. The production logger only records packets
 * delivered to the network, so this gives per-attempt reception accounting.
 */
class RxRecorder : public RadioProcessor
{
public:
    struct Record {
        double timestamp;       ///< Frame start (MonoClock seconds)
        uint8_t curhop;
        uint8_t nexthop;
        uint16_t seq;
        uint8_t flags;          ///< Header flag byte
        bool header_valid;
        bool payload_valid;
        uint16_t data_len;
        uint8_t mcsidx;
        float evm;
        float rssi;
        float cfo;
    };

    /** @brief Per-source totals, kept in both modes. */
    struct Counts {
        uint64_t frames = 0;          ///< Frames handed up (any validity)
        uint64_t header_valid = 0;
        uint64_t payload_valid = 0;
        uint64_t data_valid = 0;      ///< Valid payloads carrying data
    };

    RxRecorder() = default;
    virtual ~RxRecorder() = default;

    /** @brief Keep one record per frame (default) or only counts (long runs). */
    void setKeepRecords(bool keep)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        keep_ = keep;
    }

    bool getKeepRecords(void)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        return keep_;
    }

    /** @brief Totals keyed by curhop; invalid headers are under key -1. */
    std::map<int, Counts> getCounts(void)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        return counts_;
    }

    std::vector<Record> getRecords(void)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        return records_;
    }

protected:
    bool process(std::shared_ptr<RadioPacket>& pkt) override
    {
        Record r;

        r.timestamp = pkt->timestamp.get_real_secs();
        r.curhop = pkt->hdr.curhop;
        r.nexthop = pkt->hdr.nexthop;
        r.seq = static_cast<uint16_t>(pkt->hdr.seq);
        std::memcpy(&r.flags, &pkt->hdr.flags, 1);
        r.header_valid = !pkt->internal_flags.invalid_header;
        r.payload_valid = !pkt->internal_flags.invalid_header && !pkt->internal_flags.invalid_payload;
        r.data_len = r.payload_valid ? pkt->ehdr().data_len : 0;
        r.mcsidx = pkt->mcsidx;
        r.evm = pkt->evm;
        r.rssi = pkt->rssi;
        r.cfo = pkt->cfo;

        std::lock_guard<std::mutex> lock(mutex_);

        Counts &c = counts_[r.header_valid ? r.curhop : -1];
        c.frames++;
        c.header_valid += r.header_valid;
        c.payload_valid += r.payload_valid;
        c.data_valid += r.payload_valid && r.data_len > 0;
        if (keep_)
            records_.push_back(r);
        return true;
    }

private:
    std::mutex mutex_;
    std::vector<Record> records_;
    std::map<int, Counts> counts_;
    bool keep_ = true;
};

}

#endif /* SIM_RXRECORDER_HH_ */
