// Native (non-liquidDSP) OFDM PHY for the production stack.
//
// Uses the FFTW frame engine in src/simulation (NativeFrame.hh, wire format
// v2) behind the standard PHY/PacketModulator/PacketDemodulator interfaces.
// It is selected explicitly (--phy native-ofdm); liquid OFDM stays the default.
//
// Supported MCS entries: CRC-32; BPSK, QPSK, 8-, 16-, 32-, 64-, 128- or
// 256-QAM (native Gray rectangular QAM, NativeFrame.hh Constellation); FEC
// none, rs8, v27, v29 or punctured (NativeFEC.hh). Any other entry is
// rejected at construction with std::invalid_argument; there is no silent
// fallback. The header section is always QPSK with its own CRC-32.
#ifndef PHY_NATIVEOFDM_HH_
#define PHY_NATIVEOFDM_HH_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "liquid/Modem.hh"
#include "phy/PHY.hh"
#include "simulation/NativeFrame.hh"

class NativeOFDM : public PHY {
public:
    class PacketModulator : public PHY::PacketModulator {
    public:
        explicit PacketModulator(NativeOFDM &phy);
        ~PacketModulator() override;

        void modulate(std::shared_ptr<NetPacket> pkt,
                      const float g,
                      ModPacket &mpkt) override final;

    private:
        NativeOFDM &native_;
        std::unique_ptr<dragonradio::native::FramedPHY> frame_;
    };

    class PacketDemodulator : public PHY::PacketDemodulator {
    public:
        PacketDemodulator(NativeOFDM &phy, unsigned chanidx, const Channel &channel);
        ~PacketDemodulator() override;

        bool isFrameOpen(void) override final;

        void reset(const Channel &channel) override final;

        void timestamp(const MonoClock::time_point &timestamp,
                       std::optional<ssize_t> snapshot_off,
                       ssize_t offset,
                       size_t delay,
                       float rate,
                       float rx_rate) override final;

        void demodulate(const std::complex<float>* data,
                        size_t count) override final;

    private:
        void deliver(const dragonradio::native::FramedStream::Result &r);

        NativeOFDM &native_;
        std::unique_ptr<dragonradio::native::FramedPHY> frame_;
        std::unique_ptr<dragonradio::native::FramedStream> stream_;
        std::vector<dragonradio::native::FramedStream::Result> results_;

        ssize_t delay_ = 0;
        double resamp_rate_ = 1.0;
        double rx_rate_ = 1.0;
        MonoClock::time_point timestamp_;
        std::optional<ssize_t> snapshot_off_;
        ssize_t offset_ = 0;
        /** @brief Stream sample index at the most recent timestamp() call */
        long long sample_start_ = 0;
        /** @brief Stream samples fed since reset() */
        long long sample_end_ = 0;
        std::shared_ptr<Logger> logger_;
    };

    NativeOFDM(const std::vector<std::pair<liquid::MCS, AutoGain>> &mcs_table,
               unsigned int M,
               unsigned int cp_len,
               const std::optional<std::string> &subcarriers,
               unsigned int max_payload = 4096,
               unsigned int chase_bits = 4,
               bool soft_payload = true,
               const std::string &header_fec = "none");
    ~NativeOFDM() override = default;

    NativeOFDM(const NativeOFDM&) = delete;
    NativeOFDM& operator=(const NativeOFDM&) = delete;


    size_t getModulatedSize(mcsidx_t mcsidx, size_t n) override;

    unsigned int getM(void) const { return M_; }
    unsigned int getCPLen(void) const { return cp_len_; }
    unsigned int getMaxPayload(void) const { return max_payload_; }
    unsigned int getChaseBits(void) const { return chase_bits_; }
    std::string getSubcarriers(void) const { return mask_; }
    /** @brief Bits per payload symbol for each MCS index (1..8) */
    const std::vector<int> &getBitsPerSymbol(void) const { return bps_; }
    /** @brief Inner and outer FEC code names for each MCS index */
    std::vector<std::pair<std::string, std::string>> getFEC(void) const;
    /** @brief Decode coded payloads from soft bits (as liquid's soft_payload) */
    bool getSoftPayload(void) const { return soft_payload_; }
    /** @brief Header section code: none, v27 or v29 (a link parameter) */
    std::string getHeaderFEC(void) const { return dragonradio::native::fec::name(header_fec_); }
    /** @brief True if this build supports coded MCS entries */
    static bool fecAvailable(void) { return dragonradio::native::fec::kAvailable; }

protected:
    std::shared_ptr<PHY::PacketModulator> mkPacketModulator(void) override;
    std::shared_ptr<PHY::PacketDemodulator> mkPacketDemodulator(unsigned chanidx, const Channel &channel) override;

    /** @brief Create a frame engine (serialized with every other FFTW planner user) */
    std::unique_ptr<dragonradio::native::FramedPHY> mkFrame(void) const;

protected:
    unsigned int M_, cp_len_, max_payload_, chase_bits_;
    bool soft_payload_;
    dragonradio::native::fec::Code header_fec_;
    int max_on_air_;
    std::string mask_;
    std::vector<int> data_bins_, pilot_bins_;
    std::vector<liquid::MCS> mcs_;
    std::vector<int> bps_;
    std::vector<std::pair<dragonradio::native::fec::Code, dragonradio::native::fec::Code>> fec_;
};

#endif /* PHY_NATIVEOFDM_HH_ */
