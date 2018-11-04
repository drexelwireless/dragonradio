#include "phy/LiquidPHY.hh"

/** @brief A %PHY thats uses the liquid-usrp flexframegen code. */
class FlexFrame : public LiquidPHY {
public:
    /** @brief Modulate IQ data using a liquid-usrp flexframe. */
    class Modulator : public LiquidPHY::Modulator
    {
    public:
        Modulator(FlexFrame& phy);
        ~Modulator();

        Modulator(const Modulator&) = delete;
        Modulator(Modulator&& other) = delete;

        Modulator& operator=(const Modulator&) = delete;
        Modulator& operator=(Modulator&&) = delete;

        /** @brief Print internals of the associated flexframegen. */
        void print(void);

    private:
        /** @brief Associated FlexFrame PHY. */
        FlexFrame &myphy_;

        /** @brief The liquid-dsp flexframegen object */
        flexframe(gen) fg_;

        /** @brief The liquid-dsp flexframegenprops object  associated with this
          * flexframegen.
          */
        flexframe(genprops_s) fgprops_;

        /** Update frame properties to match fgprops_. */
        void update_props(const TXParams &params);

        void assemble(unsigned char *hdr, NetPacket& pkt) override final;

        size_t maxModulatedSamples(void) override final;

        bool modulateSamples(std::complex<float> *buf, size_t &nw) override final;
    };

    /** @brief Demodulate IQ data using a liquid-usrp flexframe. */
    class Demodulator : public LiquidPHY::Demodulator
    {
    public:
        Demodulator(FlexFrame &phy);
        ~Demodulator();

        Demodulator(const Demodulator&) = delete;
        Demodulator(Demodulator&& other) = delete;

        Demodulator& operator=(const Demodulator&) = delete;
        Demodulator& operator=(Demodulator&&) = delete;

        /** @brief Print internals of the associated flexframesync. */
        void print(void);

    private:
        /** @brief The liquid-dsp flexframesync object */
        flexframe(sync) fs_;

        void liquidReset(void) override final;

        void demodulateSamples(std::complex<float> *buf, const size_t n) override final;
    };

    FlexFrame(NodeId node_id,
              const MCS &mcs,
              bool soft_header,
              bool soft_payload,
              size_t min_packet_size)
      : LiquidPHY(node_id, mcs, soft_header, soft_payload, min_packet_size)
    {
    }

    ~FlexFrame()
    {
    }

    FlexFrame(const FlexFrame&) = delete;
    FlexFrame(FlexFrame&&) = delete;

    FlexFrame& operator=(const FlexFrame&) = delete;
    FlexFrame& operator=(FlexFrame&&) = delete;

    double getMinRXRateOversample(void) const override
    {
        return 2.0;
    }

    double getMinTXRateOversample(void) const override
    {
        return 2.0;
    }

    virtual size_t getModulatedSize(const TXParams &params, size_t n) override;

    std::unique_ptr<PHY::Demodulator> mkDemodulator(void) override;

    std::unique_ptr<PHY::Modulator> mkModulator(void) override;
};
