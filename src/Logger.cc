#include <stdint.h>

#include <complex>

#include <H5Cpp.h>

#include "Clock.hh"
#include "Logger.hh"
#include "RadioConfig.hh"

std::shared_ptr<Logger> logger;

/** @brief Log entry for slots */
struct SlotEntry {
    double timestamp;
    hvl_t iq_data;
};

/** @brief Log entry for received packets */
struct PacketRecvEntry {
    /** @brief Timestamp of the slot in which the packet occurred. */
    /** If the packet spans two slots, this is the timestamp of the first slot. */
    double timestamp;
    /** @brief Offset (in samples) from timestamp slot to start of frame. */
    uint32_t start_samples;
    /** @brief Offset (in samples) from timestamp slot to end of frame. */
    uint32_t end_samples;
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
    /** @brief Packet source. */
    uint8_t src;
    /** @brief Packet destination. */
    uint8_t dest;
    /** @brief Liquid CRC scheme. */
    crc_scheme crc;
    /** @brief Liquid inner FEC scheme. */
    fec_scheme fec0;
    /** @brief Liquid outer FEC scheme. */
    fec_scheme fec1;
    /** @brief Liquid modulation scheme. */
    modulation_scheme ms;
    /** @brief EVM [dB]. */
    float evm;
    /** @brief RSSI [dB]. */
    float rssi;
    /** @brief CFO [f/Fs]. */
    float cfo;
    /** @brief Data size (bytes). */
    uint32_t size;
    /** @brief Raw IQ data. */
    hvl_t iq_data;
};

/** @brief Log entry for sent packets */
struct PacketSendEntry {
    /** @brief Timestamp of the slot in which the packet occurred. */
    /** If the packet spans two slots, this is the timestamp of the first slot. */
    double timestamp;
    /** @brief Packet current hop. */
    uint8_t curhop;
    /** @brief Packet next hop. */
    uint8_t nexthop;
    /** @brief Packet sequence number. */
    uint16_t seq;
    /** @brief Packet source. */
    uint8_t src;
    /** @brief Packet destination. */
    uint8_t dest;
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

Logger::Logger(Clock::time_point t_start) :
  t_start_(t_start),
  t_last_slot_((time_t) 0),
  sources_(0),
  done_(false)
{
}

Logger::~Logger()
{
    stop();
    file_.close();
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

    // H5 type for Liquid CRC scheme
    H5::EnumType h5_crc_scheme(sizeof(crc_scheme));
    crc_scheme crc;

    for (unsigned int i = 0; i < LIQUID_CRC_NUM_SCHEMES; ++i)
        h5_crc_scheme.insert(crc_scheme_str[i][0], (crc=static_cast<crc_scheme>(i), &crc));

    // H5 type for Liquid FEC scheme
    H5::EnumType h5_fec_scheme(sizeof(fec_scheme));
    fec_scheme fec;

    for (unsigned int i = 0; i < LIQUID_FEC_NUM_SCHEMES; ++i)
        h5_fec_scheme.insert(fec_scheme_str[i][0], (fec=static_cast<fec_scheme>(i), &fec));

    // H5 type for Liquid modulation scheme
    H5::EnumType h5_modulation_scheme(sizeof(modulation_scheme));
    modulation_scheme ms;

    for (unsigned int i = 0; i < LIQUID_MODEM_NUM_SCHEMES; ++i)
        h5_modulation_scheme.insert(modulation_types[i].name, (ms=static_cast<modulation_scheme>(i), &ms));

    // H5 type for slots
    H5::CompType h5_slot(sizeof(SlotEntry));

    h5_slot.insertMember("timestamp", HOFFSET(SlotEntry, timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_slot.insertMember("iq_data", HOFFSET(SlotEntry, iq_data), h5_iqdata);

    // H5 type for received packets
    H5::CompType h5_packet_recv(sizeof(PacketRecvEntry));

    h5_packet_recv.insertMember("timestamp", HOFFSET(PacketRecvEntry, timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_packet_recv.insertMember("start_samples", HOFFSET(PacketRecvEntry, start_samples), H5::PredType::NATIVE_UINT32);
    h5_packet_recv.insertMember("end_samples", HOFFSET(PacketRecvEntry, end_samples), H5::PredType::NATIVE_UINT32);
    h5_packet_recv.insertMember("header_valid", HOFFSET(PacketRecvEntry, header_valid), H5::PredType::NATIVE_UINT8);
    h5_packet_recv.insertMember("payload_valid", HOFFSET(PacketRecvEntry, payload_valid), H5::PredType::NATIVE_UINT8);
    h5_packet_recv.insertMember("curhop", HOFFSET(PacketRecvEntry, curhop), H5::PredType::NATIVE_UINT8);
    h5_packet_recv.insertMember("nexthop", HOFFSET(PacketRecvEntry, nexthop), H5::PredType::NATIVE_UINT8);
    h5_packet_recv.insertMember("seq", HOFFSET(PacketRecvEntry, seq), H5::PredType::NATIVE_UINT16);
    h5_packet_recv.insertMember("src", HOFFSET(PacketRecvEntry, src), H5::PredType::NATIVE_UINT8);
    h5_packet_recv.insertMember("dest", HOFFSET(PacketRecvEntry, dest), H5::PredType::NATIVE_UINT8);
    h5_packet_recv.insertMember("crc", HOFFSET(PacketRecvEntry, crc), h5_crc_scheme);
    h5_packet_recv.insertMember("fec0", HOFFSET(PacketRecvEntry, fec0), h5_fec_scheme);
    h5_packet_recv.insertMember("fec1", HOFFSET(PacketRecvEntry, fec1), h5_fec_scheme);
    h5_packet_recv.insertMember("ms", HOFFSET(PacketRecvEntry, ms), h5_modulation_scheme);
    h5_packet_recv.insertMember("evm", HOFFSET(PacketRecvEntry, evm), H5::PredType::NATIVE_FLOAT);
    h5_packet_recv.insertMember("rssi", HOFFSET(PacketRecvEntry, rssi), H5::PredType::NATIVE_FLOAT);
    h5_packet_recv.insertMember("cfo", HOFFSET(PacketRecvEntry, cfo), H5::PredType::NATIVE_FLOAT);
    h5_packet_recv.insertMember("size", HOFFSET(PacketRecvEntry, size), H5::PredType::NATIVE_UINT32);
    h5_packet_recv.insertMember("iq_data", HOFFSET(PacketRecvEntry, iq_data), h5_iqdata);

    // H5 type for sent packets
    H5::CompType h5_packet_send(sizeof(PacketSendEntry));

    h5_packet_send.insertMember("timestamp", HOFFSET(PacketSendEntry, timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_packet_send.insertMember("curhop", HOFFSET(PacketSendEntry, curhop), H5::PredType::NATIVE_UINT8);
    h5_packet_send.insertMember("nexthop", HOFFSET(PacketSendEntry, nexthop), H5::PredType::NATIVE_UINT8);
    h5_packet_send.insertMember("seq", HOFFSET(PacketSendEntry, seq), H5::PredType::NATIVE_UINT16);
    h5_packet_send.insertMember("src", HOFFSET(PacketSendEntry, src), H5::PredType::NATIVE_UINT8);
    h5_packet_send.insertMember("dest", HOFFSET(PacketSendEntry, dest), H5::PredType::NATIVE_UINT8);
    h5_packet_send.insertMember("size", HOFFSET(PacketSendEntry, size), H5::PredType::NATIVE_UINT32);
    h5_packet_send.insertMember("iq_data", HOFFSET(PacketSendEntry, iq_data), h5_iqdata);

    // H5 type for events
    H5::CompType h5_event(sizeof(EventEntry));

    h5_event.insertMember("timestamp", HOFFSET(EventEntry, timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_event.insertMember("event", HOFFSET(EventEntry, event), h5_string);

    // Create H5 groups
    file_ = H5::H5File(filename, H5F_ACC_TRUNC);

    slots_ = std::make_unique<ExtensibleDataSet>(file_, "slots", h5_slot);
    recv_ = std::make_unique<ExtensibleDataSet>(file_, "recv", h5_packet_recv);
    send_ = std::make_unique<ExtensibleDataSet>(file_, "send", h5_packet_send);
    event_ = std::make_unique<ExtensibleDataSet>(file_, "event", h5_event);

    // Start worker thread
    worker_thread_ = std::thread(&Logger::worker, this);
}

bool Logger::getCollectSource(Source src)
{
    return sources_ & (1 << src);
}

void Logger::setCollectSource(Source src, bool collect)
{
    if (collect)
        sources_ |= 1 << src;
    else
        sources_ &= ~(1 << src);
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

void Logger::logSlot(std::shared_ptr<IQBuf> buf)
{
    if (getCollectSource(kSlots)) {
        // Only log slots we haven't logged before. We should never be asked to log
        // a slot that is older than the youngest slot we've ever logged.
        if (buf->timestamp > t_last_slot_) {
            log_q_.emplace([=](){ logSlot_(buf); });
            t_last_slot_ = buf->timestamp;
        }
    }
}

void Logger::logRecv(const Clock::time_point& t,
                     uint32_t start_samples,
                     uint32_t end_samples,
                     bool header_valid,
                     bool payload_valid,
                     const Header& hdr,
                     NodeId src,
                     NodeId dest,
                     crc_scheme crc,
                     fec_scheme fec0,
                     fec_scheme fec1,
                     modulation_scheme ms,
                     float evm,
                     float rssi,
                     float cfo,
                     uint32_t size,
                     std::shared_ptr<buffer<std::complex<float>>> buf)
{
    if (getCollectSource(kRecvPackets))
        log_q_.emplace([=](){ logRecv_(t, start_samples, end_samples, header_valid, payload_valid, hdr, src, dest, crc, fec0, fec1, ms, evm, rssi, cfo, size, buf); });
}

void Logger::logSend(const Clock::time_point& t,
                     const Header& hdr,
                     NodeId src,
                     NodeId dest,
                     uint32_t size,
                     std::shared_ptr<IQBuf> buf)
{
    if (getCollectSource(kSentPackets))
        log_q_.emplace([=](){ logSend_(t, hdr, src, dest, size, buf); });
}

void Logger::logEvent(const Clock::time_point& t, const char *fmt, va_list ap0)
{
    if (getCollectSource(kEvents)) {
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

        if (rc.verbose)
            fprintf(stderr, "%s\n", s.c_str());

        log_q_.emplace([=](){ logEvent_(t, s); });
    }
}

void Logger::stop(void)
{
    done_ = true;

    log_q_.stop();

    if (worker_thread_.joinable())
        worker_thread_.join();
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

void Logger::logSlot_(std::shared_ptr<IQBuf> buf)
{
    SlotEntry entry;

    entry.timestamp = (buf->timestamp - t_start_).get_real_secs();
    entry.iq_data.p = &(*buf)[0];
    entry.iq_data.len = buf->size();

    slots_->write(&entry, 1);
}

void Logger::logRecv_(const Clock::time_point& t,
                      uint32_t start_samples,
                      uint32_t end_samples,
                      bool header_valid,
                      bool payload_valid,
                      const Header& hdr,
                      NodeId src,
                      NodeId dest,
                      crc_scheme crc,
                      fec_scheme fec0,
                      fec_scheme fec1,
                      modulation_scheme ms,
                      float evm,
                      float rssi,
                      float cfo,
                      uint32_t size,
                      std::shared_ptr<buffer<std::complex<float>>> buf)
{
    PacketRecvEntry entry;

    entry.timestamp = (t - t_start_).get_real_secs();
    entry.start_samples = start_samples;
    entry.end_samples = end_samples;
    entry.header_valid = header_valid;
    entry.payload_valid = payload_valid;
    entry.curhop = hdr.curhop;
    entry.nexthop = hdr.nexthop;
    entry.seq = hdr.seq;
    entry.src = src;
    entry.dest = dest;
    entry.crc = crc;
    entry.fec0 = fec0;
    entry.fec1 = fec1;
    entry.ms = ms;
    entry.evm = evm;
    entry.rssi = rssi;
    entry.cfo = cfo;
    entry.size = size;
    if (getCollectSource(kRecvData)) {
        entry.iq_data.p = buf->data();
        entry.iq_data.len = buf->size();
    } else {
        entry.iq_data.p = nullptr;
        entry.iq_data.len = 0;
    }

    recv_->write(&entry, 1);
}

void Logger::logSend_(const Clock::time_point& t,
                      const Header& hdr,
                      NodeId src,
                      NodeId dest,
                      uint32_t size,
                      std::shared_ptr<IQBuf> buf)
{
    PacketSendEntry entry;

    entry.timestamp = (t - t_start_).get_real_secs();
    entry.curhop = hdr.curhop;
    entry.nexthop = hdr.nexthop;
    entry.seq = hdr.seq;
    entry.src = src;
    entry.dest = dest;
    entry.size = size;
    if (getCollectSource(kSentData)) {
        entry.iq_data.p = buf->data();
        entry.iq_data.len = buf->size();
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
