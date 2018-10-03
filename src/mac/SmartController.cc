#include "Logger.hh"
#include "RadioConfig.hh"
#include "mac/SmartController.hh"

#define DEBUG 0

#if DEBUG
#define dprintf(...) logEvent(__VA_ARGS__)
#else /* !dprintf */
#define dprintf(...)
#endif /* !DEBUG */

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

    controller.ack(*this);
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
  , mcsidx_init_(std::min(mcsidx_init, (unsigned) net_->tx_params.size()))
  , mcsidx_up_per_threshold_(mcsidx_up_per_threshold)
  , mcsidx_down_per_threshold_(mcsidx_down_per_threshold)
  , mcsidx_alpha_(mcsidx_alpha)
  , mcsidx_prob_floor_(mcsidx_prob_floor)
  , explicit_nak_win_(0)
  , explicit_nak_win_duration_(0.0)
  , selective_ack_(false)
  , enforce_ordering_(false)
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
        applyTXParams(*pkt, &broadcast_tx_params, 1.0f);

        return true;
    }

    // Get node ID of destination
    NodeId nexthop = pkt->nexthop;

    // If we have received a packet from the destination and this is not an
    // explicit NAK, add an ACK.
    RecvWindow *recvwptr = maybeGetReceiveWindow(nexthop);

    if (recvwptr && !pkt->isFlagSet(kNAK)) {
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
            dprintf("ARQ: send to %u: ack=%u",
                (unsigned) nexthop,
                (unsigned) recvw.ack);
        else
            dprintf("ARQ: send to %u: seq=%u; ack=%u",
                (unsigned) nexthop,
                (unsigned) pkt->seq,
                (unsigned) recvw.ack);
#endif

        // We're sending an ACK, so cancel the ACK timer
        timer_queue_.cancel(recvw);
    } else if (pkt->data_len != 0)
        dprintf("ARQ: send to %u: seq=%u",
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

        // Start the retransmit timer if it is not already running.
        startRetransmissionTimer(sendw[pkt->seq]);

        // Update send window metrics
        if (pkt->seq > max)
            sendw.max.store(pkt->seq, std::memory_order_release);

        // Apply TX params
        applyTXParams(*pkt, dest.tx_params, dest.g);
    } else
        // Apply broadcast TX params
        applyTXParams(*pkt, &broadcast_tx_params, 1.0f);

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

    // Immediately NAK data packets with a bad payload if they contain data.
    // We can't do anything else with the packet.
    if (pkt->isInternalFlagSet(kInvalidPayload)) {
        if (pkt->data_len != 0)
            nak(pkt->curhop, pkt->seq);

        return;
    }

    // Get a reference to the sending node
    Node &node = (*net_)[pkt->curhop];

    // Process control info
    if (pkt->isFlagSet(kControl)) {
        handleCtrlHello(node, pkt);
        handleCtrlTimestampDeltas(node, pkt);
        handleCtrlACK(node, pkt);
    }

    // Resize the packet to truncate non-data bytes
    pkt->resize(sizeof(ExtendedHeader) + pkt->data_len);

    // Handle broadcast packets
    if (pkt->isFlagSet(kBroadcast) && pkt->data_len != 0) {
        radio_out.push(std::move(pkt));
        return;
    }

    // If this packet was not destined for us, we are done
    if (pkt->nexthop != net_->getMyNodeId())
        return;

    // Get node ID of source and the extended header
    NodeId         prevhop = pkt->curhop;
    ExtendedHeader &ehdr = pkt->getExtendedHeader();

    // Handle ACK/NAK
    SendWindow *sendwptr = maybeGetSendWindow(prevhop);

    if (sendwptr) {
        SendWindow                      &sendw = *sendwptr;
        Node                            &dest = (*net_)[sendw.node_id];
        std::lock_guard<spinlock_mutex> lock(sendw.mutex);
        Seq                             unack = sendw.unack.load(std::memory_order_acquire);
        Seq                             max = sendw.max.load(std::memory_order_acquire);

        if (pkt->isFlagSet(kACK)) {
            if (ehdr.ack > unack) {
                dprintf("ARQ: recv from %u: ack=%u; unack=%u",
                    (unsigned) prevhop,
                    (unsigned) ehdr.ack,
                    (unsigned) unack);

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
                    if (unack >= sendw.mcsidx_init_seq)
                        txSuccess(sendw, dest);
                }

                // If the initial sequence number corresponding to the current
                // MCS is so old that the sequence numbers have wrapped around,
                // update it
                if (sendw.mcsidx_init_seq > unack + sendw.win)
                    sendw.mcsidx_init_seq = unack - 1;

                // Increase the send window. We really only need to do this
                // after the initial ACK, but it doesn't hurt to do it every
                // time...
                sendw.win = sendw.maxwin;

                // Now that our window is open, pending packets in our window
                // are eligible to be sent, so add them to the high-priority
                // network queue.
                if (netq_ && !sendw.pending.empty()) {
                    auto begin = sendw.pending.cbegin();
                    auto end = sendw.pending.cend();
                    auto it = sendw.pending.cbegin();

                    while (it != end && dest.seq < unack + sendw.win) {
                        (*it)->seq = dest.seq++;
                        (*it)->setInternalFlag(kHasSeq);
                        ++it;
                    }

                    netq_->splice_hi(sendw.pending, begin, it);
                }

                // Update unack
                sendw.unack.store(unack, std::memory_order_release);
            }
        } else if (pkt->isFlagSet(kNAK)) {
            if (ehdr.ack >= unack)
                handleNAK(sendw, dest, ehdr.ack);
        }
    }

    // If this packet doesn't contain any data, we are done
    if (pkt->data_len == 0) {
        dprintf("ARQ: recv from %u: ack=%u",
            (unsigned) prevhop,
            (unsigned) ehdr.ack);
        return;
    }

#if DEBUG
    if (pkt->isFlagSet(kACK))
        dprintf("ARQ: recv from %u: seq=%u; ack=%u",
            (unsigned) prevhop,
            (unsigned) pkt->seq,
            (unsigned) ehdr.ack);
    else
        dprintf("ARQ: recv from %u: seq=%u",
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
        startACKTimer(recvw);

    // Drop this packet if it is outside our receive window
    if (pkt->seq < recvw.ack) {
        dprintf("ARQ: recv from %u: OUTSIDE WINDOW (DUP) seq=%u",
            (unsigned) prevhop,
            (unsigned) pkt->seq);
        return;
    }

    if (pkt->seq > recvw.ack + recvw.win) {
        dprintf("ARQ: recv from %u: OUTSIDE WINDOW seq=%u",
            (unsigned) prevhop,
            (unsigned) pkt->seq);
        return;
    }

    // Drop this packet if we have already received it
    if (recvw[pkt->seq].received) {
        dprintf("ARQ: recv from %u: DUP seq=%u",
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

void SmartController::retransmitOnTimeout(SendWindow::Entry &entry)
{
    SendWindow                      &sendw = entry.sendw;
    std::lock_guard<spinlock_mutex> lock(sendw.mutex);
    Node                            &dest = (*net_)[sendw.node_id];

    if (!entry.pkt) {
        logEvent("AMC: attempted to retransmit ACK'ed packet on timeout");
        return;
    }

    dprintf("ARQ: send to %u: retransmit seq=%u",
        (unsigned) sendw.node_id,
        (unsigned) entry->seq);

    // Record the packet error
    if (entry.pkt->seq >= sendw.mcsidx_init_seq) {
        txFailureUpdatePER(dest);

        logEvent("AMC: txFailure retransmission: seq=%u; per=%f",
            (unsigned) entry.pkt->seq,
            dest.short_per.getValue());

        txFailure(sendw, dest);
    }

    // Actually retransmit the packet
    retransmit(entry);
}

void SmartController::ack(RecvWindow &recvw)
{
    if (!netq_)
        return;

    dprintf("ARQ: send to %u: DELAYED ack=%u",
        (unsigned) recvw.node_id,
        (unsigned) recvw.ack);

    // Create an ACK-only packet. Why don't we set the ACK field here!? Because
    // it will be filled out when the packet flows back through the controller
    // on its way out the radio. We are just providing the opportunity for an
    // ACK by injecting a packet without a data payload at the head of the
    // queue.
    auto pkt = std::make_shared<NetPacket>(sizeof(ExtendedHeader));

    pkt->curhop = net_->getMyNodeId();
    pkt->nexthop = recvw.node_id;
    pkt->flags = 0;
    pkt->seq = 0;
    pkt->data_len = 0;
    pkt->src = net_->getMyNodeId();
    pkt->dest = recvw.node_id;

    // Append selective ACK control messages
    appendCtrlACK(recvw, pkt);

    netq_->push_hi(std::move(pkt));
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

    // Limit number of explicit NAK's we send
    auto now = MonoClock::now();

    if (recvw.explicit_nak_win[recvw.explicit_nak_idx] + explicit_nak_win_duration_ > now)
        return;

    recvw.explicit_nak_win[recvw.explicit_nak_idx] = now;
    recvw.explicit_nak_idx = (recvw.explicit_nak_idx + 1) % explicit_nak_win_;

    // Send the explicit NAK
    dprintf("ARQ: send to %u: nak=%u",
        (unsigned) node_id,
        (unsigned) seq);

    auto           pkt = std::make_shared<NetPacket>(sizeof(ExtendedHeader));
    ExtendedHeader &ehdr = pkt->getExtendedHeader();

    pkt->curhop = net_->getMyNodeId();
    pkt->nexthop = node_id;
    pkt->flags = 0;
    pkt->seq = 0;
    pkt->data_len = 0;
    pkt->src = net_->getMyNodeId();
    pkt->dest = node_id;

    pkt->setFlag(kNAK);
    ehdr.ack = seq;

    netq_->push_hi(std::move(pkt));
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

    // Append timestamp deltas
    for (auto it = net_->begin(); it != net_->end(); ++it) {
        if (it->second.time_info.saw_timestamp)
            pkt->appendTimestampDelta(it->second.id,
                                      it->second.time_info.last_timestamp_epoch,
                                      it->second.time_info.last_timestamp_delta);
    }

    // Send a timestamped HELLO
    if (mac_) {
        pkt->tx_params = &broadcast_tx_params;
        pkt->g = broadcast_tx_params.g_0dBFS.getValue();
        mac_->sendTimestampedPacket(Clock::now() + rc.timestamp_delay, std::move(pkt));
    } else
        netq_->push_hi(std::move(pkt));
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

    dprintf("ARQ: send to %u: retransmit seq=%u",
        (unsigned) sendw.node_id,
        (unsigned) entry->seq);

    // We need to make an explicit new reference to the shared_ptr because push
    // takes ownership of its argument.
    std::shared_ptr<NetPacket> pkt = entry;

    // Update ACK if we can
    if (pkt->isFlagSet(kACK)) {
        RecvWindow                      &recvw = *maybeGetReceiveWindow(pkt->nexthop);
        //std::lock_guard<spinlock_mutex> lock(recv_mutex_);
        ExtendedHeader                  &ehdr = pkt->getExtendedHeader();

        ehdr.ack = recvw.ack;
    }

    // Put the packet on the high-priority network queue
    if (netq_)
        netq_->push_hi(std::move(pkt));

    // Re-start the retransmit timer
    startRetransmissionTimer(entry);
}

void SmartController::startRetransmissionTimer(SendWindow::Entry &entry)
{
    // Start the retransmit timer if it is not already running.
    if (!timer_queue_.running(entry)) {
        dprintf("ARQ: send to %u seq %u: starting retransmission timer",
            (unsigned) entry.sendw.node_id,
            (unsigned) entry->seq);
        timer_queue_.run_in(entry, (*net_)[entry.sendw.node_id].retransmission_delay);
    }
}

void SmartController::startACKTimer(RecvWindow &recvw)
{
    // Start the ACK timer if it is not already running.
    if (!timer_queue_.running(recvw)) {
        dprintf("ARQ: send to %u: starting ACK delay timer",
            (unsigned) recvw.node_id);
        timer_queue_.run_in(recvw, (*net_)[recvw.node_id].ack_delay);
    }
}

void SmartController::handleCtrlHello(Node &node, std::shared_ptr<RadioPacket>& pkt)
{
    for(auto it = pkt->begin(); it != pkt->end(); ++it) {
        switch (it->type) {
            case ControlMsg::Type::kHello:
            {
                node.is_gateway = it->hello.is_gateway;

                dprintf("ARQ: recv from %u: HELLO",
                    (unsigned) pkt->curhop);

                logEvent("ARQ: Discovered neighbor %u; gateway=%s",
                    (unsigned) pkt->curhop,
                    node.is_gateway ? "true" : "false");
            }
            break;

            case ControlMsg::Type::kTimestamp:
            {
                node.time_info.updateTimestamp(it->timestamp, pkt->timestamp);

                logEvent("TIMESYNC: Timestamp: node=%u; delta=%g",
                    (unsigned) pkt->curhop,
                    (double) node.time_info.last_timestamp_delta.t.get_real_secs());
            }
            break;

            default:
                break;
        }
    }
}

void SmartController::handleCtrlTimestampDeltas(Node &node, std::shared_ptr<RadioPacket>& pkt)
{
    // Process timestamp deltas.
    // If we have seen a timestamp from this node and it is the time master,
    // synchronize with it.
    if (node.time_info.saw_timestamp && node.id != net_->getMyNodeId() && node.id == net_->getTimeMaster()) {
        for(auto it = pkt->begin(); it != pkt->end(); ++it) {
            switch (it->type) {
                case ControlMsg::Type::kTimestampDelta:
                {
                    if (it->timestamp_delta.node == net_->getMyNodeId()) {
                        double            epsilon; // TX delay
                        double            delta;   // Clock difference
                        double            sigma;   // Clock skew

                        double our_delta   = node.time_info.last_timestamp_delta.get_real_secs();
                        double their_delta = it->timestamp_delta.delta.to_wall_time().get_real_secs();

                        epsilon = 0.5*(our_delta + their_delta);
                        delta = 0.5*(our_delta - their_delta);

                        sigma = delta/(node.time_info.last_timestamp_our_time - time_sync_.last_adjustment).get_real_secs();

                        logEvent("TIMESYNC: Time stamp delta between us and node %u is %g",
                            (unsigned) pkt->curhop,
                            (double) our_delta);

                        logEvent("TIMESYNC: Time stamp delta between node %u and us is %g",
                            (unsigned) pkt->curhop,
                            (double) their_delta);

                        if (it->timestamp_delta.epoch == Clock::epoch()) {
                            logEvent("TIMESYNC: Estimated clock delta between us and node %u is %g (epsilon=%g)",
                                (unsigned) pkt->curhop,
                                (double) delta,
                                (double) epsilon);

                            if (time_sync_.time_master == node.id) {
                                time_sync_.skew.update(sigma);

                                logEvent("TIMESYNC: Estimated skew between us and node %u is %g (sample=%g)",
                                    (unsigned) pkt->curhop,
                                    (double) time_sync_.skew.getValue(),
                                    (double) sigma);
                            }

                            if (time_sync_.time_master == 0)
                                Clock::adjust(Clock::time_point { -delta }, 0.);
                            else {
                                Clock::adjust(Clock::time_point { -delta }, -time_sync_.skew.getValue());
                                logEvent("TIMESYNC: Setting skew to %g",
                                    (double) -time_sync_.skew.getValue());
                            }

                            time_sync_.time_master = node.id;
                            time_sync_.last_adjustment = Clock::now();

                            // Timestamp deltas are no longer valid since we've adjusted our clock
                            for (auto it = net_->begin(); it != net_->end(); ++it)
                                it->second.time_info.saw_timestamp = false;
                        }
                    }
                }
                break;

                default:
                    break;
            }
        }
    }
}

void SmartController::handleCtrlACK(Node &node, std::shared_ptr<RadioPacket>& pkt)
{
    // Process selective ACKs
    SendWindow *sendwptr = maybeGetSendWindow(pkt->curhop);

    if (sendwptr) {
        SendWindow                      &sendw = *sendwptr;
        std::lock_guard<spinlock_mutex> lock(sendw.mutex);

        for(auto it = pkt->begin(); it != pkt->end(); ++it) {
            switch (it->type) {
                case ControlMsg::Type::kAck:
                {
                    dprintf("ARQ: selective ack from %u: seq=%u-%u",
                        (unsigned) node.id,
                        (unsigned) it->ack.begin,
                        (unsigned) it->ack.end);

                    for (Seq seq = it->ack.begin; seq <= it->ack.end; ++seq)
                        handleACK(sendw, seq);
                }
                break;

                default:
                    break;
            }
        }
    }
}

void SmartController::appendCtrlACK(RecvWindow &recvw, std::shared_ptr<NetPacket>& pkt)
{
    if (!selective_ack_)
        return;

    bool in_run = false; // Are we in the middle of a run of ACK's?
    Seq  begin, end;

    for (Seq seq = recvw.ack + 1; seq < recvw.max && pkt->size() + ctrlsize(ControlMsg::Type::kAck) < rc.mtu; ++seq) {
        if (recvw[seq].received) {
            if (!in_run) {
                in_run = true;
                begin = seq;
            }

            end = seq;
        } else {
            if (in_run) {
                dprintf("ARQ: selective ack to %u: seq=%u-%u",
                    (unsigned) recvw.node_id,
                    (unsigned) begin,
                    (unsigned) end);
                pkt->appendAck(begin, end);

                in_run = false;
            }
        }
    }

    if (in_run) {
        dprintf("ARQ: selective ack to %u: seq=%u-%u",
            (unsigned) recvw.node_id,
            (unsigned) begin,
            (unsigned) end);
        pkt->appendAck(begin, end);
    }
}

void SmartController::handleACK(SendWindow &sendw, const Seq &seq)
{
    SendWindow::Entry &entry = sendw[seq];

    // Cancel retransmission timer for ACK'ed packet
    timer_queue_.cancel(entry);

    // Release the packet since it's been ACK'ed
    entry.reset();
}

void SmartController::handleNAK(SendWindow &sendw, Node &dest, const Seq &seq)
{
    // If we received a NAK for a packet that was already ACK'ed, nevermind
    if (seq < sendw.unack)
        return;

    if (!sendw[seq])
        logEvent("ARQ: nak from %u for already ACK'ed packet: seq=%u",
            (unsigned) sendw.node_id,
            (unsigned) seq);

    // Record the packet error
    txFailureUpdatePER(dest);

    logEvent("AMC: txFailure nak: seq=%u; per=%f",
        (unsigned) seq,
        dest.short_per.getValue());

    txFailure(sendw, dest);

    // Retransmit the packet
    dprintf("ARQ: recv from %u: nak=%u",
        (unsigned) sendw.node_id,
        (unsigned) seq);

    retransmit(sendw[seq]);
}

void SmartController::txSuccess(SendWindow &sendw, Node &node)
{
    node.short_per.update(0.0);
    node.long_per.update(0.0);

    double long_per = node.long_per.getValue();

    if (   node.long_per.getNSamples() >= node.long_per.getWindowSize()
        && long_per < mcsidx_up_per_threshold_) {
        double old_prob = sendw.mcsidx_prob[sendw.mcsidx];

        // Set transition probability of current MCS index to 1.0 since we
        // successfully passed the long PER test
        sendw.mcsidx_prob[sendw.mcsidx] = 1.0;

        if (sendw.mcsidx_prob[sendw.mcsidx] != old_prob)
            logEvent("AMC: Transition probability for MCS index %u = %f",
                (unsigned) sendw.mcsidx,
                sendw.mcsidx_prob[sendw.mcsidx]);

        // Now we see if we can actually increase the MCS index. Not only must
        // there be a higher entry in the MCS table, but we must pass the
        // probabilistic transition test.
        if (   sendw.mcsidx < net_->tx_params.size() - 1
            && dist_(gen_) < sendw.mcsidx_prob[sendw.mcsidx+1]) {
            if (rc.verbose && ! rc.debug)
                fprintf(stderr, "Moving up modulation scheme\n");
            ++sendw.mcsidx;
            sendw.mcsidx_init_seq = node.seq;
            node.tx_params = &net_->tx_params[sendw.mcsidx];

            resetPEREstimates(node);

            logEvent("AMC: Moving up modulation scheme: fec0=%s; fec1=%s; ms=%s; per=%f; unack=%u; init_seq=%u; swin=%lu; lwin=%lu",
                node.tx_params->mcs.fec0_name(),
                node.tx_params->mcs.fec1_name(),
                node.tx_params->mcs.ms_name(),
                long_per,
                (unsigned) sendw.unack.load(std::memory_order_release),
                (unsigned) sendw.mcsidx_init_seq,
                node.short_per.getWindowSize(),
                node.long_per.getWindowSize());
        } else
            resetPEREstimates(node);
    }
}

void SmartController::txFailureUpdatePER(Node &node)
{
    node.short_per.update(1.0);
    node.long_per.update(1.0);
}

void SmartController::txFailure(SendWindow &sendw, Node &node)
{
    double short_per = node.short_per.getValue();

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

        logEvent("AMC: Transition probability for MCS index %u = %f",
            (unsigned) sendw.mcsidx,
            sendw.mcsidx_prob[sendw.mcsidx]);

        // Move down modulation scheme
        if (rc.verbose && ! rc.debug)
            fprintf(stderr, "Moving down modulation scheme\n");
        --sendw.mcsidx;
        sendw.mcsidx_init_seq = node.seq;
        node.tx_params = &net_->tx_params[sendw.mcsidx];

        resetPEREstimates(node);

        logEvent("AMC: Moving down modulation scheme: fec0=%s; fec1=%s; ms=%s; per=%f; unack=%u; init_seq=%u; swin=%lu; lwin=%lu",
            node.tx_params->mcs.fec0_name(),
            node.tx_params->mcs.fec1_name(),
            node.tx_params->mcs.ms_name(),
            short_per,
            (unsigned) sendw.unack.load(std::memory_order_release),
            (unsigned) sendw.mcsidx_init_seq,
            node.short_per.getWindowSize(),
            node.long_per.getWindowSize());
    }
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
            // add it to the send window's pending queue.
            //
            // NOTE: The pending queue can grow indefinitely! This is not good,
            // so it is the responsibility of the network layer to perform
            // admission control.
            //
            // NOTE: The receiver thread can ALSO assign sequence numbers. We
            // want to make sure packets we get from the network aren't ever
            // reordered. The reason there isn't a race condition between the
            // receiver thread and us, the sender thread, is slightly subtle.
            // Only the receiver can open the window, at which time it will hold
            // the lock on the send window and attempt to drain the pending
            // queue. There are two possibilities:
            //
            // 1) If the receiver doesn't drain the queue, it will fill the
            // window, in which case we will add packets to the pending queue,
            // so order will be maintained.
            //
            // 2) If the receiver does drain the queue, we are free to add more
            // packets to the send window.

            if (nexthop.seq < unack + sendw.win) {
                pkt->seq = nexthop.seq++;
                pkt->setInternalFlag(kHasSeq);

                // If this is the first packet we are sending to the destination,
                // set its SYN flag
                if (sendw.new_window) {
                    pkt->setFlag(kSYN);
                    sendw.new_window = false;
                }

                return true;
            } else {
                dprintf("ARQ: send to %u: POSTPONE",
                    (unsigned) pkt->nexthop);
                sendw.pending.emplace_back(std::move(pkt));
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

            return true;
        }
    }
}

void SmartController::resetPEREstimates(Node &node)
{
    double max_packets_per_slot = getMaxPacketsPerSlot(*node.tx_params);

    node.short_per.setWindowSize(rc.amc_short_per_nslots*max_packets_per_slot);
    node.long_per.setWindowSize(rc.amc_long_per_nslots*max_packets_per_slot);
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
                                          std::forward_as_tuple(node_id, *this, max_sendwin_)).first->second;

        sendw.mcsidx = mcsidx_init_;
        sendw.mcsidx_init_seq = dest.seq;
        sendw.mcsidx_prob.resize(net_->tx_params.size(), 1.0);

        dest.tx_params = &net_->tx_params[mcsidx_init_];

        resetPEREstimates(dest);

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
    // when we see a SYN packet that is outside the receive window. In that
    // case, assume the sender restarted and re-create the receive window. This
    // could cause an issue if we see a re-transmission of the first packet
    // after the sender has advanced its window. This should not happen because
    // the sender will only open up its window if it has seen its SYN packet
    // ACK'ed.
    if (it != recv_.end()) {
        RecvWindow                      &recvw = it->second;
        std::lock_guard<spinlock_mutex> lock(recvw.mutex);

        if (!isSYN || (seq >= recvw.max - recvw.win && seq < recvw.ack + recvw.win))
            return recvw;
    }

    RecvWindow &recvw = recv_.emplace(std::piecewise_construct,
                                      std::forward_as_tuple(node_id),
                                      std::forward_as_tuple(node_id, *this, recvwin_, explicit_nak_win_)).first->second;

    recvw.ack = recvw.max = seq;

    return recvw;
}
