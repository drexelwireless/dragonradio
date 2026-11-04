// Native downlink OFDMA PHY for the production stack (P5).
//
// Built on NativeOFDM (same MCS validation, mask, codes and constellations)
// and the OFDMA frame engine (src/simulation/NativeOFDMA.hh). A burst carries
// one packet on every tone, or two packets for different destinations on
// disjoint tone sets with one shared IQ buffer (PacketModulator::
// modulateBurst; the slot synthesizer batches). A receiver demodulates only
// the user slots the production packet filter wants (its own next hop,
// broadcast). Selected explicitly (--phy native-ofdma); liquid OFDM stays the
// default. Not interoperable with native-ofdm or liquid OFDM.
#ifndef PHY_NATIVEOFDMA_HH_
#define PHY_NATIVEOFDMA_HH_

#include "phy/NativeOFDM.hh"
#include "simulation/NativeOFDMA.hh"

class NativeOFDMA : public NativeOFDM {
public:
    class PacketModulator : public PHY::PacketModulator {
    public:
        explicit PacketModulator(NativeOFDMA &phy);
        ~PacketModulator() override;

        void modulate(std::shared_ptr<NetPacket> pkt, const float g, ModPacket &mpkt) override final;

        void modulateBurst(std::vector<std::shared_ptr<NetPacket>> pkts, const float g, ModPacket &mpkt) override final;

    private:
        NativeOFDMA &ofdma_;
        std::unique_ptr<dragonradio::native::FramedOFDMA> frame_;
    };

    class PacketDemodulator : public PHY::PacketDemodulator {
    public:
        PacketDemodulator(NativeOFDMA &phy, unsigned chanidx, const Channel &channel);
        ~PacketDemodulator() override;

        bool isFrameOpen(void) override final;
        void reset(const Channel &channel) override final;
        void timestamp(const MonoClock::time_point &timestamp, std::optional<ssize_t> snapshot_off, ssize_t offset,
                       size_t delay, float rate, float rx_rate) override final;
        void demodulate(const std::complex<float>* data, size_t count) override final;

    private:
        void deliver(const dragonradio::native::FramedOFDMAStream::Result &r);

        NativeOFDMA &ofdma_;
        std::unique_ptr<dragonradio::native::FramedOFDMA> frame_;
        std::unique_ptr<dragonradio::native::FramedOFDMAStream> stream_;
        std::vector<dragonradio::native::FramedOFDMAStream::Result> results_;
        ssize_t delay_ = 0;
        double resamp_rate_ = 1.0;
        double rx_rate_ = 1.0;
        MonoClock::time_point timestamp_;
        std::optional<ssize_t> snapshot_off_;
        ssize_t offset_ = 0;
        long long sample_start_ = 0;
        long long sample_end_ = 0;
    };

    /** @param allocation Two-user allocation: "equal", "1:2" or "2:1" (slot 0 : slot 1) */
    NativeOFDMA(const std::vector<std::pair<liquid::MCS, AutoGain>> &mcs_table,
                unsigned int M,
                unsigned int cp_len,
                const std::optional<std::string> &subcarriers,
                unsigned int max_payload = 4096,
                bool soft_payload = true,
                const std::string &allocation = "equal");
    ~NativeOFDMA() override = default;

    unsigned getMaxBurstUsers(void) const override { return 2; }

    /** @brief Samples of a single-user burst (every tone) carrying n bytes after the extended header. */
    size_t getModulatedSize(mcsidx_t mcsidx, size_t n) override;

    std::string getAllocation(void) const { return allocation_name_; }

protected:
    std::shared_ptr<PHY::PacketModulator> mkPacketModulator(void) override;
    std::shared_ptr<PHY::PacketDemodulator> mkPacketDemodulator(unsigned chanidx, const Channel &channel) override;

    std::unique_ptr<dragonradio::native::FramedOFDMA> mkOFDMA(void) const;

private:
    int two_user_alloc_;
    std::string allocation_name_;
};

#endif /* PHY_NATIVEOFDMA_HH_ */
