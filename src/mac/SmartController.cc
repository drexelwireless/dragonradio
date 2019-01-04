#include "Logger.hh"
#include "RadioConfig.hh"
#include "mac/SmartController.hh"

#define DEBUG 0

#if DEBUG
#define dprintf(...) logEvent(__VA_ARGS__)
#else /* !dprintf */
#define dprintf(...)
#endif /* !DEBUG */

/** @brief Amount of time we wait for a regular packet to have a SACK attached.
 */
const double kSACKDelay = 0.050;

void applyTXParams(NetPacket &pkt, TXParams *p, float g)
{
    pkt.tx_params = p;
    pkt.g = p->g_0dBFS.getValue() * g;
}

void SendWindow::Entry::operator()()
{
    sendw.controller.retransmitOnTimeout(*this);
}

void RecvWindow::operator()()
{
    std::lock_guard<spinlock_mutex> lock(this->mutex);

    if (timer_for_ack) {
        controller.ack(*this);
    } else {
        need_selective_ack = true;
        timer_for_ack = true;

        dprintf("ARQ: starting full ACK timer: node=%u",
            (unsigned) node.id);
        controller.timer_queue_.run_in(*this, kSACKDelay);
    }
}

SmartController::SmartController(std::shared_ptr<Net> net,
                                 std::shared_ptr<PHY> phy,
                                 Seq::uint_type max_sendwin,
                                 Seq::uint_type recvwin,
                                 unsigned mcsidx_init,
                                 double mcsidx_up_per_threshold,
                                 double mcsidx_down_per_threshold,
                                 double mcsidx_alpha,
                                 double mcsidx_prob_floor)
  : Controller(net)
  , phy_(phy)
  , mac_(nullptr)
  , netq_(nullptr)
  , max_sendwin_(max_sendwin)
  , recvwin_(recvwin)
  , slot_size_(0)
  , mcsidx_init_(std::min(mcsidx_init, (unsigned) net_->tx_params.size() - 1))
  , mcsidx_up_per_threshold_(mcsidx_up_per_threshold)
  , mcsidx_down_per_threshold_(mcsidx_down_per_threshold)
  , mcsidx_alpha_(mcsidx_alpha)
  , mcsidx_prob_floor_(mcsidx_prob_floor)
  , explicit_nak_win_(0)
  , explicit_nak_win_duration_(0.0)
  , selective_ack_(false)
  , selective_ack_feedback_delay_(0.0)
  , max_retransmissions_({})
  , enforce_ordering_(false)
  , mcu_(0)
  , gen_(std::random_device()())
  , dist_(0, 1.0)
{
    timer_queue_.start();
}

SmartController::~SmartController()
{
    timer_queue_.stop();
}

bool SmartController::pull(std::shared_ptr<NetPacket>& pkt)
{
get_packet:
    // Get a packet to send. We look for a packet on our internal queue first.
    if (!getPacket(pkt))
        return false;

    // Handle broadcast packets
    if (pkt->isFlagSet(kBroadcast)) {
        applyTXParams(*pkt, &broadcast_tx_params, broadcast_gain.getLinearGain());

        return true;
    }

    // Get node ID of destination
    NodeId nexthop = pkt->nexthop;

    // If we have received a packet from the destination, add an ACK.
    RecvWindow *recvwptr = maybeGetReceiveWindow(nexthop);

    if (recvwptr) {
        RecvWindow                      &recvw = *recvwptr;
        ExtendedHeader                  &ehdr = pkt->getExtendedHeader();
        std::lock_guard<spinlock_mutex> lock(recvw.mutex);

        // The packet we are ACK'ing had better be no more than 1 more than the
        // max sequence number we've received.
        assert(recvw.ack <= recvw.max + 1);

        pkt->setFlag(kACK);
        ehdr.ack = recvw.ack;

#if DEBUG
        if (pkt->data_len == 0)
            dprintf("ARQ: send delayed ack: node=%u; ack=%u",
                (unsigned) nexthop,
                (unsigned) recvw.ack);
        else
            dprintf("ARQ: send ack: node=%u; ack=%u",
                (unsigned) nexthop,
                (unsigned) recvw.ack);
#endif

        // Append selective ACK if needed
        if (recvw.need_selective_ack)
            appendCtrlACK(recvw, pkt);
    } else if (pkt->data_len != 0)
        dprintf("ARQ: send: node=%u; seq=%u",
            (unsigned) nexthop,
            (unsigned) pkt->seq);

    // Update our send window if this packet has data
    if (pkt->data_len != 0) {
        SendWindow                      &sendw = getSendWindow(nexthop);
        Node                            &dest = (*net_)[nexthop];
        std::lock_guard<spinlock_mutex> lock(sendw.mutex);
        Seq                             unack = sendw.unack.load(std::memory_order_acquire);
        Seq                             max = sendw.max.load(std::memory_order_acquire);

        // It is possible that the send window shifts after we pull a packet
        // but before we get to this point. For example, an ACK could be
        // received in between the time we release the lock on the receive
        // window and this point. If that happens, we get another packet
        if (pkt->seq < unack)
            goto get_packet;

        // This asserts that the sequence number of the packet we are sending is
        // in our send window.
        assert(pkt->seq >= unack && pkt->seq < unack + sendw.win);

        // Save the packet in our send window.
        sendw[pkt->seq] = pkt;
        sendw[pkt->seq].timestamp = Clock::now();
        sendw[pkt->seq].mcsidx = sendw.mcsidx;

        // If this packet is a retransmission, increment the retransmission
        // count, otherwise set it to 0.
        if (pkt->isInternalFlagSet(kRetransmission))
            ++sendw[pkt->seq].nretrans;
        else
            sendw[pkt->seq].nretrans = 0;

        // Update send window metrics
        if (pkt->seq > max)
            sendw.max.store(pkt->seq, std::memory_order_release);

        // Apply TX params
        applyTXParams(*pkt, dest.tx_params, dest.g);
    } else
        // Apply broadcast TX params
        applyTXParams(*pkt, &broadcast_tx_params, ack_gain.getLinearGain());

    return true;
}

void SmartController::received(std::shared_ptr<RadioPacket>&& pkt)
{
    // Skip packets with invalid header
    if (pkt->isInternalFlagSet(kInvalidHeader))
        return;

    // Skip packets that aren't for us
    if (!pkt->isFlagSet(kBroadcast) && pkt->nexthop != net_->getMyNodeId())
        return;

    // Add the sending node if we haven't seen it before
    if (!net_->contains(pkt->curhop))
        net_->addNode(pkt->curhop);

    // Get node ID of source
    NodeId prevhop = pkt->curhop;

    // Immediately NAK data packets with a bad payload if they contain data.
    // We can't do anything else with the packet.
    if (pkt->isInternalFlagSet(kInvalidPayload)) {
        if (pkt->data_len != 0) {
            RecvWindow                      &recvw = getReceiveWindow(prevhop, pkt->seq, pkt->isFlagSet(kSYN));
            //std::lock_guard<spinlock_mutex> lock(recvw.mutex);

            // Update the max seq number we've received
            if (pkt->seq > recvw.max) {
                recvw.max = pkt->seq;
                recvw.max_timestamp = pkt->timestamp;
            }

            // Send a NAK
            nak(pkt->curhop, pkt->seq);
        }

        return;
    }

    // Get a reference to the sending node
    Node &node = (*net_)[pkt->curhop];

    // Process control info
    if (pkt->isFlagSet(kControl)) {
        handleCtrlHello(node, pkt);
        handleCtrlTimestampEchos(node, pkt);
    }

    // Handle broadcast packets
    if (pkt->isFlagSet(kBroadcast)) {
        // Resize the packet to truncate non-data bytes
        pkt->resize(sizeof(ExtendedHeader) + pkt->data_len);

        // Send the packet along if it has data
        if (pkt->data_len != 0)
            radio_out.push(std::move(pkt));

        return;
    }

    // If this packet was not destined for us, we are done
    if (pkt->nexthop != net_->getMyNodeId())
        return;

    // Get the extended header
    ExtendedHeader &ehdr = pkt->getExtendedHeader();

    // Handle ACK/NAK
    SendWindow *sendwptr = maybeGetSendWindow(prevhop);

    if (sendwptr) {
        SendWindow                      &sendw = *sendwptr;
        std::lock_guard<spinlock_mutex> lock(sendw.mutex);
        Seq                             unack = sendw.unack.load(std::memory_order_acquire);
        Seq                             max = sendw.max.load(std::memory_order_acquire);
        Clock::time_point               tfeedback = Clock::now() - selective_ack_feedback_delay_;
        std::optional<Seq>              nak;

        // Handle any NAK
        nak = handleNAK(sendw, pkt);

        // If we saw a NAK, look at feedback at least up to the sequence number
        // that was NAK'ed. We add a tiny amount of slop, 0.001 sec, to make
        // sure we *include* the NAK'ed packet.
        if (nak)
            tfeedback = std::max(tfeedback, sendw[*nak].timestamp + 0.001);

        // Handle ACK
        if (pkt->isFlagSet(kACK)) {
            if (ehdr.ack > unack) {
                dprintf("ARQ: ack: node=%u; seq=[%u,%u)",
                    (unsigned) node.id,
                    (unsigned) unack,
                    (unsigned) ehdr.ack);

                // Don't assert this because the sender could crash us with bad
                // data! We protected against this case in the following loop.
                //assert(ehdr.ack <= sendw.max + 1);

                // Move the send window along. It's possible the sender sends an
                // ACK for something we haven't sent, so we must guard against
                // that here as well
                for (; unack < ehdr.ack && unack <= max; ++unack) {
                    // Handle the ACK
                    handleACK(sendw, unack);

                    // Update our packet error rate to reflect successful TX
                    if (unack >= sendw.per_end)
                        txSuccess(sendw.node);
                }

                // unack is the NEXT un-ACK'ed packet, i.e., the packet we  are
                // waiting to hear about next. Note that it is possible for the
                // sender to ACK a packet we've already decided was bad, e.g., a
                // retranmission, so we must be careful not to "rewind" the PER
                // window here by blindly setting sendw.per_end = unack without
                // the test.
                if (unack > sendw.per_end)
                    sendw.per_end = unack;
            }

            // Handle selective ACK. We do this *after* handling the ACK,
            // because a selective ACK tells us about packets *beyond* that
            // which was ACK'ed.
            handleSelectiveACK(sendw, pkt, tfeedback);

            // If the NAK is for a retransmitted packet, count it as a
            // transmission failure. We need to check for this case because a
            // NAK for a retransmitted packet will have already been counted
            // toward our PER the first time the packet was NAK'ed.
            if (nak) {
                SendWindow::Entry &entry = sendw[*nak];

                if (sendw.mcsidx >= entry.mcsidx && entry.nretrans > 0) {
                    txFailure(node);

                    logEvent("ARQ: txFailure nak of retransmission: node=%u; seq=%u; mcsidx=%u",
                        (unsigned) node.id,
                        (unsigned) *nak,
                        (unsigned) entry.mcsidx);
                }
            }

            // Update MCS based on new PER
            updateMCS(sendw);

            // Advance the send window. It is possible that packets immediately
            // after the packet that the sender just ACK'ed have timed out and
            // been dropped, so advanceSendWindow must look for dropped packets
            // and attempt to push the send window up towards max.
            advanceSendWindow(sendw, unack, max);
        }
    }

    // Resize the packet to truncate non-data bytes
    pkt->resize(sizeof(ExtendedHeader) + pkt->data_len);

    // If this packet doesn't contain any data, we are done
    if (pkt->data_len == 0) {
        dprintf("ARQ: recv: node=%u; ack=%u",
            (unsigned) prevhop,
            (unsigned) ehdr.ack);
        return;
    }

#if DEBUG
    if (pkt->isFlagSet(kACK))
        dprintf("ARQ: recv: node=%u; seq=%u; ack=%u",
            (unsigned) prevhop,
            (unsigned) pkt->seq,
            (unsigned) ehdr.ack);
    else
        dprintf("ARQ: recv: node=%u; seq=%u",
            (unsigned) prevhop,
            (unsigned) pkt->seq);
#endif

    // Fill our receive window
    RecvWindow                      &recvw = getReceiveWindow(prevhop, pkt->seq, pkt->isFlagSet(kSYN));
    std::lock_guard<spinlock_mutex> lock(recvw.mutex);

    // If this is a SYN packet, ACK immediately to open up the window.
    //
    // Otherwise, start the ACK timer if it is not already running. Even if this
    // is a duplicate packet, we need to send an ACK because the duplicate may
    // be a retransmission, i.e., our previous ACK could have been lost.
    if (pkt->isFlagSet(kSYN))
        ack(recvw);
    else
        startSACKTimer(recvw);

    // Drop this packet if it is before our receive window
    if (pkt->seq < recvw.ack) {
        dprintf("ARQ: recv OUTSIDE WINDOW (DUP): node=%u; seq=%u",
            (unsigned) prevhop,
            (unsigned) pkt->seq);
        return;
    }

    // If the packet is after our receive window, we need to advance the receive
    // window.
    if (pkt->seq >= recvw.ack + recvw.win) {
        logEvent("ARQ: recv OUTSIDE WINDOW (ADVANCE): node=%u; seq=%u",
            (unsigned) prevhop,
            (unsigned) pkt->seq);

        // We want to slide the window forward so pkt->seq is the new max
        // packet. We therefore need to "forget" all packets in our current
        // window with sequence numbers less than pkt->seq - recvw.win. It's
        // possible this number is greater than our max received sequence
        // number, so we must account for that as well!
        Seq new_ack = pkt->seq + 1 - recvw.win;
        Seq forget = new_ack > recvw.max ? recvw.max + 1 : new_ack;

        // Go ahead and deliver packets that will be left outside our window.
        for (auto seq = recvw.ack; seq < forget; ++seq) {
            RecvWindow::Entry &entry = recvw[seq];

            // Go ahead and deliver the packet
            if (entry.pkt && !entry.delivered)
                radio_out.push(std::move(entry.pkt));

            // Release the packet
            entry.reset();
        }

	    recvw.ack = new_ack;
    } else if (recvw[pkt->seq].received) {
        // Drop this packet if we have already received it
        dprintf("ARQ: recv DUP: node=%u; seq=%u",
            (unsigned) prevhop,
            (unsigned) pkt->seq);
        return;
    }

    // Update the max seq number we've received
    if (pkt->seq > recvw.max) {
        recvw.max = pkt->seq;
        recvw.max_timestamp = pkt->timestamp;
    }

    // If this is the next packet we expected, send it now and update the
    // receive window
    if (pkt->seq == recvw.ack) {
        recvw.ack++;
        radio_out.push(std::move(pkt));
    } else if (!enforce_ordering_ && !pkt->isTCP()) {
        // If this is not a TCP packet, insert it into our receive window, but
        // also go ahead and send it.
        radio_out.push(std::move(pkt));
        recvw[pkt->seq].alreadyDelivered();
    } else {
        // Insert the packet into our receive window
        recvw[pkt->seq] = std::move(pkt);
    }

    // Now drain the receive window until we reach a hole
    for (auto seq = recvw.ack; seq <= recvw.max; ++seq) {
        RecvWindow::Entry &entry = recvw[seq];

        if (!entry.received)
            break;

        if (!entry.delivered)
            radio_out.push(std::move(entry.pkt));

        entry.reset();
        ++recvw.ack;
    }
}

void SmartController::transmitted(std::shared_ptr<NetPacket>& pkt)
{
    if (!pkt->isFlagSet(kBroadcast) && pkt->data_len != 0) {
        SendWindow                      &sendw = getSendWindow(pkt->nexthop);
        std::lock_guard<spinlock_mutex> lock(sendw.mutex);

        // Start the retransmit timer if it is not already running.
        startRetransmissionTimer(sendw[pkt->seq]);
    }

    // Cancel the selective ACK timer when we actually have sent a selective ACK
    if (pkt->isInternalFlagSet(kHasSelectiveACK)) {
        RecvWindow                      &recvw = *maybeGetReceiveWindow(pkt->nexthop);
        std::lock_guard<spinlock_mutex> lock(recvw.mutex);

        timer_queue_.cancel(recvw);
    }
}

void SmartController::retransmitOnTimeout(SendWindow::Entry &entry)
{
    SendWindow                      &sendw = entry.sendw;
    std::lock_guard<spinlock_mutex> lock(sendw.mutex);

    if (!entry.pkt) {
        logEvent("AMC: attempted to retransmit ACK'ed packet on timeout: node=%u",
            (unsigned) sendw.node.id);
        return;
    }

    // Record the packet error
    if (sendw.mcsidx >= entry.mcsidx) {
        txFailure(sendw.node);

        logEvent("AMC: txFailure retransmission: node=%u; seq=%u; mcsidx=%u; short per=%f",
            (unsigned) sendw.node.id,
            (unsigned) entry.pkt->seq,
            (unsigned) entry.mcsidx,
            sendw.node.short_per.getValue());

        updateMCS(sendw);
    }

    // Actually retransmit (or drop) the packet
    retransmitOrDrop(entry);
}

void SmartController::ack(RecvWindow &recvw)
{
    if (!netq_)
        return;

    // Create an ACK-only packet. Why don't we set the ACK field here!? Because
    // it will be filled out when the packet flows back through the controller
    // on its way out the radio. We are just providing the opportunity for an
    // ACK by injecting a packet without a data payload at the head of the
    // queue.
    auto pkt = std::make_shared<NetPacket>(sizeof(ExtendedHeader));

    pkt->curhop = net_->getMyNodeId();
    pkt->nexthop = recvw.node.id;
    pkt->flags = 0;
    pkt->seq = 0;
    pkt->data_len = 0;
    pkt->src = net_->getMyNodeId();
    pkt->dest = recvw.node.id;

    // Append selective ACK control messages
    appendCtrlACK(recvw, pkt);

    netq_->push_hi_front(std::move(pkt));
}

void SmartController::nak(NodeId node_id, Seq seq)
{
    if (!netq_)
        return;

    // Get the receive window
    RecvWindow *recvwptr = maybeGetReceiveWindow(node_id);

    if (!recvwptr)
        return;

    RecvWindow                      &recvw = *recvwptr;
    std::lock_guard<spinlock_mutex> lock(recvw.mutex);

    // If we have a zero-sized NAK window, don't send any NAK's.
    if (recvw.explicit_nak_win.size() == 0)
        return;

    // Limit number of explicit NAK's we send
    auto now = MonoClock::now();

    if (recvw.explicit_nak_win[recvw.explicit_nak_idx] + explicit_nak_win_duration_ > now)
        return;

    recvw.explicit_nak_win[recvw.explicit_nak_idx] = now;
    recvw.explicit_nak_idx = (recvw.explicit_nak_idx + 1) % explicit_nak_win_;

    // Send the explicit NAK
    logEvent("ARQ: send nak: node=%u; nak=%u",
        (unsigned) node_id,
        (unsigned) seq);

    // Create an ACK-only packet. Why don't we set the ACK field here!? Because
    // it will be filled out when the packet flows back through the controller
    // on its way out the radio. We are just providing the opportunity for an
    // ACK by injecting a packet without a data payload at the head of the
    // queue.
    auto pkt = std::make_shared<NetPacket>(sizeof(ExtendedHeader));

    pkt->curhop = net_->getMyNodeId();
    pkt->nexthop = node_id;
    pkt->flags = 0;
    pkt->seq = 0;
    pkt->data_len = 0;
    pkt->src = net_->getMyNodeId();
    pkt->dest = node_id;

    // Append NAK control message
    pkt->appendNak(seq);

    // Append selective ACK control messages
    appendCtrlACK(recvw, pkt);

    netq_->push_hi_front(std::move(pkt));
}

void SmartController::broadcastHello(void)
{
    if (!netq_)
        return;

    dprintf("ARQ: broadcast HELLO");

    auto pkt = std::make_shared<NetPacket>(sizeof(ExtendedHeader));

    pkt->curhop = net_->getMyNodeId();
    pkt->nexthop = 0;
    pkt->flags = 0;
    pkt->seq = 0;
    pkt->data_len = 0;
    pkt->src = net_->getMyNodeId();
    pkt->dest = 0;

    pkt->setFlag(kBroadcast);

    // Append hello message
    ControlMsg::Hello msg;
    Node              &me = net_->me();

    msg.is_gateway = me.is_gateway;

    pkt->appendHello(msg);

    // Echo most recently heard timestamps if we are the time master
    std::optional<NodeId> time_master = net_->getTimeMaster();

    if (time_master && *time_master == net_->getMyNodeId()) {
        for (auto it = net_->begin(); it != net_->end(); ++it) {
            auto last_timestamp = it->second.timestamps.rbegin();

            if (it->first != net_->getMyNodeId() && last_timestamp != it->second.timestamps.rend()) {
                logEvent("TIMESYNC: Echoing timestamp: node=%u; t_sent=%f; t_recv=%f",
                    (unsigned) it->first,
                    (double) last_timestamp->first.get_real_secs(),
                    (double) last_timestamp->second.get_real_secs());

                pkt->appendTimestampEcho(it->first,
                                         last_timestamp->first,
                                         last_timestamp->second);
            }
        }
    }

    // Send a timestamped HELLO
    if (mac_) {
        pkt->tx_params = &broadcast_tx_params;
        pkt->g = broadcast_tx_params.g_0dBFS.getValue();
        mac_->sendTimestampedPacket(Clock::now() + rc.timestamp_delay, std::move(pkt));
    } else
        netq_->push_hi_front(std::move(pkt));
}

void SmartController::resetMCSTransitionProbabilities(void)
{
    for (auto it = send_.begin(); it != send_.end(); ++it) {
        SendWindow                      &sendw = it->second;
        std::lock_guard<spinlock_mutex> lock(sendw.mutex);

        std::vector<double>&v = sendw.mcsidx_prob;

        std::fill(v.begin(), v.end(), 1.0);
    }
}

void SmartController::retransmitOrDrop(SendWindow::Entry &entry)
{
    assert(entry.pkt);

    // We drop a packet if:
    // 1) It is NOT a SYN packet, because in that case it is needed to initiate
    //    a connection. We always retrasmit SYN packets.
    // 2) It has exceeded the maximum number of allowed retransmissions.
    // 3) OR it has passed its deadline.
    if (!entry.pkt->isFlagSet(kSYN) &&
        (   (max_retransmissions_ && entry.nretrans >= *max_retransmissions_)
         || entry.pkt->deadlinePassed(MonoClock::now())))
        drop(entry);
    else
        retransmit(entry);
}

/** NOTE: The lock on the send window to which entry belongs MUST be held before
 * calling retransmit.
 */
void SmartController::retransmit(SendWindow::Entry &entry)
{
    if (!entry.pkt) {
        logEvent("AMC: attempted to retransmit ACK'ed packet");
        return;
    }

    logEvent("ARQ: retransmit: node=%u; seq=%u; mcsidx=%u",
        (unsigned) entry.pkt->nexthop,
        (unsigned) entry.pkt->seq,
        (unsigned) entry.mcsidx);

    // The retransmit timer will be restarted when the packet is actually sent,
    // so don't re-start it here! Doing so can lead to a cascade of retransmit
    // timers firing when there are a large number of outstanding transmissions
    // and we suddenly need to ratchet down the MCS. Instead, we cancel the
    // timer here and allow it to be restarted upon transmission. We need to
    // cancel the timer because retransmission could be triggered but something
    // OTHER than a retransmission time-out, e.g., an explicit NAK, and if we
    // don't cancel it, we can end up retransmitting the same packet twice,
    // e.g., once due to the explicit NAK, and again due to a retransmission
    // timeout.
    timer_queue_.cancel(entry);

    // We need to make an explicit new reference to the shared_ptr because push
    // takes ownership of its argument.
    std::shared_ptr<NetPacket> pkt = entry;

    // Clear any control information in the packet
    pkt->clearControl();

    // Mark the packet as a retransmission
    pkt->setInternalFlag(kRetransmission);

    // Put the packet on the high-priority network queue. The ACK and MCS will
    // be set properly upon retransmission.
    if (netq_)
        netq_->push_hi_back(std::move(pkt));
}

void SmartController::drop(SendWindow::Entry &entry)
{
    SendWindow &sendw = entry.sendw;

    // If the packet has already been ACK'd, forget it
    if (!entry)
        return;

    // Drop the packet
    logEvent("ARQ: dropping packet: node=%u; seq=%u",
        (unsigned) sendw.node.id,
        (unsigned) entry.pkt->seq);

    // Cancel retransmission timer
    timer_queue_.cancel(entry);

    // Release the packet
    entry.reset();

    // Advance send window if we can
    Seq unack = sendw.unack.load(std::memory_order_acquire);
    Seq max = sendw.max.load(std::memory_order_acquire);

    advanceSendWindow(sendw, unack, max);
}

void SmartController::advanceSendWindow(SendWindow &sendw, Seq unack, Seq max)
{
    // Advance send window if we can
    while (unack <= max && !sendw[unack])
        ++unack;

    // Increase the send window. We really only need to do this after the
    // initial ACK, but it doesn't hurt to do it every time...
    sendw.win = sendw.maxwin;

    // Indicate that this node's send window is now open
    if (sendw.node.seq < unack + sendw.win)
        netq_->setSendWindowStatus(sendw.node.id, true);

    // Update unack
    sendw.unack.store(unack, std::memory_order_release);
}

void SmartController::startRetransmissionTimer(SendWindow::Entry &entry)
{
    // Start the retransmit timer if the packet has not already been ACK'ed and
    // the timer is not already running
    if (entry.pkt && !timer_queue_.running(entry)) {
        dprintf("ARQ: starting retransmission timer: node=%u; seq=%u",
            (unsigned) entry.sendw.node.id,
            (unsigned) entry.pkt->seq);
        timer_queue_.run_in(entry, entry.sendw.node.retransmission_delay);
    }
}

void SmartController::startSACKTimer(RecvWindow &recvw)
{
    // Start the selective ACK timer if it is not already running.
    if (!timer_queue_.running(recvw)) {
        dprintf("ARQ: starting SACK timer: node=%u",
            (unsigned) recvw.node.id);

        recvw.need_selective_ack = false;
        recvw.timer_for_ack = false;
        timer_queue_.run_in(recvw, recvw.node.ack_delay - kSACKDelay);
    }
}

void SmartController::handleCtrlHello(Node &node, std::shared_ptr<RadioPacket>& pkt)
{
    for(auto it = pkt->begin(); it != pkt->end(); ++it) {
        switch (it->type) {
            case ControlMsg::Type::kHello:
            {
                node.is_gateway = it->hello.is_gateway;

                dprintf("ARQ: HELLO: node=%u",
                    (unsigned) pkt->curhop);

                logEvent("ARQ: Discovered neighbor: node=%u; gateway=%s",
                    (unsigned) pkt->curhop,
                    node.is_gateway ? "true" : "false");
            }
            break;

            case ControlMsg::Type::kTimestamp:
            {
                MonoClock::time_point t_sent;
                MonoClock::time_point t_recv;

                t_sent = it->timestamp.t_sent.to_mono_time();
                t_recv = pkt->timestamp;

                node.timestamps.emplace_back(std::make_pair(t_sent, t_recv));

                logEvent("TIMESYNC: Timestamp: node=%u; t_sent=%f; t_recv=%f",
                    (unsigned) pkt->curhop,
                    (double) t_sent.get_real_secs(),
                    (double) t_recv.get_real_secs());
            }
            break;

            default:
                break;
        }
    }
}

void SmartController::handleCtrlTimestampEchos(Node &node, std::shared_ptr<RadioPacket>& pkt)
{
    // If the transmitter is the time master, record our echoed timestamps.
    std::optional<NodeId> time_master = net_->getTimeMaster();

    if (node.id != net_->getMyNodeId() && time_master && node.id == *time_master) {
        for(auto it = pkt->begin(); it != pkt->end(); ++it) {
            switch (it->type) {
                case ControlMsg::Type::kTimestampEcho:
                {
                    if (it->timestamp_echo.node == net_->getMyNodeId()) {
                        MonoClock::time_point t_sent;
                        MonoClock::time_point t_recv;

                        t_sent = it->timestamp_echo.t_sent.to_mono_time();
                        t_recv = it->timestamp_echo.t_recv.to_mono_time();

                        echoed_timestamps_.emplace_back(std::make_pair(t_sent, t_recv));

                        logEvent("TIMESYNC: Timestamp echo: node=%u; t_sent=%f; t_recv=%f",
                            (unsigned) pkt->curhop,
                            (double) t_sent.get_real_secs(),
                            (double) t_recv.get_real_secs());
                    }
                }
                break;

                default:
                    break;
            }
        }
    }
}

inline bool apendSelectiveACK(size_t mtu,
                              RecvWindow &recvw,
                              std::shared_ptr<NetPacket>& pkt,
                              Seq begin,
                              Seq end)
{
    if (pkt->size() + ctrlsize(ControlMsg::Type::kSelectiveAck) < mtu) {
        logEvent("ARQ: send selective ack: node=%u; seq=[%u, %u)",
            (unsigned) recvw.node.id,
            (unsigned) begin,
            (unsigned) end);
        pkt->appendSelectiveAck(begin, end);
        return true;
    } else {
        logEvent("ARQ: OUT OF SPACE for selective ack: node=%u; size=%lu",
            (unsigned) recvw.node.id,
            pkt->size());
        return false;
    }
}

void SmartController::appendCtrlACK(RecvWindow &recvw, std::shared_ptr<NetPacket>& pkt)
{
    if (!selective_ack_)
        return;

    bool in_run = false; // Are we in the middle of a run of ACK's?
    Seq  begin = recvw.ack;
    Seq  end = recvw.ack;

    // The ACK in the (extended) header will handle ACK'ing recvw.ack, so we
    // need to start looking for selective ACK's at recvw.ack + 1. Recall that
    // recvw.ack is the next sequence number we should ACK, meaning we have
    // successfully received (or given up) on all packets with sequence numbers
    // <= recvw.ack. In particular, this means that recvw.ack + 1 should NOT be
    // ACK'ed, because otherwise recvw.ack would be equal to recvw.ack + 1!
    for (Seq seq = recvw.ack + 1; seq <= recvw.max; ++seq) {
        if (recvw[seq].received) {
            if (!in_run) {
                in_run = true;
                begin = seq;
            }

            end = seq;
        } else {
            if (in_run) {
                if (!apendSelectiveACK(rc.mtu + mcu_, recvw, pkt, begin, end + 1))
                    return;

                in_run = false;
            }
        }
    }

    // Close out any final run
    if (in_run) {
        if (!apendSelectiveACK(rc.mtu + mcu_, recvw, pkt, begin, end + 1))
            return;
    }

    // If we cannot ACK recvw.max, add an empty selective ACK range marking then
    // end up our received packets. This will inform the sender that the last
    // stretch of packets WAS NOT received.
    if (end < recvw.max) {
        if (!apendSelectiveACK(rc.mtu + mcu_, recvw, pkt, recvw.max+1, recvw.max+1))
            return;
    }

    // Mark this packet as containing a selective ACK
    pkt->setInternalFlag(kHasSelectiveACK);

    // We no longer need a selective ACK
    recvw.need_selective_ack = false;
}

void SmartController::handleACK(SendWindow &sendw, const Seq &seq)
{
    SendWindow::Entry &entry = sendw[seq];
    Seq               unack = sendw.unack.load(std::memory_order_acquire);

    // If this packet is outside our send window, we're done.
    if (seq < unack || seq >= unack + sendw.win) {
        logEvent("ARQ: ack for packet outside send window: node=%u; seq=%u; unack=%u; end=%u",
            (unsigned) sendw.node.id,
            (unsigned) seq,
            (unsigned) unack,
            (unsigned) unack + sendw.win);

        return;
    }

    // If this packet has already been ACK'ed, we're done.
    if (!entry.pkt) {
        dprintf("ARQ: ack for already ACK'ed packet: node=%u; seq=%u",
            (unsigned) sendw.node.id,
            (unsigned) seq);

        return;
    }

    // Cancel retransmission timer for ACK'ed packet
    timer_queue_.cancel(entry);

    // Release the packet since it's been ACK'ed
    entry.reset();
}

std::optional<Seq> SmartController::handleNAK(SendWindow &sendw,
                                              std::shared_ptr<RadioPacket>& pkt)
{
    std::optional<Seq> result;
    Seq                unack = sendw.unack.load(std::memory_order_acquire);

    for(auto it = pkt->begin(); it != pkt->end(); ++it) {
        switch (it->type) {
            case ControlMsg::Type::kNak:
            {
                SendWindow::Entry &entry = sendw[it->nak];

                // If this packet is outside our send window, ignore the NAK.
                if (it->nak < unack || it->nak >= unack + sendw.win || !entry.pkt) {
                    logEvent("ARQ: nak for packet outside send window: node=%u; seq=%u; unack=%u; end=%u",
                        (unsigned) sendw.node.id,
                        (unsigned) it->nak,
                        (unsigned) unack,
                        (unsigned) unack + sendw.win);
                // If this packet has already been ACK'ed, ignore the NAK.
                } else if (!entry.pkt) {
                    logEvent("ARQ: nak for already ACK'ed packet: node=%u; seq=%u",
                        (unsigned) sendw.node.id,
                        (unsigned) it->nak);
                } else {
                    // Log the NAK
                    logEvent("ARQ: nak: node=%u; seq=%u",
                        (unsigned) sendw.node.id,
                        (unsigned) it->nak);

                    result = it->nak;
                }
            }
            break;

            default:
                break;
        }
    }

    return result;
}

void SmartController::handleSelectiveACK(SendWindow &sendw,
                                         std::shared_ptr<RadioPacket>& pkt,
                                         Clock::time_point tfeedback)
{
    Node &node = sendw.node;
    Seq  unack = sendw.unack.load(std::memory_order_acquire);
    Seq  nextSeq = unack;
    bool sawACKRun = false;

    for(auto it = pkt->begin(); it != pkt->end(); ++it) {
        switch (it->type) {
            case ControlMsg::Type::kSelectiveAck:
            {
                if (!sawACKRun)
                    logEvent("ARQ: selective ack: node=%u; per_end=%u",
                        (unsigned) node.id,
                        (unsigned) sendw.per_end);

                // Record the gap between the last packet in the previous ACK
                // run and the first packet in this ACK run as failures.
                if (nextSeq < it->ack.begin) {
                    logEvent("ARQ: selective nak: node=%u; seq=[%u,%u)",
                        (unsigned) node.id,
                        (unsigned) nextSeq,
                        (unsigned) it->ack.begin);

                    for (Seq seq = nextSeq; seq < it->ack.begin; ++seq) {
                        if (seq >= sendw.per_end) {
                            sendw.per_end = seq + 1;

                            if (sendw[seq].timestamp < tfeedback && sendw[seq]) {
                                txFailure(node);

                                logEvent("ARQ: txFailure selective nak: node=%u; seq=%u",
                                    (unsigned) node.id,
                                    (unsigned) seq);

                                // Retransmit the NAK'ed packet
                                retransmit(sendw[seq]);
                            }
                        }
                    }
                }

                // Mark every packet in this ACK run as a success.
                logEvent("ARQ: selective ack: node=%u; seq=[%u,%u)",
                    (unsigned) node.id,
                    (unsigned) it->ack.begin,
                    (unsigned) it->ack.end);

                for (Seq seq = it->ack.begin; seq < it->ack.end; ++seq) {
                    // Handle the ACK
                    if (seq >= unack)
                        handleACK(sendw, seq);

                    // Update our packet error rate to reflect successful TX
                    if (seq >= sendw.per_end && sendw[seq].timestamp < tfeedback) {
                        txSuccess(node);
                        sendw.per_end = seq + 1;
                    }
                }

                // We've now handled at least one ACK run
                sawACKRun = true;
                nextSeq = it->ack.end;
            }
            break;

            default:
                break;
        }
    }
}

void SmartController::txSuccess(Node &node)
{
    node.short_per.update(0.0);
    node.long_per.update(0.0);
}

void SmartController::txFailure(Node &node)
{
    node.short_per.update(1.0);
    node.long_per.update(1.0);
}

void SmartController::updateMCS(SendWindow &sendw)
{
    Node          &node = sendw.node;
    double        short_per = node.short_per.getValue();
    double        long_per = node.long_per.getValue();
    static double prev_short_per = 0.0;
    static double prev_long_per = 0.0;

    if (short_per != prev_short_per || long_per != prev_long_per) {
        logEvent("AMC: updateMCS: node=%u; short per=%f (%u samples); long per=%f (%u samples)",
            node.id,
            short_per,
            node.short_per.getNSamples(),
            long_per,
            node.long_per.getNSamples());

        prev_short_per = short_per;
        prev_long_per = long_per;
    }

    // First for high PER, then test for low PER
    if (   node.short_per.getNSamples() >= node.short_per.getWindowSize()
        && short_per > mcsidx_down_per_threshold_
        && sendw.mcsidx > 0) {
        // Don't decrease MCS if largest possible packet won't fit in slot.
        if (getMaxPacketsPerSlot(net_->tx_params[sendw.mcsidx-1]) < 1)
            return;

        // Decrease the probability that we will transition to this MCS index
        sendw.mcsidx_prob[sendw.mcsidx] =
            std::max(sendw.mcsidx_prob[sendw.mcsidx]*mcsidx_alpha_,
                     mcsidx_prob_floor_);

        logEvent("AMC: Transition probability for MCS: node=%u; index=%u; prob=%f",
            node.id,
            (unsigned) sendw.mcsidx,
            sendw.mcsidx_prob[sendw.mcsidx]);

        // Move down one MCS
        moveDownMCS(sendw);
    } else if (   node.long_per.getNSamples() >= node.long_per.getWindowSize()
               && long_per < mcsidx_up_per_threshold_) {
        double old_prob = sendw.mcsidx_prob[sendw.mcsidx];

        // Set transition probability of current MCS index to 1.0 since we
        // successfully passed the long PER test
        sendw.mcsidx_prob[sendw.mcsidx] = 1.0;

        if (sendw.mcsidx_prob[sendw.mcsidx] != old_prob)
            logEvent("AMC: Transition probability for MCS: node=%u; index=%u; prob=%f",
                node.id,
                (unsigned) sendw.mcsidx,
                sendw.mcsidx_prob[sendw.mcsidx]);

        // Now we see if we can actually increase the MCS index. Not only must
        // there be a higher entry in the MCS table, but we must pass the
        // probabilistic transition test.
        if (   sendw.mcsidx < net_->tx_params.size() - 1
            && dist_(gen_) < sendw.mcsidx_prob[sendw.mcsidx+1]) {
            moveUpMCS(sendw);
        } else
            resetPEREstimates(sendw);
    }
}

void SmartController::moveDownMCS(SendWindow &sendw)
{
    Node &node = sendw.node;

    if (rc.verbose && ! rc.debug)
        fprintf(stderr, "Moving down modulation scheme\n");

    --sendw.mcsidx;
    sendw.per_end = node.seq;
    node.tx_params = &net_->tx_params[sendw.mcsidx];

    logEvent("AMC: Moving down modulation scheme: node=%u; short per=%f; swin=%lu; lwin=%lu",
        node.id,
        node.short_per.getValue(),
        node.short_per.getWindowSize(),
        node.long_per.getWindowSize());

    resetPEREstimates(sendw);

    logEvent("AMC: Moved down modulation scheme: node=%u; mcsidx=%u; fec0=%s; fec1=%s; ms=%s; unack=%u; init_seq=%u; swin=%lu; lwin=%lu",
        node.id,
        (unsigned) sendw.mcsidx,
        node.tx_params->mcs.fec0_name(),
        node.tx_params->mcs.fec1_name(),
        node.tx_params->mcs.ms_name(),
        (unsigned) sendw.unack.load(std::memory_order_release),
        (unsigned) sendw.per_end,
        node.short_per.getWindowSize(),
        node.long_per.getWindowSize());
}

void SmartController::moveUpMCS(SendWindow &sendw)
{
    Node &node = sendw.node;

    if (rc.verbose && ! rc.debug)
        fprintf(stderr, "Moving up modulation scheme\n");

    ++sendw.mcsidx;
    sendw.per_end = node.seq;
    node.tx_params = &net_->tx_params[sendw.mcsidx];

    logEvent("AMC: Moving up modulation scheme: node=%u; long per=%f; swin=%lu; lwin=%lu",
        node.id,
        node.long_per.getValue(),
        node.short_per.getWindowSize(),
        node.long_per.getWindowSize());

    resetPEREstimates(sendw);

    logEvent("AMC: Moved up modulation scheme: node=%u; mcsidx=%u; fec0=%s; fec1=%s; ms=%s; unack=%u; init_seq=%u; swin=%lu; lwin=%lu",
        node.id,
        (unsigned) sendw.mcsidx,
        node.tx_params->mcs.fec0_name(),
        node.tx_params->mcs.fec1_name(),
        node.tx_params->mcs.ms_name(),
        (unsigned) sendw.unack.load(std::memory_order_release),
        (unsigned) sendw.per_end,
        node.short_per.getWindowSize(),
        node.long_per.getWindowSize());
}

void SmartController::resetPEREstimates(SendWindow &sendw)
{
    double max_packets_per_slot = getMaxPacketsPerSlot(*(sendw.node.tx_params));

    sendw.node.short_per.setWindowSize(rc.amc_short_per_nslots*max_packets_per_slot);
    sendw.node.short_per.reset(0);

    sendw.node.long_per.setWindowSize(rc.amc_long_per_nslots*max_packets_per_slot);
    sendw.node.long_per.reset(0);
}

bool SmartController::getPacket(std::shared_ptr<NetPacket>& pkt)
{
    for (;;) {
        // Get a packet from the network
        if (!net_in.pull(pkt))
            return false;

        assert(pkt);

        // We can always send a broadcast packet
        if (pkt->isFlagSet(kBroadcast))
            return true;

        SendWindow                      &sendw = getSendWindow(pkt->nexthop);
        std::lock_guard<spinlock_mutex> lock(sendw.mutex);
        Seq                             unack = sendw.unack.load(std::memory_order_acquire);

        // If packet has no payload, we can always send it---it has control
        // information.
        if (pkt->data_len == 0)
            return true;

        // Set the packet sequence number if it doesn't yet have one.
        if (!pkt->isInternalFlagSet(kHasSeq)) {
            Node& nexthop = (*net_)[pkt->nexthop];

            // If we can fit this packet in our window, do so. Otherwise, we
            // log an error and drop the packet. We should never receive a
            // packet from the network queue that we can't send.
            if (nexthop.seq < unack + sendw.win) {
                pkt->seq = nexthop.seq++;
                pkt->setInternalFlag(kHasSeq);

                // If this is the first packet we are sending to the destination,
                // set its SYN flag
                if (sendw.new_window) {
                    pkt->setFlag(kSYN);
                    sendw.new_window = false;
                }

                // If the send window is closed, tell the network queue
                if (nexthop.seq >= unack + sendw.win)
                    netq_->setSendWindowStatus(nexthop.id, false);

                return true;
            } else {
                logEvent("ARQ: DROPPING DUE TO FULL WINDOW: node=%u",
                    (unsigned) pkt->nexthop);
            }
        } else {
            // If this packet comes before our window, drop it. It could have
            // snuck in as a retransmission just before the send window moved
            // forward. Try again!
            if (pkt->seq < unack)
                continue;

            // Otherwise it had better be in our window becasue we added it back
            // when our window expanded due to an ACK!
            assert(pkt->seq < unack + sendw.win);

            // See if this packet should be dropped. The network queue won't
            // drop a packet with a sequence number, because we need to drop a
            // packet with a sequence number in the controller to ensure the
            // send window is properly adjusted.
            if (pkt->shouldDrop(MonoClock::now())) {
                drop(sendw[pkt->seq]);
                pkt.reset();
                continue;
            }


            return true;
        }
    }
}

SendWindow *SmartController::maybeGetSendWindow(NodeId node_id)
{
    std::lock_guard<spinlock_mutex> lock(send_mutex_);
    auto                            it = send_.find(node_id);

    if (it != send_.end())
        return &(it->second);
    else
        return nullptr;
}

SendWindow &SmartController::getSendWindow(NodeId node_id)
{
    std::lock_guard<spinlock_mutex> lock(send_mutex_);
    auto                            it = send_.find(node_id);

    if (it != send_.end())
        return it->second;
    else {
        Node       &dest = (*net_)[node_id];
        SendWindow &sendw = send_.emplace(std::piecewise_construct,
                                          std::forward_as_tuple(node_id),
                                          std::forward_as_tuple(dest, *this, max_sendwin_)).first->second;

        sendw.mcsidx = mcsidx_init_;
        sendw.mcsidx_prob.resize(net_->tx_params.size(), 1.0);
        sendw.per_end = dest.seq;

        dest.tx_params = &net_->tx_params[mcsidx_init_];

        while (getMaxPacketsPerSlot(*dest.tx_params) == 0) {
            ++sendw.mcsidx;
            dest.tx_params = &net_->tx_params[sendw.mcsidx];
        }

        resetPEREstimates(sendw);

        return sendw;
    }
}

RecvWindow *SmartController::maybeGetReceiveWindow(NodeId node_id)
{
    std::lock_guard<spinlock_mutex> lock(recv_mutex_);
    auto                            it = recv_.find(node_id);

    if (it != recv_.end())
        return &(it->second);
    else
        return nullptr;
}

RecvWindow &SmartController::getReceiveWindow(NodeId node_id, Seq seq, bool isSYN)
{
    std::lock_guard<spinlock_mutex> lock(recv_mutex_);
    auto                            it = recv_.find(node_id);

    // XXX If we have a receive window for this source use it. The exception is
    // when we either see a SYN packet or a sequence number that is outside the
    // receive window. In that case, assume the sender restarted and re-create
    // the receive window. This could cause an issue if we see a re-transmission
    // of the first packet after the sender has advanced its window. This should
    // not happen because the sender will only open up its window if it has seen
    // its SYN packet ACK'ed.
    if (it != recv_.end()) {
        RecvWindow                      &recvw = it->second;
        std::lock_guard<spinlock_mutex> lock(recvw.mutex);

        if (!isSYN || (seq >= recvw.max - recvw.win && seq < recvw.ack + recvw.win))
            return recvw;
        else {
            // This is a new connection, so cancel selective ACK timer for the
            // old receive window
            timer_queue_.cancel(recvw);

            // Delete the old receive window
            recv_.erase(it);
        }
    }

    Node       &src = (*net_)[node_id];
    RecvWindow &recvw = recv_.emplace(std::piecewise_construct,
                                      std::forward_as_tuple(node_id),
                                      std::forward_as_tuple(src, *this, seq, recvwin_, explicit_nak_win_)).first->second;

    return recvw;
}
