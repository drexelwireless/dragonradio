#include <stdint.h>

#include <complex>

#include <H5Cpp.h>

#include "Logger.hh"

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
    /** @brief Was header valid? */
    uint8_t header_valid;
    /** @brief Was payload valid? */
    uint8_t payload_valid;
    /** @brief Packet ID. */
    uint16_t pkt_id;
    /** @brief Packet source. */
    uint8_t src;
    /** @brief Packet destination. */
    uint8_t dest;
    /** @brief Offset (in samples) from timestamp slot to start of frame. */
    uint32_t start_samples;
    /** @brief Offset (in samples) from timestamp slot to end of frame. */
    uint32_t end_samples;
    /** @brief Raw IQ data. */
    hvl_t iq_data;
};

/** @brief Log entry for sent packets */
struct PacketSendEntry {
    /** @brief Timestamp of the slot in which the packet occurred. */
    /** If the packet spans two slots, this is the timestamp of the first slot. */
    double timestamp;
    /** @brief Packet ID. */
    uint16_t pkt_id;
    /** @brief Packet source. */
    uint8_t src;
    /** @brief Packet destination. */
    uint8_t dest;
    /** @brief Raw IQ data. */
    hvl_t iq_data;
};

void addAttribute(H5::H5Location &loc,
                  const std::string& name,
                  const std::string& val)
{
    H5::StrType   h5_type(H5::PredType::C_S1, H5T_VARIABLE);
    H5::DataSpace attr_space(H5S_SCALAR);
    H5::Attribute att = loc.createAttribute(name, h5_type, attr_space);

    att.write(h5_type, val);
}

void addAttribute(H5::H5Location &loc,
                  const std::string& name,
                  uint8_t val)
{
    H5::IntType   h5_type(H5::PredType::NATIVE_UINT8);
    H5::DataSpace attr_space(H5S_SCALAR);
    H5::Attribute att = loc.createAttribute(name, h5_type, attr_space);

    att.write(h5_type, &val);
}

void addAttribute(H5::H5Location &loc,
                  const std::string& name,
                  uint32_t val)
{
    H5::IntType   h5_type(H5::PredType::NATIVE_UINT32);
    H5::DataSpace attr_space(H5S_SCALAR);
    H5::Attribute att = loc.createAttribute(name, h5_type, attr_space);

    att.write(h5_type, &val);
}

void addAttribute(H5::H5Location &loc,
                  const std::string& name,
                  double val)
{
    H5::FloatType h5_type(H5::PredType::NATIVE_DOUBLE);
    H5::DataSpace attr_space(H5S_SCALAR);
    H5::Attribute att = loc.createAttribute(name, h5_type, attr_space);

    att.write(h5_type, &val);
}

Logger::Logger(const std::string& filename,
               NodeId node_id,
               uhd::time_spec_t t_start) :
  _t_start((time_t) t_start.get_full_secs()),
  _t_last_slot((time_t) 0),
  _done(false)
{
    // H5 compound type for complex floats
    // This matches numpy's format
    // See:
    //   https://stackoverflow.com/questions/33621687/c-reading-a-numpy-array-of-complex-numbers-from-an-hdf5-file
    H5::CompType h5_complex32(sizeof(std::complex<float>));

    h5_complex32.insertMember("r", 0, H5::PredType::NATIVE_FLOAT);
    h5_complex32.insertMember("i", sizeof(float), H5::PredType::NATIVE_FLOAT);

    // H5 type for variable-length IQ data
    H5::VarLenType h5_iqdata(&h5_complex32);

    // H5 type for slots
    H5::CompType h5_slot(sizeof(SlotEntry));

    h5_slot.insertMember("timestamp", HOFFSET(SlotEntry, timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_slot.insertMember("iq_data", HOFFSET(SlotEntry, iq_data), h5_iqdata);

    // H5 type for received packets
    H5::CompType h5_packet_recv(sizeof(PacketRecvEntry));

    h5_packet_recv.insertMember("timestamp", HOFFSET(PacketRecvEntry, timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_packet_recv.insertMember("pkt_id", HOFFSET(PacketRecvEntry, pkt_id), H5::PredType::NATIVE_UINT16);
    h5_packet_recv.insertMember("header_valid", HOFFSET(PacketRecvEntry, header_valid), H5::PredType::NATIVE_UINT8);
    h5_packet_recv.insertMember("payload_valid", HOFFSET(PacketRecvEntry, payload_valid), H5::PredType::NATIVE_UINT8);
    h5_packet_recv.insertMember("src", HOFFSET(PacketRecvEntry, src), H5::PredType::NATIVE_UINT8);
    h5_packet_recv.insertMember("dest", HOFFSET(PacketRecvEntry, dest), H5::PredType::NATIVE_UINT8);
    h5_packet_recv.insertMember("start_samples", HOFFSET(PacketRecvEntry, start_samples), H5::PredType::NATIVE_UINT32);
    h5_packet_recv.insertMember("end_samples", HOFFSET(PacketRecvEntry, end_samples), H5::PredType::NATIVE_UINT32);
    h5_packet_recv.insertMember("iq_data", HOFFSET(PacketRecvEntry, iq_data), h5_iqdata);

    // H5 type for sent packets
    H5::CompType h5_packet_send(sizeof(PacketSendEntry));

    h5_packet_send.insertMember("timestamp", HOFFSET(PacketSendEntry, timestamp), H5::PredType::NATIVE_DOUBLE);
    h5_packet_send.insertMember("pkt_id", HOFFSET(PacketSendEntry, pkt_id), H5::PredType::NATIVE_UINT16);
    h5_packet_send.insertMember("src", HOFFSET(PacketSendEntry, src), H5::PredType::NATIVE_UINT8);
    h5_packet_send.insertMember("dest", HOFFSET(PacketSendEntry, dest), H5::PredType::NATIVE_UINT8);
    h5_packet_send.insertMember("iq_data", HOFFSET(PacketSendEntry, iq_data), h5_iqdata);

    // Create H5 groups
    _file = H5::H5File(filename, H5F_ACC_TRUNC);
    addAttribute(_file, "node_id", node_id);
    addAttribute(_file, "start", (uint32_t) t_start.get_full_secs());

    _slots = std::make_unique<ExtensibleDataSet>(_file, "slots", h5_slot);
    _recv = std::make_unique<ExtensibleDataSet>(_file, "recv", h5_packet_recv);
    _send = std::make_unique<ExtensibleDataSet>(_file, "send", h5_packet_send);

    // Start worker thread
    worker_thread = std::thread(&Logger::worker, this);
}

Logger::~Logger()
{
    _file.close();
}

void Logger::setTXBandwidth(double bw)
{
    addAttribute(_file, "tx_bandwidth", bw);
}

void Logger::setRXBandwidth(double bw)
{
    addAttribute(_file, "rx_bandwidth", bw);
}

void Logger::stop(void)
{
    _done = true;

    log_q.stop();

    if (worker_thread.joinable())
        worker_thread.join();
}

void Logger::logSlot(std::shared_ptr<IQBuf> buf)
{
    // Only log slots we haven't logged before. We should never be asked to log
    // a slot that is older than the youngest slot we've ever logged.
    if (buf->timestamp > _t_last_slot) {
        log_q.emplace([=](){ _logSlot(buf); });
        _t_last_slot = buf->timestamp;
    }
}


void Logger::logRecv(const uhd::time_spec_t& t,
                     bool header_valid,
                     bool payload_valid,
                     const Header& hdr,
                     uint32_t start_samples,
                     uint32_t end_samples,
                     std::shared_ptr<buffer<std::complex<float>>> buf)
{
    log_q.emplace([=](){ _logRecv(t, header_valid, payload_valid, hdr, start_samples, end_samples, buf); });
}

void Logger::logSend(const uhd::time_spec_t& t,
                     const Header& hdr,
                     std::shared_ptr<IQBuf> buf)
{
    log_q.emplace([=](){ _logSend(t, hdr, buf); });
}

void Logger::worker(void)
{
    std::function<void()> entry;

    while (!_done) {
        log_q.pop(entry);
        if (_done)
            break;

        entry();
    }
}

void Logger::_logSlot(std::shared_ptr<IQBuf> buf)
{
    SlotEntry entry;

    entry.timestamp = (buf->timestamp - _t_start).get_real_secs();
    entry.iq_data.p = &(*buf)[0];
    entry.iq_data.len = buf->size();

    _slots->write(&entry, 1);
}

void Logger::_logRecv(const uhd::time_spec_t& t,
                      bool header_valid,
                      bool payload_valid,
                      const Header& hdr,
                      uint32_t start_samples,
                      uint32_t end_samples,
                      std::shared_ptr<buffer<std::complex<float>>> buf)
{
    PacketRecvEntry entry;

    entry.timestamp = (t - _t_start).get_real_secs();
    entry.header_valid = header_valid;
    entry.payload_valid = payload_valid;
    entry.pkt_id = hdr.pkt_id;
    entry.src = hdr.src;
    entry.dest = hdr.dest;
    entry.start_samples = start_samples;
    entry.end_samples = end_samples;
    entry.iq_data.p = &(*buf)[0];
    entry.iq_data.len = buf->size();

    _recv->write(&entry, 1);
}

void Logger::_logSend(const uhd::time_spec_t& t,
                      const Header& hdr,
                      std::shared_ptr<IQBuf> buf)
{
    PacketSendEntry entry;

    entry.timestamp = (t - _t_start).get_real_secs();
    entry.pkt_id = hdr.pkt_id;
    entry.src = hdr.src;
    entry.dest = hdr.dest;
    entry.iq_data.p = &(buf->data)[0];
    entry.iq_data.len = buf->data.size();

    _send->write(&entry, 1);
}
