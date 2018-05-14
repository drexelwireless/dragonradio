#include "Logger.hh"
#include "SlottedMAC.hh"

SlottedMAC::SlottedMAC(std::shared_ptr<USRP> usrp,
                       std::shared_ptr<PHY> phy,
                       std::shared_ptr<PacketModulator> modulator,
                       std::shared_ptr<PacketDemodulator> demodulator,
                       double slot_size,
                       double guard_size)
  : usrp_(usrp),
    phy_(phy),
    modulator_(modulator),
    demodulator_(demodulator),
    slot_size_(slot_size),
    guard_size_(guard_size)
{
}

SlottedMAC::~SlottedMAC()
{
}

double SlottedMAC::getSlotSize(void)
{
    return slot_size_;
}

void SlottedMAC::setSlotSize(double t)
{
    slot_size_ = t;
    reconfigure();
}

double SlottedMAC::getGuardSize(void)
{
    return guard_size_;
}

void SlottedMAC::setGuardSize(double t)
{
    guard_size_ = t;
    reconfigure();
}

void SlottedMAC::txSlot(Clock::time_point when, size_t maxSamples)
{
    std::list<std::shared_ptr<IQBuf>>     txBuf;
    std::list<std::unique_ptr<ModPacket>> modBuf;

    modulator_->pop(modBuf, maxSamples);

    if (!modBuf.empty()) {
        for (auto it = modBuf.begin(); it != modBuf.end(); ++it) {
            if (logger) {
                Header hdr;

                hdr.pkt_id = (*it)->pkt->pkt_id;
                hdr.src = (*it)->pkt->src;
                hdr.dest = (*it)->pkt->dest;

                logger->logSend(when, hdr, (*it)->samples);
            }

            txBuf.emplace_back(std::move((*it)->samples));
        }

        usrp_->burstTX(when, txBuf);
    }
}
