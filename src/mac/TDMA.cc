#include <uhd/utils/thread_priority.hpp>

#include "Clock.hh"
#include "Logger.hh"
#include "USRP.hh"
#include "mac/TDMA.hh"

TDMA::TDMA(std::shared_ptr<USRP> usrp,
           std::shared_ptr<PHY> phy,
           std::shared_ptr<PacketModulator> modulator,
           std::shared_ptr<PacketDemodulator> demodulator,
           double bandwidth,
           size_t nslots,
           double slot_size,
           double guard_size)
  : _usrp(usrp),
    _phy(phy),
    _modulator(modulator),
    _demodulator(demodulator),
    _bandwidth(bandwidth),
    _slot_size(slot_size),
    _guard_size(guard_size),
    _done(false)
{
    _slots.resize(nslots, false);

    _rx_rate = _bandwidth*phy->getRxRateOversample();
    _tx_rate = _bandwidth*phy->getTxRateOversample();

    usrp->set_rx_rate(_rx_rate);
    usrp->set_tx_rate(_tx_rate);

    phy->setRxRate(_rx_rate);
    phy->setTxRate(_tx_rate);

    if (logger) {
        logger->setAttribute("tx_bandwidth", _tx_rate);
        logger->setAttribute("rx_bandwidth", _rx_rate);
    }

    _rxThread = std::thread(&TDMA::rxWorker, this);
    _txThread = std::thread(&TDMA::txWorker, this);

    reconfigure();
}

TDMA::~TDMA()
{
}

size_t TDMA::getNumSlots(void)
{
    return _slots.size();
}

void TDMA::setNumSlots(size_t n)
{
    _slots.resize(n, false);
    reconfigure();
}

double TDMA::getSlotSize(void)
{
    return _slot_size;
}

void TDMA::setSlotSize(double t)
{
    _slot_size = t;
    reconfigure();
}

double TDMA::getGuardSize(void)
{
    return _guard_size;
}

void TDMA::setGuardSize(double t)
{
    _guard_size = t;
    reconfigure();
}

void TDMA::addSlot(size_t i)
{
    _slots[i] = true;
}

void TDMA::removeSlot(size_t i)
{
    _slots[i] = false;
}

void TDMA::stop(void)
{
    _done = true;

    if (_rxThread.joinable())
        _rxThread.join();

    if (_txThread.joinable())
        _txThread.join();
}

void TDMA::rxWorker(void)
{
    Clock::time_point t_now;        // Current time
    Clock::time_point t_cur_slot;   // Time at which current slot starts
    Clock::time_point t_next_slot;  // Time at which next slot starts
    double            t_slot_pos;   // Offset into the current slot (sec)
    size_t            slot_samps;   // Number of samples in a slot
    int               slot;         // Curent slot index in the frame
    double            txRate;       // TX rate in Hz

    uhd::set_thread_priority_safe();

    txRate = _usrp->get_rx_rate();
    slot_samps = txRate * _slot_size;

    while (!_done) {
        // Set up streaming starting at *next* slot
        t_now = Clock::now();
        t_slot_pos = fmod(t_now.get_real_secs(), _slot_size);
        t_next_slot = t_now + _slot_size - t_slot_pos;
        slot = fmod(t_now.get_real_secs(), _frame_size) / _slot_size;

        _usrp->startRXStream(t_next_slot);

        while (!_done) {
            // Update times
            t_now = Clock::now();
            t_cur_slot = t_next_slot;
            t_next_slot += _slot_size;

            // Read samples for current slot
            auto curSlot = std::make_shared<IQBuf>(slot_samps + _usrp->getMaxRXSamps());

            _demodulator->push(curSlot);

            _usrp->burstRX(t_cur_slot, slot_samps, *curSlot);

            // Move to the next slot
            ++slot;
        }

        _usrp->stopRXStream();
    }
}

void TDMA::txWorker(void)
{
    Clock::time_point t_now;       // Current time
    Clock::time_point t_send_slot; // Time at which *our* slot starts
    size_t            slot_samps;  // Number of samples to send in a slot

    uhd::set_thread_priority_safe();

    slot_samps = _usrp->get_tx_rate()*(_slot_size - _guard_size);

    _modulator->setWatermark(slot_samps);

    while (!_done) {
        // Figure out when our next send slot is.
        t_now = Clock::now();

        for (;;) {
            t_send_slot = findNextSlot(t_now);
            if (t_send_slot > t_now)
                break;

            t_now = t_send_slot;
            printf("MISS\n");
        }

        // Schedule transmission for start of our slot
        txSlot(t_send_slot, slot_samps);

        // Sleep until the beginning of the guard interval before our next TX
        // slot
        Clock::time_point t_sleep;

        t_now = Clock::now();
        t_sleep = findNextSlot(t_now) - t_now - _guard_size;

        if (t_sleep > 0.0) {
            struct timespec ts;

            ts.tv_sec = t_sleep.get_full_secs();
            ts.tv_nsec = t_sleep.get_frac_secs()*1e9;

            if (nanosleep(&ts, NULL) < 0)
                perror("txWorker: slumber interrupted");
        }
    }
}

Clock::time_point TDMA::findNextSlot(Clock::time_point t)
{
    double t_secs = t.get_real_secs(); // Time t in seconds
    double t_slot_pos;                 // Offset into the current slot (sec)
    size_t cur_slot;                   // Current slot index
    size_t tx_slot;                    // Slots before we can TX

    t_slot_pos = fmod(t_secs, _slot_size);
    cur_slot = fmod(t_secs, _frame_size) / _slot_size;

    for (tx_slot = 1; !_slots[(cur_slot + tx_slot) % _slots.size()]; ++tx_slot)
        ;

    return (t - t_slot_pos) + tx_slot*_slot_size;
}

void TDMA::txSlot(Clock::time_point when, size_t maxSamples)
{
    std::list<std::shared_ptr<IQBuf>>     txBuf;
    std::list<std::unique_ptr<ModPacket>> modBuf;

    _modulator->pop(modBuf, maxSamples);

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

        _usrp->burstTX(when, txBuf);
    }
}

void TDMA::reconfigure(void)
{
    _frame_size = _slot_size*_slots.size();
    _demodulator->setDemodParameters(0.5*_guard_size*_rx_rate,
                                     (_slot_size - 0.5*_guard_size)*_tx_rate);
}
