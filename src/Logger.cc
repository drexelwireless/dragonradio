#include <stdint.h>

#include <complex>

#include <H5Cpp.h>

#include "Clock.hh"
#include "IQCompression.hh"
#include "Logger.hh"
#include "RadioConfig.hh"

std::shared_ptr<Logger> logger;

/** @brief Log entry for slots */
struct SlotEntry {
    /** @brief Receive timestamp. */
    double timestamp;
    /** @brief Bandwidth [Hz] */
    float bw;
    /** @brief Raw IQ data. */
    hvl_t iq_data;
};

/** @brief Log entry for snapshots */
struct SnapshotEntry {
    /** @brief Receive timestamp. */
    double timestamp;
    /** @brief Sampling frequency [Hz] */
    float fs;
    /** @brief Compressed IQ data. */
    hvl_t iq_data;
};

/** @brief Log entry for self-transmission events */
struct SelfTXEntry {
    /** @brief Timestamp of snapshot this self-transmission belongs to. */
    double timestamp;
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
    double timestamp;
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
    /** @brief Data length. */
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
    /** @brief Demodulation latency (sec) */
    float demod_latency;
    /** @brief Data size (bytes). */
    uint32_t size;
    /** @brief Raw IQ data. */
    hvl_t symbols;
};

/** @brief Log entry for sent packets */
struct PacketSendEntry {
    /** @brief Timestamp of the slot in which the packet occurred. */
    /** If the packet spans two slots, this is the timestamp of the first slot. */
    double timestamp;
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
    /** @brief Data length. */
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
    /** @brief Modulation latency [sec] */
    double mod_latency;
    /** @brief Data size (bytes). */
    uint32_t size;
    /** @brief Raw IQ data. */
    hvl_t iq_data;
};

/** @brief Generic event */
struct EventEntry {
    double timestamp;
    const char *event;
};

Logger::Logger(Clock::time_point t_start)
  : is_open_(false)
  , t_start_(t_start)
  , t_last_slot_((time_t) 0)
  , sources_(0)
  , done_(false)
{
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
    h5_slot.insertMember("bw", HOFFSET(SlotEntry, bw), H5::PredType::NATIVE_FLOAT);
    h5_slot.insertMember("iq_data", HOFFSET(SlotEntry, iq_data), h5_iqdata);

    // H5 type for snapshots
    H5::CompType h5_snapshot(sizeof(SnapshotEntry));

    h5_snapshot.insertMember("timestamp", HOFFSET(SnapshotEntry, timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_snapshot.insertMember("fs", HOFFSET(SnapshotEntry, fs), H5::PredType::NATIVE_FLOAT);
    h5_snapshot.insertMember("iq_data", HOFFSET(SnapshotEntry, iq_data), h5_compressed_iqdata);

    // H5 type for snapshot self-transmission events
    H5::CompType h5_selftx(sizeof(SelfTXEntry));

    h5_selftx.insertMember("timestamp", HOFFSET(SelfTXEntry, timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_selftx.insertMember("is_local", HOFFSET(SelfTXEntry, is_local), H5::PredType::NATIVE_UINT8);
    h5_selftx.insertMember("start", HOFFSET(SelfTXEntry, start), H5::PredType::NATIVE_INT32);
    h5_selftx.insertMember("end", HOFFSET(SelfTXEntry, end), H5::PredType::NATIVE_INT32);
    h5_selftx.insertMember("fc", HOFFSET(SelfTXEntry, fc), H5::PredType::NATIVE_FLOAT);
    h5_selftx.insertMember("fs", HOFFSET(SelfTXEntry, fs), H5::PredType::NATIVE_FLOAT);

    // H5 type for received packets
    H5::CompType h5_packet_recv(sizeof(PacketRecvEntry));

    h5_packet_recv.insertMember("timestamp", HOFFSET(PacketRecvEntry, timestamp), H5::PredType::NATIVE_DOUBLE);
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
    h5_packet_recv.insertMember("demod_latency", HOFFSET(PacketRecvEntry, demod_latency), H5::PredType::NATIVE_FLOAT);
    h5_packet_recv.insertMember("size", HOFFSET(PacketRecvEntry, size), H5::PredType::NATIVE_UINT32);
    h5_packet_recv.insertMember("symbols", HOFFSET(PacketRecvEntry, symbols), h5_iqdata);

    // H5 type for sent packets
    H5::CompType h5_packet_send(sizeof(PacketSendEntry));

    h5_packet_send.insertMember("timestamp", HOFFSET(PacketSendEntry, timestamp), H5::PredType::NATIVE_DOUBLE);
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
    h5_packet_send.insertMember("mod_latency", HOFFSET(PacketSendEntry, mod_latency), H5::PredType::NATIVE_DOUBLE);
    h5_packet_send.insertMember("size", HOFFSET(PacketSendEntry, size), H5::PredType::NATIVE_UINT32);
    h5_packet_send.insertMember("iq_data", HOFFSET(PacketSendEntry, iq_data), h5_iqdata);

    // H5 type for events
    H5::CompType h5_event(sizeof(EventEntry));

    h5_event.insertMember("timestamp", HOFFSET(EventEntry, timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_event.insertMember("event", HOFFSET(EventEntry, event), h5_string);

    // Create H5 groups
    file_ = H5::H5File(filename, H5F_ACC_TRUNC);

    slots_ = std::make_unique<ExtensibleDataSet>(file_, "slots", h5_slot);
    snapshots_ = std::make_unique<ExtensibleDataSet>(file_, "snapshots", h5_snapshot);
    selftx_ = std::make_unique<ExtensibleDataSet>(file_, "selftx", h5_selftx);
    recv_ = std::make_unique<ExtensibleDataSet>(file_, "recv", h5_packet_recv);
    send_ = std::make_unique<ExtensibleDataSet>(file_, "send", h5_packet_send);
    event_ = std::make_unique<ExtensibleDataSet>(file_, "event", h5_event);

    // Start worker thread
    worker_thread_ = std::thread(&Logger::worker, this);

    is_open_ = true;
}

void Logger::stop(void)
{
    done_ = true;

    log_q_.stop();

    if (worker_thread_.joinable())
        worker_thread_.join();
}

void Logger::close(void)
{
    if (is_open_) {
        stop();
        slots_.reset();
        snapshots_.reset();
        selftx_.reset();
        recv_.reset();
        send_.reset();
        event_.reset();
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

void Logger::setAttribute(const std::string& name, double val)
{
    H5::FloatType h5_type(H5::PredType::NATIVE_DOUBLE);
    H5::DataSpace attr_space(H5S_SCALAR);
    H5::Attribute att = createOrOpenAttribute(name, h5_type, attr_space);

    att.write(h5_type, &val);
}

void Logger::logSlot(std::shared_ptr<IQBuf> buf,
                     float bw)
{
    if (getCollectSource(kSlots)) {
        // Only log slots we haven't logged before. We should never be asked to log
        // a slot that is older than the youngest slot we've ever logged.
        if (buf->timestamp > t_last_slot_) {
            log_q_.emplace([=](){ logSlot_(buf, bw); });
            t_last_slot_ = buf->timestamp;
        }
    }
}

void Logger::worker(void)
{
    std::function<void()> entry;

    while (!done_) {
        log_q_.pop(entry);
        if (done_)
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

void Logger::logSlot_(std::shared_ptr<IQBuf> buf,
                      float bw)
{
    SlotEntry entry;

    entry.timestamp = (Clock::to_wall_time(buf->timestamp) - t_start_).get_real_secs();
    entry.bw = bw;
    entry.iq_data.p = &(*buf)[0];
    entry.iq_data.len = buf->size();

    slots_->write(&entry, 1);
}

void Logger::logSnapshot_(std::shared_ptr<IQBuf> buf)
{
    SnapshotEntry entry;
    buffer<char>  data;

    data = compressFLAC(8, buf->data(), buf->size());

    entry.timestamp = (Clock::to_wall_time(buf->timestamp) - t_start_).get_real_secs();
    entry.fs = buf->fs;

    entry.iq_data.p = data.data();
    entry.iq_data.len = data.size();

    snapshots_->write(&entry, 1);
}

void Logger::logSelfTX_(Clock::time_point timestamp,
                        SelfTX selftx)
{
    SelfTXEntry entry;

    entry.timestamp = (timestamp - t_start_).get_real_secs();
    entry.is_local = selftx.is_local;
    entry.start = selftx.start;
    entry.end = selftx.end;
    entry.fc = selftx.fc;
    entry.fs = selftx.fs;

    selftx_->write(&entry, 1);
}

typedef union {
    Header::Flags flags;
    uint8_t bits;
} u_flags;

void Logger::logRecv_(const Clock::time_point& t,
                      int32_t start_samples,
                      int32_t end_samples,
                      bool header_valid,
                      bool payload_valid,
                      const Header& hdr,
                      const ExtendedHeader& ehdr,
                      uint32_t mgen_flow_uid,
                      uint32_t mgen_seqno,
                      unsigned mcsidx,
                      float evm,
                      float rssi,
                      float cfo,
                      float fc,
                      float bw,
                      float demod_latency,
                      uint32_t size,
                      std::shared_ptr<buffer<std::complex<float>>> symbols)
{
    PacketRecvEntry entry;
    u_flags         u;

    u.flags = hdr.flags;

    entry.timestamp = (t - t_start_).get_real_secs();
    entry.start_samples = start_samples;
    entry.end_samples = end_samples;
    entry.header_valid = header_valid;
    entry.payload_valid = payload_valid;
    entry.curhop = hdr.curhop;
    entry.nexthop = hdr.nexthop;
    entry.seq = hdr.seq;
    entry.flags = u.bits;
    entry.src = ehdr.src;
    entry.dest = ehdr.dest;
    entry.ack = ehdr.ack;
    entry.data_len = ehdr.data_len;
    entry.mgen_flow_uid = mgen_flow_uid;
    entry.mgen_seqno = mgen_seqno;
    entry.mcsidx = mcsidx;
    entry.evm = evm;
    entry.rssi = rssi;
    entry.cfo = cfo;
    entry.fc = fc;
    entry.bw = bw;
    entry.demod_latency = demod_latency;
    entry.size = size;
    if (getCollectSource(kRecvSymbols)) {
        entry.symbols.p = symbols->data();
        entry.symbols.len = symbols->size();
    } else {
        entry.symbols.p = nullptr;
        entry.symbols.len = 0;
    }

    recv_->write(&entry, 1);
}

void Logger::logSend_(const Clock::time_point& t,
                      DropType dropped,
                      unsigned nretrans,
                      const Header& hdr,
                      const ExtendedHeader& ehdr,
                      uint32_t mgen_flow_uid,
                      uint32_t mgen_seqno,
                      unsigned mcsidx,
                      float fc,
                      float bw,
                      double mod_latency,
                      uint32_t size,
                      std::shared_ptr<IQBuf> buf,
                      size_t offset,
                      size_t nsamples)
{
    PacketSendEntry entry;
    u_flags         u;

    u.flags = hdr.flags;

    entry.timestamp = (t - t_start_).get_real_secs();
    entry.dropped = dropped;
    entry.nretrans = nretrans;
    entry.curhop = hdr.curhop;
    entry.nexthop = hdr.nexthop;
    entry.seq = hdr.seq;
    entry.flags = u.bits;
    entry.src = ehdr.src;
    entry.dest = ehdr.dest;
    entry.ack = ehdr.ack;
    entry.data_len = ehdr.data_len;
    entry.mgen_flow_uid = mgen_flow_uid;
    entry.mgen_seqno = mgen_seqno;
    entry.mcsidx = mcsidx;
    entry.fc = fc;
    entry.bw = bw;
    entry.mod_latency = mod_latency;
    entry.size = size;
    if (buf && getCollectSource(kSentIQ)) {
        // It's possible that a packet's content is split across two successive
        // IQ buffers. If this happens, we won't have all of the packet's IQ
        // data, so we need to clamp nsamples.
        assert(offset <= buf->size());
        entry.iq_data.p = buf->data() + offset;
        entry.iq_data.len = std::min(nsamples, (unsigned) buf->size() - offset);
    } else {
        entry.iq_data.p = nullptr;
        entry.iq_data.len = 0;
    }

    send_->write(&entry, 1);
}

void Logger::logEvent_(const Clock::time_point& t,
                       const std::string& s)
{
    EventEntry entry;

    entry.timestamp = (t - t_start_).get_real_secs();
    entry.event = s.c_str();

    event_->write(&entry, 1);
}

void vlogEvent(const Clock::time_point& t, const char *fmt, va_list ap0)
{
    std::shared_ptr<Logger> l = logger;

    if (rc.debug || (l && l->getCollectSource(Logger::kEvents))) {
        int                     n = 2 * strlen(fmt);
        std::unique_ptr<char[]> buf;
        va_list                 ap;

        for (;;) {
            buf.reset(new char[n]);

            va_copy(ap, ap0);
            int count = vsnprintf(&buf[0], n, fmt, ap);
            va_end(ap);

            if (count < 0 || count >= n)
                n *= 2;
            else
                break;
        }

        std::string s { buf.get() };

        if (rc.debug)
            fprintf(stderr, "%s\n", s.c_str());

        if (l && l->getCollectSource(Logger::kEvents))
            l->logEvent(t, s);
    }
}
