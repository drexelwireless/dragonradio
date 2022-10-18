// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include <stdint.h>
#include <string.h>

#include <complex>

#include <H5Cpp.h>

#include "Clock.hh"
#include "IQCompression.hh"
#include "Logger.hh"

std::shared_ptr<Logger> logger;

/** @brief File block size(-ish) */
constexpr size_t kBlockSize = 4*1024;

/** @brief Number of elements in the meta data cache */
constexpr int kMDCNumElements = 512;

/** @brief Number of elements in the raw data chunk cache */
constexpr size_t kRDCCNumElements = 512;

/** @brief Total size of the raw data chunk cache, in bytes */
constexpr size_t kRDCCNumBytes = 16*1024*kBlockSize;

/** @brief Preemption policy */
constexpr double kRDCCW0 = 0.0;

/** @brief Log entry for slots */
struct SlotEntry {
    /** @brief Receive timestamp. */
    double timestamp;
    /** @brief Monotonic clock timestamp. */
    double mono_timestamp;
    /** @brief Sample center frequency [Hz] */
    float fc;
    /** @brief Sample rate [Hz] */
    float fs;
    /** @brief Size of uncompressed IQ data (bytes). */
    uint32_t iq_data_len;
    /** @brief Compressed IQ data. */
    hvl_t iq_data;
};

/** @brief Log entry for TX records */
struct TXRecordEntry {
    /** @brief TX timestamp. */
    double timestamp;
    /** @brief Monotonic TX timestamp. */
    double mono_timestamp;
    /** @brief Number of samples. */
    int64_t nsamples;
    /** @brief Sampling frequency [Hz] */
    float fs;
};

/** @brief Log entry for snapshots */
struct SnapshotEntry {
    /** @brief Receive timestamp. */
    double timestamp;
    /** @brief Monotonic clock timestamp. */
    double mono_timestamp;
    /** @brief Sampling frequency [Hz] */
    float fs;
    /** @brief Size of uncompressed IQ data (bytes). */
    uint32_t iq_data_len;
    /** @brief Compressed IQ data. */
    hvl_t iq_data;
};

/** @brief Log entry for self-transmission events */
struct SelfTXEntry {
    /** @brief Timestamp of snapshot this self-transmission belongs to. */
    double timestamp;
    /** @brief Monotonic clock timestamp. */
    double mono_timestamp;
    /** @brief Is this TX local, i.e., produced by this node? */
    uint8_t is_local;
    /** @brief Offset of start of packet. */
    int32_t start;
    /** @brief Offset of end of packet. */
    int32_t end;
    /** @brief Center frequency [Hz] */
    float fc;
    /** @brief Sampling frequency [Hz] */
    float fs;
};

/** @brief Log entry for received packets */
struct PacketRecvEntry {
    /** @brief Timestamp of the slot in which the packet occurred. */
    /** If the packet spans two slots, this is the timestamp of the first slot. */
    double slot_timestamp;
    /** @brief Timestamp of packet reception. */
    double timestamp;
    /** @brief Monotonic clock timestamp of packet reception. */
    double mono_timestamp;
    /** @brief Offset (in samples) from timestamp slot to start of frame. */
    int32_t start_samples;
    /** @brief Offset (in samples) from timestamp slot to end of frame. */
    int32_t end_samples;
    /** @brief Was header valid? */
    uint8_t header_valid;
    /** @brief Was payload valid? */
    uint8_t payload_valid;
    /** @brief Packet current hop. */
    uint8_t curhop;
    /** @brief Packet next hop. */
    uint8_t nexthop;
    /** @brief Packet sequence number. */
    uint16_t seq;
    /** @brief Packet header flags. */
    uint8_t flags;
    /** @brief Packet source. */
    uint8_t src;
    /** @brief Packet destination. */
    uint8_t dest;
    /** @brief ACK'ed sequence number. */
    uint16_t ack;
    /** @brief Size of data portion of packet (bytes). */
    uint16_t data_len;
    /** @brief MGEN flow UID. */
    uint32_t mgen_flow_uid;
    /** @brief MGEN sequence number. */
    uint32_t mgen_seqno;
    /** @brief MCS Index. */
    uint8_t mcsidx;
    /** @brief EVM [dB]. */
    float evm;
    /** @brief RSSI [dB]. */
    float rssi;
    /** @brief CFO [f/Fs]. */
    float cfo;
    /** @brief Center frequency [Hz] */
    float fc;
    /** @brief Bandwidth [Hz] */
    float bw;
    /** @brief Channel center frequency [Hz] */
    float chan_fc;
    /** @brief Bandwidth [Hz] */
    float chan_bw;
    /** @brief Demodulation latency (sec) */
    double demod_latency;
    /** @brief Latency between packet reception and write to tun/tap [sec] */
    double tuntap_latency;
    /** @brief Size of packet (bytes). */
    uint32_t size;
    /** @brief Raw IQ data. */
    hvl_t symbols;
};

/** @brief Log entry for sent packets */
struct PacketSendEntry {
    /** @brief Timestamp of packet transmission. */
    double timestamp;
    /** @brief Monotonic clock timestamp of packet transmission. */
    double mono_timestamp;
    /** @brief Timestamp of packet reception from network. */
    double net_timestamp;
    /** @brief Timestamp of packet reception from MGEN. */
    double wall_timestamp;
    /** @brief Packet deadline. */
    double deadline;
    /** @brief Was this packet dropped, and if so, why was it dropped? */
    uint8_t dropped;
    /** @brief Number of packet retransmissions. */
    uint16_t nretrans;
    /** @brief Packet current hop. */
    uint8_t curhop;
    /** @brief Packet next hop. */
    uint8_t nexthop;
    /** @brief Packet sequence number. */
    uint16_t seq;
    /** @brief Packet header flags. */
    uint8_t flags;
    /** @brief Packet source. */
    uint8_t src;
    /** @brief Packet destination. */
    uint8_t dest;
    /** @brief ACK'ed sequence number. */
    uint16_t ack;
    /** @brief Size of data portion of packet (bytes). */
    uint16_t data_len;
    /** @brief MGEN flow UID. */
    uint32_t mgen_flow_uid;
    /** @brief MGEN sequence number. */
    uint32_t mgen_seqno;
    /** @brief MCS Index. */
    uint8_t mcsidx;
    /** @brief Center frequency [Hz] */
    float fc;
    /** @brief Bandwidth [Hz] */
    float bw;
    /** @brief Latency between packet creation and tun/tap read [sec] */
    double tuntap_latency;
    /** @brief Enqueue latency [sec] */
    double enqueue_latency;
    /** @brief Latency of *just* dequeue [sec] */
    double dequeue_latency;
    /** @brief Queue latency [sec] */
    double queue_latency;
    /** @brief LLC latency [sec] */
    double llc_latency;
    /** @brief Latency of *just* modulation [sec] */
    double mod_latency;
    /** @brief Latency from network read to modulation [sec] */
    double synth_latency;
    /** @brief Size of packet (bytes). */
    uint32_t size;
    /** @brief Number of IQ samples. */
    int32_t nsamples;
    /** @brief Raw IQ data. */
    hvl_t iq_data;
};

/** @brief Generic event */
struct EventEntry {
    /** @brief Event timestamp. */
    double timestamp;
    /** @brief Monotonic clock timestamp. */
    double mono_timestamp;
    /** @brief Event description. */
    const char *event;
};

/** @brief Log entry for LLC events */
struct ARQEventEntry {
    /** @brief Event timestamp */
    double timestamp;
    /** @brief Monotonic clock timestamp. */
    double mono_timestamp;
    /** @brief Type of LLC entry */
    uint8_t type;
    /** @brief Node ID of other node. */
    uint8_t node;
    /** @brief Sequence number */
    uint16_t seq;
    /** @brief Selective ACKs.*/
    /** A selective ACK sequence is a list of tuples [start,end) representing
     * selective ACKs from start (inclusive) to end (non-inclusive).
     */
    hvl_t sacks;
};

Logger::Logger(const WallClock::time_point& t_start,
               const MonoClock::time_point& mono_t_start)
  : is_open_(false)
  , t_start_(t_start)
  , mono_t_start_(mono_t_start)
  , sources_(0)
{
    done_.store(false, std::memory_order_release);
}

Logger::~Logger()
{
    close();
}

void Logger::open(const std::string& filename)
{
    // H5 type for strings
    H5::StrType h5_string(H5::PredType::C_S1, H5T_VARIABLE);

    // H5 compound type for complex floats
    // This matches numpy's format
    // See:
    //   https://stackoverflow.com/questions/33621687/c-reading-a-numpy-array-of-complex-numbers-from-an-hdf5-file
    H5::CompType h5_complex32(sizeof(std::complex<float>));

    h5_complex32.insertMember("r", 0, H5::PredType::NATIVE_FLOAT);
    h5_complex32.insertMember("i", sizeof(float), H5::PredType::NATIVE_FLOAT);

    // H5 type for variable-length IQ data
    H5::VarLenType h5_iqdata(&h5_complex32);

    // H5 type for variable-length compressed IQ data
    H5::VarLenType h5_compressed_iqdata(&H5::PredType::NATIVE_CHAR);

    // H5 type for slots
    H5::CompType h5_slot(sizeof(SlotEntry));

    h5_slot.insertMember("timestamp", HOFFSET(SlotEntry, timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_slot.insertMember("mono_timestamp", HOFFSET(SlotEntry, mono_timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_slot.insertMember("fc", HOFFSET(SlotEntry, fc), H5::PredType::NATIVE_FLOAT);
    h5_slot.insertMember("fs", HOFFSET(SlotEntry, fs), H5::PredType::NATIVE_FLOAT);
    h5_slot.insertMember("iq_data_len", HOFFSET(SlotEntry, iq_data_len), H5::PredType::NATIVE_UINT32);
    h5_slot.insertMember("iq_data", HOFFSET(SlotEntry, iq_data), h5_compressed_iqdata);

    // H5 type for tx records
    H5::CompType h5_tx_record(sizeof(TXRecordEntry));

    h5_tx_record.insertMember("timestamp", HOFFSET(TXRecordEntry, timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_tx_record.insertMember("mono_timestamp", HOFFSET(TXRecordEntry, mono_timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_tx_record.insertMember("nsamples", HOFFSET(TXRecordEntry, nsamples), H5::PredType::NATIVE_INT64);
    h5_tx_record.insertMember("fs", HOFFSET(TXRecordEntry, fs), H5::PredType::NATIVE_DOUBLE);

    // H5 type for snapshots
    H5::CompType h5_snapshot(sizeof(SnapshotEntry));

    h5_snapshot.insertMember("timestamp", HOFFSET(SnapshotEntry, timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_snapshot.insertMember("mono_timestamp", HOFFSET(SnapshotEntry, mono_timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_snapshot.insertMember("fs", HOFFSET(SnapshotEntry, fs), H5::PredType::NATIVE_FLOAT);
    h5_snapshot.insertMember("iq_data_len", HOFFSET(SnapshotEntry, iq_data_len), H5::PredType::NATIVE_UINT32);
    h5_snapshot.insertMember("iq_data", HOFFSET(SnapshotEntry, iq_data), h5_compressed_iqdata);

    // H5 type for snapshot self-transmission events
    H5::CompType h5_selftx(sizeof(SelfTXEntry));

    h5_selftx.insertMember("timestamp", HOFFSET(SelfTXEntry, timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_selftx.insertMember("mono_timestamp", HOFFSET(SelfTXEntry, mono_timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_selftx.insertMember("is_local", HOFFSET(SelfTXEntry, is_local), H5::PredType::NATIVE_UINT8);
    h5_selftx.insertMember("start", HOFFSET(SelfTXEntry, start), H5::PredType::NATIVE_INT32);
    h5_selftx.insertMember("end", HOFFSET(SelfTXEntry, end), H5::PredType::NATIVE_INT32);
    h5_selftx.insertMember("fc", HOFFSET(SelfTXEntry, fc), H5::PredType::NATIVE_FLOAT);
    h5_selftx.insertMember("fs", HOFFSET(SelfTXEntry, fs), H5::PredType::NATIVE_FLOAT);

    // H5 type for received packets
    H5::CompType h5_packet_recv(sizeof(PacketRecvEntry));

    h5_packet_recv.insertMember("slot_timestamp", HOFFSET(PacketRecvEntry, slot_timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_packet_recv.insertMember("timestamp", HOFFSET(PacketRecvEntry, timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_packet_recv.insertMember("mono_timestamp", HOFFSET(PacketRecvEntry, mono_timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_packet_recv.insertMember("start_samples", HOFFSET(PacketRecvEntry, start_samples), H5::PredType::NATIVE_INT32);
    h5_packet_recv.insertMember("end_samples", HOFFSET(PacketRecvEntry, end_samples), H5::PredType::NATIVE_INT32);
    h5_packet_recv.insertMember("header_valid", HOFFSET(PacketRecvEntry, header_valid), H5::PredType::NATIVE_UINT8);
    h5_packet_recv.insertMember("payload_valid", HOFFSET(PacketRecvEntry, payload_valid), H5::PredType::NATIVE_UINT8);
    h5_packet_recv.insertMember("curhop", HOFFSET(PacketRecvEntry, curhop), H5::PredType::NATIVE_UINT8);
    h5_packet_recv.insertMember("nexthop", HOFFSET(PacketRecvEntry, nexthop), H5::PredType::NATIVE_UINT8);
    h5_packet_recv.insertMember("seq", HOFFSET(PacketRecvEntry, seq), H5::PredType::NATIVE_UINT16);
    h5_packet_recv.insertMember("flags", HOFFSET(PacketRecvEntry, flags), H5::PredType::NATIVE_UINT8);
    h5_packet_recv.insertMember("src", HOFFSET(PacketRecvEntry, src), H5::PredType::NATIVE_UINT8);
    h5_packet_recv.insertMember("dest", HOFFSET(PacketRecvEntry, dest), H5::PredType::NATIVE_UINT8);
    h5_packet_recv.insertMember("ack", HOFFSET(PacketRecvEntry, ack), H5::PredType::NATIVE_UINT16);
    h5_packet_recv.insertMember("data_len", HOFFSET(PacketRecvEntry, data_len), H5::PredType::NATIVE_UINT16);
    h5_packet_recv.insertMember("mgen_flow_uid", HOFFSET(PacketRecvEntry, mgen_flow_uid), H5::PredType::NATIVE_UINT32);
    h5_packet_recv.insertMember("mgen_seqno", HOFFSET(PacketRecvEntry, mgen_seqno), H5::PredType::NATIVE_UINT32);
    h5_packet_recv.insertMember("mcsidx", HOFFSET(PacketRecvEntry, mcsidx), H5::PredType::NATIVE_UINT8);
    h5_packet_recv.insertMember("evm", HOFFSET(PacketRecvEntry, evm), H5::PredType::NATIVE_FLOAT);
    h5_packet_recv.insertMember("rssi", HOFFSET(PacketRecvEntry, rssi), H5::PredType::NATIVE_FLOAT);
    h5_packet_recv.insertMember("cfo", HOFFSET(PacketRecvEntry, cfo), H5::PredType::NATIVE_FLOAT);
    h5_packet_recv.insertMember("fc", HOFFSET(PacketRecvEntry, fc), H5::PredType::NATIVE_FLOAT);
    h5_packet_recv.insertMember("bw", HOFFSET(PacketRecvEntry, bw), H5::PredType::NATIVE_FLOAT);
    h5_packet_recv.insertMember("chan_fc", HOFFSET(PacketRecvEntry, chan_fc), H5::PredType::NATIVE_FLOAT);
    h5_packet_recv.insertMember("chan_bw", HOFFSET(PacketRecvEntry, chan_bw), H5::PredType::NATIVE_FLOAT);
    h5_packet_recv.insertMember("demod_latency", HOFFSET(PacketRecvEntry, demod_latency), H5::PredType::NATIVE_DOUBLE);
    h5_packet_recv.insertMember("tuntap_latency", HOFFSET(PacketRecvEntry, tuntap_latency), H5::PredType::NATIVE_DOUBLE);
    h5_packet_recv.insertMember("size", HOFFSET(PacketRecvEntry, size), H5::PredType::NATIVE_UINT32);
    h5_packet_recv.insertMember("symbols", HOFFSET(PacketRecvEntry, symbols), h5_iqdata);

    // H5 type for sent packets
    H5::CompType h5_packet_send(sizeof(PacketSendEntry));

    h5_packet_send.insertMember("timestamp", HOFFSET(PacketSendEntry, timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_packet_send.insertMember("mono_timestamp", HOFFSET(PacketSendEntry, mono_timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_packet_send.insertMember("net_timestamp", HOFFSET(PacketSendEntry, net_timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_packet_send.insertMember("wall_timestamp", HOFFSET(PacketSendEntry, wall_timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_packet_send.insertMember("deadline", HOFFSET(PacketSendEntry, deadline), H5::PredType::NATIVE_DOUBLE);
    h5_packet_send.insertMember("dropped", HOFFSET(PacketSendEntry, dropped), H5::PredType::NATIVE_UINT8);
    h5_packet_send.insertMember("nretrans", HOFFSET(PacketSendEntry, nretrans), H5::PredType::NATIVE_UINT16);
    h5_packet_send.insertMember("curhop", HOFFSET(PacketSendEntry, curhop), H5::PredType::NATIVE_UINT8);
    h5_packet_send.insertMember("nexthop", HOFFSET(PacketSendEntry, nexthop), H5::PredType::NATIVE_UINT8);
    h5_packet_send.insertMember("seq", HOFFSET(PacketSendEntry, seq), H5::PredType::NATIVE_UINT16);
    h5_packet_send.insertMember("flags", HOFFSET(PacketSendEntry, flags), H5::PredType::NATIVE_UINT8);
    h5_packet_send.insertMember("src", HOFFSET(PacketSendEntry, src), H5::PredType::NATIVE_UINT8);
    h5_packet_send.insertMember("dest", HOFFSET(PacketSendEntry, dest), H5::PredType::NATIVE_UINT8);
    h5_packet_send.insertMember("ack", HOFFSET(PacketSendEntry, ack), H5::PredType::NATIVE_UINT16);
    h5_packet_send.insertMember("data_len", HOFFSET(PacketSendEntry, data_len), H5::PredType::NATIVE_UINT16);
    h5_packet_send.insertMember("mgen_flow_uid", HOFFSET(PacketSendEntry, mgen_flow_uid), H5::PredType::NATIVE_UINT32);
    h5_packet_send.insertMember("mgen_seqno", HOFFSET(PacketSendEntry, mgen_seqno), H5::PredType::NATIVE_UINT32);
    h5_packet_send.insertMember("mcsidx", HOFFSET(PacketSendEntry, mcsidx), H5::PredType::NATIVE_UINT8);
    h5_packet_send.insertMember("fc", HOFFSET(PacketSendEntry, fc), H5::PredType::NATIVE_FLOAT);
    h5_packet_send.insertMember("bw", HOFFSET(PacketSendEntry, bw), H5::PredType::NATIVE_FLOAT);
    h5_packet_send.insertMember("tuntap_latency", HOFFSET(PacketSendEntry, tuntap_latency), H5::PredType::NATIVE_DOUBLE);
    h5_packet_send.insertMember("enqueue_latency", HOFFSET(PacketSendEntry, enqueue_latency), H5::PredType::NATIVE_DOUBLE);
    h5_packet_send.insertMember("dequeue_latency", HOFFSET(PacketSendEntry, dequeue_latency), H5::PredType::NATIVE_DOUBLE);
    h5_packet_send.insertMember("queue_latency", HOFFSET(PacketSendEntry, queue_latency), H5::PredType::NATIVE_DOUBLE);
    h5_packet_send.insertMember("llc_latency", HOFFSET(PacketSendEntry, llc_latency), H5::PredType::NATIVE_DOUBLE);
    h5_packet_send.insertMember("mod_latency", HOFFSET(PacketSendEntry, mod_latency), H5::PredType::NATIVE_DOUBLE);
    h5_packet_send.insertMember("synth_latency", HOFFSET(PacketSendEntry, synth_latency), H5::PredType::NATIVE_DOUBLE);
    h5_packet_send.insertMember("size", HOFFSET(PacketSendEntry, size), H5::PredType::NATIVE_UINT32);
    h5_packet_send.insertMember("nsamples", HOFFSET(PacketSendEntry, nsamples), H5::PredType::NATIVE_INT32);
    h5_packet_send.insertMember("iq_data", HOFFSET(PacketSendEntry, iq_data), h5_iqdata);

    // H5 type for events
    H5::CompType h5_event(sizeof(EventEntry));

    h5_event.insertMember("timestamp", HOFFSET(EventEntry, timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_event.insertMember("mono_timestamp", HOFFSET(EventEntry, mono_timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_event.insertMember("event", HOFFSET(EventEntry, event), h5_string);

    // H5 type for variable-length SACK data
    H5::VarLenType h5_sack_data(&H5::PredType::NATIVE_UINT16);

    // H5 type for ARQ events
    H5::CompType h5_arq_event(sizeof(ARQEventEntry));

    h5_arq_event.insertMember("timestamp", HOFFSET(ARQEventEntry, timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_arq_event.insertMember("mono_timestamp", HOFFSET(ARQEventEntry, mono_timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_arq_event.insertMember("type", HOFFSET(ARQEventEntry, type), H5::PredType::NATIVE_UINT8);
    h5_arq_event.insertMember("node", HOFFSET(ARQEventEntry, node), H5::PredType::NATIVE_UINT8);
    h5_arq_event.insertMember("seq", HOFFSET(ARQEventEntry, seq), H5::PredType::NATIVE_UINT16);
    h5_arq_event.insertMember("sacks", HOFFSET(ARQEventEntry, sacks), h5_sack_data);

    // Open log file and set cache parameters
    H5::FileAccPropList acc_plist = H5::FileAccPropList::DEFAULT;

    acc_plist.setCache(kMDCNumElements,
                       kRDCCNumElements,
                       kRDCCNumBytes,
                       kRDCCW0);

    file_ = H5::H5File(filename, H5F_ACC_TRUNC, H5::FileCreatPropList::DEFAULT, acc_plist);

    // Create H5 groups
    slots_ = std::make_unique<ExtensibleDataSet>(file_, "slots", h5_slot);
    tx_records_ = std::make_unique<ExtensibleDataSet>(file_, "tx_records", h5_tx_record);
    snapshots_ = std::make_unique<ExtensibleDataSet>(file_, "snapshots", h5_snapshot);
    selftx_ = std::make_unique<ExtensibleDataSet>(file_, "selftx", h5_selftx);
    recv_ = std::make_unique<ExtensibleDataSet>(file_, "recv", h5_packet_recv);
    send_ = std::make_unique<ExtensibleDataSet>(file_, "send", h5_packet_send);
    event_ = std::make_unique<ExtensibleDataSet>(file_, "event", h5_event);
    arq_event_ = std::make_unique<ExtensibleDataSet>(file_, "arq_event", h5_arq_event);

    // Start worker thread
    worker_thread_ = std::thread(&Logger::worker, this);

    is_open_ = true;
}

void Logger::stop(void)
{
    done_.store(true, std::memory_order_release);

    log_q_.disable();

    if (worker_thread_.joinable())
        worker_thread_.join();
}

void Logger::close(void)
{
    if (is_open_) {
        stop();
        slots_.reset();
        tx_records_.reset();
        snapshots_.reset();
        selftx_.reset();
        recv_.reset();
        send_.reset();
        event_.reset();
        arq_event_.reset();
        file_.close();
        is_open_ = false;
    }
}

void Logger::setAttribute(const std::string& name, const std::string& val)
{
    H5::StrType   h5_type(H5::PredType::C_S1, H5T_VARIABLE);
    H5::DataSpace attr_space(H5S_SCALAR);
    H5::Attribute att = createOrOpenAttribute(name, h5_type, attr_space);

    att.write(h5_type, val);
}

void Logger::setAttribute(const std::string& name, uint8_t val)
{
    H5::IntType   h5_type(H5::PredType::NATIVE_UINT8);
    H5::DataSpace attr_space(H5S_SCALAR);
    H5::Attribute att = createOrOpenAttribute(name, h5_type, attr_space);

    att.write(h5_type, &val);
}

void Logger::setAttribute(const std::string& name, uint32_t val)
{
    H5::IntType   h5_type(H5::PredType::NATIVE_UINT32);
    H5::DataSpace attr_space(H5S_SCALAR);
    H5::Attribute att = createOrOpenAttribute(name, h5_type, attr_space);

    att.write(h5_type, &val);
}

void Logger::setAttribute(const std::string& name, int64_t val)
{
    H5::IntType   h5_type(H5::PredType::NATIVE_INT64);
    H5::DataSpace attr_space(H5S_SCALAR);
    H5::Attribute att = createOrOpenAttribute(name, h5_type, attr_space);

    att.write(h5_type, &val);
}

void Logger::setAttribute(const std::string& name, uint64_t val)
{
    H5::IntType   h5_type(H5::PredType::NATIVE_UINT64);
    H5::DataSpace attr_space(H5S_SCALAR);
    H5::Attribute att = createOrOpenAttribute(name, h5_type, attr_space);

    att.write(h5_type, &val);
}

void Logger::setAttribute(const std::string& name, double val)
{
    H5::FloatType h5_type(H5::PredType::NATIVE_DOUBLE);
    H5::DataSpace attr_space(H5S_SCALAR);
    H5::Attribute att = createOrOpenAttribute(name, h5_type, attr_space);

    att.write(h5_type, &val);
}

void Logger::worker(void)
{
    std::function<void()> entry;

    while (true) {
        log_q_.pop(entry);
        if (done_.load(std::memory_order_acquire))
            break;

        entry();
    }
}

H5::Attribute Logger::createOrOpenAttribute(const std::string &name,
                                            const H5::DataType &data_type,
                                            const H5::DataSpace &data_space)
{
    if (file_.attrExists(name))
        return file_.openAttribute(name);
    else
        return file_.createAttribute(name, data_type, data_space);
}

void Logger::logSlot_(const IQBuf &buf)
{
    SlotEntry    entry;
    buffer<char> compressed = compressIQData(buf.data(), buf.size());

    entry.timestamp = (WallClock::to_wall_time(*buf.timestamp) - t_start_).count();
    entry.mono_timestamp = std::chrono::duration<double>(*buf.timestamp - mono_t_start_).count();
    entry.fc = buf.fc;
    entry.fs = buf.fs;
    entry.iq_data_len = buf.size();
    entry.iq_data.p = compressed.data();
    entry.iq_data.len = compressed.size();

    slots_->write(&entry, 1);
}

void Logger::logTXRecord_(const std::optional<MonoClock::time_point> &t, size_t nsamples, double fs)
{
    TXRecordEntry entry;

    entry.timestamp = t ? std::chrono::duration<double>(WallClock::to_wall_time(*t) - t_start_).count() : 0;
    entry.mono_timestamp = t ? std::chrono::duration<double>(*t - mono_t_start_).count() : 0;
    entry.nsamples = nsamples;
    entry.fs = fs;

    tx_records_->write(&entry, 1);
}

void Logger::logSnapshot_(std::shared_ptr<Snapshot> snapshot)
{
    if (snapshot->slots.empty())
        return;

    SnapshotEntry          entry;
    double                 timestamp = std::chrono::duration<double>(WallClock::to_wall_time(snapshot->timestamp) - t_start_).count();
    double                 mono_timestamp = std::chrono::duration<double>(snapshot->timestamp - mono_t_start_).count();
    std::shared_ptr<IQBuf> buf = *(snapshot->getCombinedSlots());
    buffer<char>           compressed = compressIQData(buf->data(), buf->size());

    entry.timestamp = timestamp;
    entry.mono_timestamp = mono_timestamp;
    entry.fs = buf->fs;
    entry.iq_data_len = buf->size();
    entry.iq_data.p = compressed.data();
    entry.iq_data.len = compressed.size();

    snapshots_->write(&entry, 1);

    SelfTXEntry selftx_entry;

    for (auto&& selftx : snapshot->selftx) {
        selftx_entry.timestamp = timestamp;
        selftx_entry.mono_timestamp = mono_timestamp;
        selftx_entry.is_local = selftx.is_local;
        selftx_entry.start = selftx.start;
        selftx_entry.end = selftx.end;
        selftx_entry.fc = selftx.fc;
        selftx_entry.fs = selftx.fs;

        selftx_->write(&selftx_entry, 1);
    }
}

typedef union {
    Header::Flags flags;
    uint8_t bits;
} u_flags;

void Logger::logRecv_(RadioPacket &pkt)
{
    PacketRecvEntry entry;
    Header          &hdr = pkt.hdr;
    u_flags         u;

    u.flags = hdr.flags;

    entry.slot_timestamp = std::chrono::duration<double>(WallClock::to_wall_time(pkt.slot_timestamp) - t_start_).count();
    entry.timestamp = std::chrono::duration<double>(WallClock::to_wall_time(pkt.timestamp) - t_start_).count();
    entry.mono_timestamp = std::chrono::duration<double>(pkt.timestamp - mono_t_start_).count();
    entry.start_samples = pkt.start_samples;
    entry.end_samples = pkt.end_samples;
    entry.header_valid = !pkt.internal_flags.invalid_header;
    entry.payload_valid = !pkt.internal_flags.invalid_payload;
    entry.curhop = hdr.curhop;
    entry.nexthop = hdr.nexthop;
    entry.seq = hdr.seq;
    entry.flags = u.bits;
    entry.mgen_flow_uid = pkt.mgen_flow_uid.value_or(0);
    entry.mgen_seqno = pkt.mgen_seqno.value_or(0);
    entry.mcsidx = pkt.mcsidx;
    entry.evm = pkt.evm;
    entry.rssi = pkt.rssi;
    entry.cfo = pkt.cfo;
    entry.fc = pkt.channel.fc;
    entry.bw = pkt.bw;
    entry.chan_fc = pkt.channel.fc;
    entry.chan_bw = pkt.channel.bw;
    entry.demod_latency = pkt.demod_latency;
    entry.tuntap_latency = std::chrono::duration<double>(pkt.tuntap_timestamp - pkt.timestamp).count();
    entry.size = pkt.payload_len;
    if (pkt.symbols && getCollectSource(kRecvSymbols)) {
        entry.symbols.p = pkt.symbols->data();
        entry.symbols.len = pkt.symbols->size();
    } else {
        entry.symbols.p = nullptr;
        entry.symbols.len = 0;
    }

    // Only read from extended header if it is present. It may not be present if
    // the packet is invalid.
    if (pkt.size() >= sizeof(ExtendedHeader)) {
        ExtendedHeader  &ehdr = pkt.ehdr();

        entry.src = ehdr.src;
        entry.dest = ehdr.dest;
        entry.ack = ehdr.ack;
        entry.data_len = ehdr.data_len;
    } else {
        entry.src = 0;
        entry.dest = 0;
        entry.ack = 0;
        entry.data_len = 0;
    }

    recv_->write(&entry, 1);
}

void Logger::logSend_(const MonoClock::time_point& t,
                      DropType dropped,
                      const NetPacket& pkt,
                      const NetPacketInfo& info)
{
    PacketSendEntry      entry;
    const Header         &hdr = pkt.hdr;
    const ExtendedHeader &ehdr = pkt.ehdr();
    u_flags              u;

    u.flags = hdr.flags;

    entry.timestamp = std::chrono::duration<double>(WallClock::to_wall_time(t) - t_start_).count();
    entry.mono_timestamp = std::chrono::duration<double>(t - mono_t_start_).count();
    entry.net_timestamp = std::chrono::duration<double>(pkt.timestamp - mono_t_start_).count();
    entry.wall_timestamp = pkt.wall_timestamp ? std::chrono::duration<double>(*pkt.wall_timestamp - t_start_).count() : 0.0;
    entry.deadline = pkt.deadline ? std::chrono::duration<double>(*pkt.deadline - mono_t_start_).count() : 0.0;
    entry.dropped = dropped;
    entry.nretrans = info.nretrans;
    entry.curhop = hdr.curhop;
    entry.nexthop = hdr.nexthop;
    entry.seq = hdr.seq;
    entry.flags = u.bits;
    entry.src = ehdr.src;
    entry.dest = ehdr.dest;
    entry.ack = ehdr.ack;
    entry.data_len = ehdr.data_len;
    entry.mgen_flow_uid =  pkt.mgen_flow_uid.value_or(0);
    entry.mgen_seqno =  pkt.mgen_seqno.value_or(0);
    entry.mcsidx =  info.mcsidx;

    if (dropped == kNotDropped) {
        entry.fc = info.channel.fc;
        entry.bw = info.channel.bw;
        entry.tuntap_latency = pkt.wall_timestamp ? std::chrono::duration<double>(info.timestamps.tuntap_timestamp - *pkt.wall_timestamp).count() : 0;
        entry.enqueue_latency = info.timestamps.enqueue_timestamp ? std::chrono::duration<double>(*info.timestamps.enqueue_timestamp - pkt.timestamp).count() : 0;
        entry.dequeue_latency = (info.timestamps.dequeue_end_timestamp && info.timestamps.dequeue_start_timestamp)
                                ? std::chrono::duration<double>(*info.timestamps.dequeue_end_timestamp - *info.timestamps.dequeue_start_timestamp).count() : 0;
        entry.queue_latency = info.timestamps.dequeue_end_timestamp ? std::chrono::duration<double>(*info.timestamps.dequeue_end_timestamp - pkt.timestamp).count() : 0;
        entry.llc_latency = std::chrono::duration<double>(info.timestamps.llc_timestamp - pkt.timestamp).count();
        entry.mod_latency = std::chrono::duration<double>(info.timestamps.mod_end_timestamp - info.timestamps.mod_start_timestamp).count();
        entry.synth_latency = std::chrono::duration<double>(info.timestamps.mod_end_timestamp - pkt.timestamp).count();
        entry.size = pkt.size();
        entry.nsamples = info.nsamples;
        if (info.samples && getCollectSource(kSentIQ)) {
            // It's possible that a packet's content is split across two successive
            // IQ buffers. If this happens, we won't have all of the packet's IQ
            // data, so we need to clamp nsamples.
            assert(info.offset <= info.samples->size());
            entry.iq_data.p = info.samples->data() + info.offset;
            entry.iq_data.len = std::min(info.nsamples,
                                        (unsigned) info.samples->size() - info.offset);
        } else {
            entry.iq_data.p = nullptr;
            entry.iq_data.len = 0;
        }
    } else {
        entry.fc = 0;
        entry.bw = 0;
        entry.tuntap_latency = 0;
        entry.enqueue_latency = 0;
        entry.dequeue_latency = 0;
        entry.queue_latency = 0;
        entry.llc_latency = 0;
        entry.mod_latency = 0;
        entry.synth_latency = 0;
        entry.size = 0;
        entry.nsamples = 0;
        entry.iq_data.p = nullptr;
        entry.iq_data.len = 0;
    }

    send_->write(&entry, 1);
}

void Logger::logEvent_(const MonoClock::time_point& t,
                       const char* event)
{
    EventEntry entry;

    entry.timestamp = std::chrono::duration<double>(WallClock::to_wall_time(t) - t_start_).count();
    entry.mono_timestamp = std::chrono::duration<double>(t - mono_t_start_).count();
    entry.event = event;

    event_->write(&entry, 1);

    delete[] event;
}

void Logger::logARQEvent_(const MonoClock::time_point& t,
                          ARQEventType type,
                          NodeId node,
                          Seq seq)
{
    ARQEventEntry entry;

    entry.timestamp = std::chrono::duration<double>(WallClock::to_wall_time(t) - t_start_).count();
    entry.mono_timestamp = std::chrono::duration<double>(t - mono_t_start_).count();
    entry.type = type;
    entry.node = node;
    entry.seq = seq;
    entry.sacks.p = nullptr;
    entry.sacks.len = 0;

    arq_event_->write(&entry, 1);
}

void Logger::logARQSACKEvent_(const Packet& pkt,
                              ARQEventType type,
                              NodeId node,
                              Seq unack,
                              const std::vector<Seq::uint_type>& sacks)
{
    // Log event
    ARQEventEntry entry;

    entry.timestamp = std::chrono::duration<double>(WallClock::to_wall_time(pkt.timestamp) - t_start_).count();
    entry.mono_timestamp = std::chrono::duration<double>(pkt.timestamp - mono_t_start_).count();
    entry.type = type;
    entry.node = node;
    entry.seq = unack;
    entry.sacks.p = const_cast<Seq::uint_type*>(sacks.data());
    entry.sacks.len = sacks.size();

    arq_event_->write(&entry, 1);
}
