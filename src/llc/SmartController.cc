// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "Logger.hh"
#include "llc/SmartController.hh"

#define DEBUG 0

#if DEBUG
#define dprintf(...) logARQ(LOGDEBUG, __VA_ARGS__)
#else /* !dprintf */
#define dprintf(...)
#endif /* !DEBUG */

SmartController::SmartController(std::shared_ptr<RadioNet> radionet,
                                 size_t mtu,
                                 std::shared_ptr<PHY> phy,
                                 double slot_size,
                                 Seq::uint_type max_sendwin,
                                 Seq::uint_type recvwin,
                                 const std::vector<evm_thresh_t> &evm_thresholds)
  : Controller(radionet, mtu)
  , phy_(phy)
  , slot_size_(slot_size)
  , mcs_fast_adjustment_period_(1.0)
  , max_sendwin_(max_sendwin)
  , recvwin_(recvwin)
  , evm_thresholds_(evm_thresholds)
  , short_per_window_(100e-3)
  , long_per_window_(400e-3)
  , short_stats_window_(100e-3)
  , long_stats_window_(400e-3)
  , mcsidx_broadcast_(0)
  , mcsidx_ack_(0)
  , mcsidx_min_(0)
  , mcsidx_max_(phy->mcs_table.size()-1)
  , mcsidx_init_(0)
  , mcsidx_up_per_threshold_(0.04)
  , mcsidx_down_per_threshold_(0.10)
  , mcsidx_alpha_(0.5)
  , mcsidx_prob_floor_(0.1)
  , ack_delay_(100e-3)
  , ack_delay_estimation_window_(1)
  , retransmission_delay_(500e-3)
  , min_retransmission_delay_(200e-3)
  , retransmission_delay_slop_(1.1)
  , sack_delay_(50e-3)
  , explicit_nak_win_(0)
  , explicit_nak_win_duration_(0.0)
  , selective_ack_(false)
  , selective_ack_feedback_delay_(0.0)
  , max_retransmissions_({})
  , demod_always_ordered_(false)
  , enforce_ordering_(false)
  , move_along_(true)
  , decrease_retrans_mcsidx_(false)
  , timestamp_seq_(0)
  , gen_(std::random_device()())
  , dist_(0, 1.0)
{
    if (evm_thresholds.size() != phy->mcs_table.size())
        throw std::out_of_range("EVM threshold table and PHY MCS table must be the same size");

    // Calculate samples needed to modulate the largest packet we will ever see
    // at each MCS
    size_t max_pkt_size = getMTU() + sizeof(struct ether_header);

    max_packet_samples_.resize(phy->mcs_table.size());

    for (mcsidx_t mcsidx = 0; mcsidx < phy->mcs_table.size(); ++mcsidx)
        max_packet_samples_[mcsidx] =  phy->getModulatedSize(mcsidx, max_pkt_size);

    timer_queue_.start();
}

SmartController::~SmartController()
{
    timer_queue_.stop();
}

void SmartController::setEmcon(NodeId node_id, bool emcon)
{
    Node &node = (*radionet_)[node_id];

    if (node.emcon != emcon) {
        // If this node can no longer transmit, kick the network input so that
        // getPacket will realize it's not allowed to transmit.
        if (node.id == radionet_->getThisNodeId())
            net_in.kick();

        node.emcon = emcon;
    }
}

bool SmartController::pull(std::shared_ptr<NetPacket> &pkt)
{
get_packet:
    // Get a packet to send. We look for a packet on our internal queue first.
    if (!getPacket(pkt))
        return false;

    // Handle broadcast packets
    if (pkt->hdr.nexthop == kNodeBroadcast) {
        pkt->mcsidx = mcsidx_broadcast_;
        pkt->g = broadcast_gain.getLinearGain();
        pkt->llc_timestamp = MonoClock::now();
        return true;
    }

    // Get node ID of destination
    NodeId nexthop = pkt->hdr.nexthop;

    // If we have received a packet from the destination, add an ACK.
    {
        RecvWindow                  &recvw = getRecvWindow(nexthop);
        std::lock_guard<std::mutex> lock(recvw.mutex);

        if (recvw.active) {
            // The packet we are ACK'ing had better be no more than 1 more than the
            // max sequence number we've received.
            if(recvw.ack > recvw.max + 1)
                logARQ(LOGERROR, "INVARIANT VIOLATED: received packet outside window: ack=%u; max=%u",
                    (unsigned) recvw.ack,
                    (unsigned) recvw.max);

            pkt->hdr.flags.ack = 1;
            pkt->ehdr().ack = recvw.ack;

#if DEBUG
            if (pkt->hdr.flags.has_seq == 0)
                dprintf("send delayed ack: node=%u; ack=%u",
                    (unsigned) nexthop,
                    (unsigned) recvw.ack);
            else
                dprintf("send ack: node=%u; ack=%u",
                    (unsigned) nexthop,
                    (unsigned) recvw.ack);
#endif

            // Append selective ACK if needed. A NAK packet should always have
            // selective ACK information
            if (recvw.need_selective_ack || pkt->internal_flags.need_selective_ack)
                appendFeedback(pkt, recvw);
        } else if (pkt->hdr.flags.has_seq == 1)
            dprintf("send: node=%u; seq=%u",
                (unsigned) nexthop,
                (unsigned) pkt->hdr.seq);
    }

    // Update our send window if this packet has a sequence number
    if (pkt->hdr.flags.has_seq == 1) {
        SendWindow                  &sendw = getSendWindow(nexthop);
        Node                        &dest = (*radionet_)[nexthop];
        std::lock_guard<std::mutex> lock(sendw.mutex);

        // It is possible that the send window shifts after we pull a packet
        // but before we get to this point. For example, an ACK could be
        // received in between the time we release the lock on the receive
        // window and this point. If that happens, we get another packet
        if (pkt->hdr.seq < sendw.unack) {
            pkt.reset();
            goto get_packet;
        }

        // This checks that the sequence number of the packet we are sending is
        // in our send window.
        if (pkt->hdr.seq < sendw.unack || pkt->hdr.seq >= sendw.unack + sendw.win) {
            logARQ(LOGERROR, "INVARIANT VIOLATED: asked to send packet outside window: nexthop=%u; seq=%u; unack=%u; win=%u",
                (unsigned) nexthop,
                (unsigned) pkt->hdr.seq,
                (unsigned) sendw.unack,
                (unsigned) sendw.win);
            pkt.reset();
            goto get_packet;
        }

        // Save the packet in our send window.
        sendw[pkt->hdr.seq].set(pkt);
        sendw[pkt->hdr.seq].timestamp = MonoClock::now();

        // If this packet is a retransmission, increment the retransmission
        // count, otherwise set it to 0.
        if (pkt->internal_flags.retransmission)
            ++pkt->nretrans;

        // Update send window metrics
        if (pkt->hdr.seq > sendw.max)
            sendw.max = pkt->hdr.seq;

        // If we have locally updated our send window, tell the receiver.
        if (sendw.send_set_unack) {
            logARQ(LOGDEBUG, "send set unack: nexthop=%u; unack=%u",
                (unsigned) nexthop,
                (unsigned) sendw.unack);
            pkt->appendSetUnack(sendw.unack);
            sendw.send_set_unack = false;
        }

        // Apply TX params. If this is a retransmission, the packet has a
        // deadline, and it was transmitted at the current MCS, decrease the MCS
        // in the hope that we can get this packet through before its deadline
        // passes.
        if (decrease_retrans_mcsidx_ &&
            pkt->internal_flags.retransmission &&
            pkt->deadline &&
            pkt->mcsidx == sendw.mcsidx &&
            pkt->mcsidx > mcsidx_min_)
            --pkt->mcsidx;
        else
            pkt->mcsidx = sendw.mcsidx;

        pkt->g = dest.g;
    } else {
        // Apply ACK TX params
        pkt->mcsidx = mcsidx_ack_;
        pkt->g = ack_gain.getLinearGain();
    }

    pkt->llc_timestamp = MonoClock::now();
    return true;
}

void SmartController::received(std::shared_ptr<RadioPacket> &&pkt)
{
    // Skip packets with invalid header
    if (pkt->internal_flags.invalid_header)
        return;

    // Get the sending node's send and receive windows. This will add the node
    // to the network if it doesn't already exist.
    NodeId     prevhop = pkt->hdr.curhop;
    RecvWindow &recvw = getRecvWindow(prevhop);
    SendWindow &sendw = getSendWindow(prevhop);

    // Record last heard timestamp
    {
        std::lock_guard<std::mutex> lock(sendw.mutex);

        sendw.heard(pkt->timestamp);
    }

    // Skip packets that aren't for us
    NodeId this_node_id = radionet_->getThisNodeId();

    if (pkt->hdr.nexthop != kNodeBroadcast &&
        pkt->hdr.nexthop != this_node_id)
        return;

    // Activate receive window and send NAK for bad packet
    {
        std::lock_guard<std::mutex> lock(recvw.mutex);

        // Update metrics. EVM and RSSI should be valid as long as the header is
        // valid.
        recvw.short_evm.update(pkt->timestamp, pkt->evm);
        recvw.long_evm.update(pkt->timestamp, pkt->evm);
        recvw.short_rssi.update(pkt->timestamp, pkt->rssi);
        recvw.long_rssi.update(pkt->timestamp, pkt->rssi);

        // In the fast adjustment period, provide feedback as quickly as possible
        if (recvw.short_evm && recvw.short_rssi && isMCSFastAdjustmentPeriod())
            startSACKTimer(recvw);

        // Handle packet with a sequence number
        if (pkt->hdr.flags.has_seq == 1) {
            // Activate the receive window if it is not yet active. If this is a
            // SYN packet or if the sequence number is outside the receive
            // window, assume the sender restarted and reset the receive window.
            // This could cause an issue if we see a re-transmission of the
            // first packet after the sender has advanced its window. This
            // should not happen because the sender will only open up its window
            // if it has seen its SYN packet ACK'ed.
            if ((pkt->hdr.nexthop == this_node_id) &&
                (!recvw.active || pkt->hdr.flags.syn || !recvw.contains(pkt->hdr.seq))) {
                // This is a new connection, so cancel selective ACK timer for the
                // old receive window
                timer_queue_.cancel(recvw);

                // Reset the receive window
                recvw.reset(pkt->hdr.seq);
            }

            // Immediately NAK non-broadcast data packets with a bad payload if
            // they contain data. We can't do anything else with the packet.
            if (pkt->internal_flags.invalid_payload) {
                if (pkt->hdr.nexthop != kNodeBroadcast) {
                    // If the packet is after our receive window, we need to advance
                    // the receive window.
                    if (pkt->hdr.seq >= recvw.ack + recvw.win)
                        advanceRecvWindow(pkt->hdr.seq, recvw);

                    // Update the max seq number we've received
                    if (pkt->hdr.seq > recvw.max) {
                        recvw.max = pkt->hdr.seq;
                        recvw.max_timestamp = pkt->timestamp;
                    }

                    // Send a NAK
                    nak(recvw, pkt->hdr.seq);
                }

                // We're done with this packet since it has a bad payload
                return;
            }
        } else {
            // We're done with this packet if it has a bad payload
            if (pkt->internal_flags.invalid_payload)
                return;
        }
    }

    // Process control info
    if (pkt->hdr.flags.has_control) {
        Node &node = (*radionet_)[prevhop];

        handleCtrlHelloAndPing(*pkt, node);
        handleCtrlTimestamp(*pkt, node);
    }

    // Handle broadcast packets
    if (pkt->hdr.nexthop == kNodeBroadcast) {
        // Clear all control information, leaving only data payload behind.
        pkt->clearControl();

        // Send the packet along if it has data
        if (pkt->ehdr().data_len != 0)
            radio_out.push(std::move(pkt));

        return;
    }

    // At this point, the packet must have been sent to us.

    // Handle ACK/NAK
    {
        std::lock_guard<std::mutex> lock(sendw.mutex);

        // Record last heard timestamp
        sendw.last_heard_timestamp = MonoClock::now();

        if (!sendw.new_window) {
            MonoClock::time_point tfeedback = MonoClock::now() - selective_ack_feedback_delay_;
            std::optional<Seq>    nak;

            // Handle any NAK
            nak = handleNAK(*pkt, sendw);

            // If packets are always demodulated in order, then when we see an
            // explicit NAK, we can assume all packets up to and including the
            // NAK'ed packet should have been received. In this case, look at
            // feedback at least up to the sequence number that was NAK'ed. We add a
            // tiny amount of slop, 0.001 sec, to make sure we *include* the NAK'ed
            // packet.
            if (demod_always_ordered_ && nak)
                tfeedback = std::max(tfeedback, sendw[*nak].timestamp + 0.001);

            // Handle ACK
            if (pkt->hdr.flags.ack) {
                // Handle statistics reported by the receiver. We do this before
                // looking at ACK's because we use the statistics to decide whether
                // to move up our MCS.
                handleReceiverStats(*pkt, sendw);

                if (pkt->ehdr().ack > sendw.unack) {
                    dprintf("ack: node=%u; seq=[%u,%u)",
                        (unsigned) prevhop,
                        (unsigned) sendw.unack,
                        (unsigned) pkt->ehdr().ack);

                    // Don't assert this because the sender could crash us with bad
                    // data! We protected against this case in the following loop.
                    //assert(pkt->ehdr().ack <= sendw.max + 1);

                    // Move the send window along. It's possible the sender sends an
                    // ACK for something we haven't sent, so we must guard against
                    // that here as well
                    for (; sendw.unack < pkt->ehdr().ack && sendw.unack <= sendw.max; ++sendw.unack) {
                        // Handle the ACK
                        handleACK(sendw, sendw.unack);

                        // Update our packet error rate to reflect successful TX
                        if (sendw.unack >= sendw.per_end)
                            sendw.txSuccess();
                    }

                    // unack is the NEXT un-ACK'ed packet, i.e., the packet we  are
                    // waiting to hear about next. Note that it is possible for the
                    // sender to ACK a packet we've already decided was bad, e.g., a
                    // retranmission, so we must be careful not to "rewind" the PER
                    // window here by blindly setting sendw.per_end = unack without
                    // the test.
                    if (sendw.unack > sendw.per_end)
                        sendw.per_end = sendw.unack;
                }

                // Handle selective ACK. We do this *after* handling the ACK,
                // because a selective ACK tells us about packets *beyond* that
                // which was ACK'ed.
                handleSelectiveACK(pkt, sendw, tfeedback);

                // If the NAK is for a retransmitted packet, count it as a
                // transmission failure. We need to check for this case because a
                // NAK for a retransmitted packet will have already been counted
                // toward our PER the first time the packet was NAK'ed. If the
                // packet has already been re-transmitted, don't record a failure.
                if (nak) {
                    SendWindow::Entry &entry = sendw[*nak];

                    if (entry.pkt && sendw.mcsidx >= entry.pkt->mcsidx && entry.pkt->nretrans > 0 && *nak >= sendw.per_cutoff) {
                        sendw.txFailure();

                        if (logger)
                            logger->logRetransmissionNAK(pkt->timestamp, sendw.node.id, *nak);

                        dprintf("txFailure nak of retransmission: node=%u; seq=%u; mcsidx=%u",
                            (unsigned) prevhop,
                            (unsigned) *nak,
                            (unsigned) entry.pkt->mcsidx);
                    }
                }

                // Update MCS based on new PER
                sendw.updateMCS(isMCSFastAdjustmentPeriod());

                // Advance the send window. It is possible that packets immediately
                // after the packet that the sender just ACK'ed have timed out and
                // been dropped, so advanceSendWindow must look for dropped packets
                // and attempt to push the send window up towards max.
                advanceSendWindow(sendw);
            }
        }
    }

    // If this packet doesn't have a sequence number, we are done
    if (pkt->hdr.flags.has_seq == 0) {
        dprintf("recv: node=%u; ack=%u",
            (unsigned) prevhop,
            (unsigned) pkt->ehdr().ack);
        return;
    }

#if DEBUG
    if (pkt->hdr.flags.ack)
        dprintf("recv: node=%u; seq=%u; ack=%u",
            (unsigned) prevhop,
            (unsigned) pkt->hdr.seq,
            (unsigned) pkt->ehdr().ack);
    else
        dprintf("recv: node=%u; seq=%u",
            (unsigned) prevhop,
            (unsigned) pkt->hdr.seq);
#endif

    // Fill our receive window
    std::lock_guard<std::mutex> lock(recvw.mutex);

    // If this is a SYN packet, ACK immediately to open up the window.
    //
    // Otherwise, start the ACK timer if it is not already running. Even if this
    // is a duplicate packet, we need to send an ACK because the duplicate may
    // be a retransmission, i.e., our previous ACK could have been lost.
    if (pkt->hdr.flags.syn)
        ack(recvw);
    else
        startSACKTimer(recvw);

    // Handle sender setting unack
    handleSetUnack(*pkt, recvw);

    // Drop this packet if it is before our receive window
    if (pkt->hdr.seq < recvw.ack) {
        dprintf("recv OUTSIDE WINDOW (DUP): node=%u; seq=%u",
            (unsigned) prevhop,
            (unsigned) pkt->hdr.seq);
        return;
    }

    // If the packet is after our receive window, we need to advance the receive
    // window.
    if (pkt->hdr.seq >= recvw.ack + recvw.win) {
        advanceRecvWindow(pkt->hdr.seq, recvw);
    } else if (recvw[pkt->hdr.seq].received) {
        // Drop this packet if we have already received it
        dprintf("recv DUP: node=%u; seq=%u",
            (unsigned) prevhop,
            (unsigned) pkt->hdr.seq);
        return;
    }

    // Update the max seq number we've received
    if (pkt->hdr.seq > recvw.max) {
        recvw.max = pkt->hdr.seq;
        recvw.max_timestamp = pkt->timestamp;
    }

    // Clear packet control information now that it's already been processed.
    pkt->clearControl();

    // If this is the next packet we expected, send it now and update the
    // receive window
    if (pkt->hdr.seq == recvw.ack) {
        recvw.ack++;

        if (pkt->ehdr().data_len != 0)
            radio_out.push(std::move(pkt));
    } else if (!enforce_ordering_ && !pkt->isTCP()) {
        // If this is not a TCP packet, insert it into our receive window, but
        // also go ahead and send it.
        if (pkt->ehdr().data_len != 0)
            radio_out.push(std::move(pkt));

        recvw[pkt->hdr.seq].alreadyDelivered();
    } else {
        // Insert the packet into our receive window
        recvw[pkt->hdr.seq].set(std::move(pkt));
    }

    // Now drain the receive window until we reach a hole
    for (auto seq = recvw.ack; seq <= recvw.max; ++seq) {
        RecvWindow::Entry &entry = recvw[seq];

        if (!entry.received)
            break;

        if (!entry.delivered && pkt->ehdr().data_len != 0)
            radio_out.push(std::move(entry.pkt));

        entry.reset();
        ++recvw.ack;
    }
}

void SmartController::transmitted(std::list<std::unique_ptr<ModPacket>> &mpkts)
{
    for (auto it = mpkts.begin(); it != mpkts.end(); ++it) {
        NetPacket &pkt = *(*it)->pkt;

        if (pkt.hdr.nexthop != kNodeBroadcast && pkt.hdr.flags.has_seq == 1) {
            SendWindow                  &sendw = getSendWindow(pkt.hdr.nexthop);
            std::lock_guard<std::mutex> lock(sendw.mutex);

            // If the destination is subject to emissions control, reset the
            // send window entry corresponding to the packet and advance the
            // send window. Otherwise, start the retransmit timer.
            if (sendw.node.emcon) {
                sendw[pkt.hdr.seq].reset();

                if (sendw.unack < pkt.hdr.seq + 1)
                    sendw.unack = pkt.hdr.seq + 1;

                advanceSendWindow(sendw);
            } else {
                startRetransmissionTimer(sendw[pkt.hdr.seq]);
            }
        }

        // Cancel the selective ACK timer when we actually have sent a selective ACK
        if (pkt.internal_flags.has_selective_ack) {
            RecvWindow                  &recvw = getRecvWindow(pkt.hdr.nexthop);
            std::lock_guard<std::mutex> lock(recvw.mutex);

            timer_queue_.cancel(recvw);
        }

        // Record timestamp at which we received this timestamp sequence number
        if (pkt.timestamp_seq) {
            {
                std::lock_guard<std::mutex> lock(timestamps_mutex_);
                Timestamps                  &ts = timestamps_[radionet_->getThisNodeId()];

                ts.timestamps_sent.insert_or_assign(*pkt.timestamp_seq, pkt.tx_timestamp);
            }

            logTimeSync(LOGDEBUG, "Transmitted timestamp: tseq_sent=%u; t_sent=%f",
                (unsigned) *pkt.timestamp_seq,
                (double) pkt.tx_timestamp.get_real_secs());
        }
    }
}

void SmartController::retransmitOnTimeout(SendWindow::Entry &entry)
{
    SendWindow                  &sendw = entry.sendw;
    std::lock_guard<std::mutex> lock(sendw.mutex);

    if (!entry.pkt) {
        logARQ(LOGDEBUG, "attempted to retransmit ACK'ed packet on timeout: node=%u",
            (unsigned) sendw.node.id);
        return;
    }

    // Record the packet error as long as receiving node can transmit
    if (!sendw.node.emcon && sendw.mcsidx >= entry.pkt->mcsidx && entry.pkt->hdr.seq >= sendw.per_cutoff) {
        sendw.txFailure();

        if (logger)
            logger->logACKTimeout(MonoClock::now(), sendw.node.id, entry.pkt->hdr.seq);

        dprintf("txFailure retransmission: node=%u; seq=%u; mcsidx=%u; short per=%0.2f",
            (unsigned) sendw.node.id,
            (unsigned) entry.pkt->hdr.seq,
            (unsigned) entry.pkt->mcsidx,
            sendw.short_per.value_or(0.0));

        sendw.updateMCS(isMCSFastAdjustmentPeriod());
    }

    // Actually retransmit (or drop) the packet
    retransmitOrDrop(entry);
}

void SmartController::ack(RecvWindow &recvw)
{
    if (!netlink_)
        return;

    if (radionet_->getThisNode().emcon)
        return;

    // Create an ACK-only packet. Why don't we set the ACK field here!? Because
    // it will be filled out when the packet flows back through the controller
    // on its way out the radio. We are just providing the opportunity for an
    // ACK by injecting a packet without a data payload at the head of the
    // queue.
    auto pkt = std::make_shared<NetPacket>(sizeof(ExtendedHeader));

    pkt->timestamp = MonoClock::now();
    pkt->hdr.curhop = radionet_->getThisNodeId();
    pkt->hdr.nexthop = recvw.node.id;
    pkt->hdr.flags = {0};
    pkt->hdr.seq = {0};
    pkt->ehdr().data_len = 0;
    pkt->ehdr().src = radionet_->getThisNodeId();
    pkt->ehdr().dest = recvw.node.id;

    // Mark this packet as seed a selective ACK
    pkt->internal_flags.need_selective_ack = 1;

    netlink_->push_hi(std::move(pkt));
}

void SmartController::nak(RecvWindow &recvw, Seq seq)
{
    if (!netlink_)
        return;

    if (radionet_->getThisNode().emcon)
        return;

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
    dprintf("send nak: node=%u; nak=%u",
        (unsigned) recvw.node.id,
        (unsigned) seq);

    if (logger)
        logger->logSendNAK(recvw.node.id, seq);

    // Create an ACK-only packet. Why don't we set the ACK field here!? Because
    // it will be filled out when the packet flows back through the controller
    // on its way out the radio. We are just providing the opportunity for an
    // ACK by injecting a packet without a data payload at the head of the
    // queue.
    auto pkt = std::make_shared<NetPacket>(sizeof(ExtendedHeader));

    pkt->timestamp = MonoClock::now();
    pkt->hdr.curhop = radionet_->getThisNodeId();
    pkt->hdr.nexthop = recvw.node.id;
    pkt->hdr.flags = {0};
    pkt->hdr.seq = {0};
    pkt->ehdr().data_len = 0;
    pkt->ehdr().src = radionet_->getThisNodeId();
    pkt->ehdr().dest = recvw.node.id;

    // Append NAK control message
    pkt->appendNak(seq);

    // Mark this packet as seed a selective ACK
    pkt->internal_flags.need_selective_ack = 1;

    netlink_->push_hi(std::move(pkt));
}

void SmartController::broadcastHello(void)
{
    if (!netlink_)
        return;

    Node &me = radionet_->getThisNode();

    if (me.emcon)
        return;

    dprintf("broadcast HELLO");

    auto pkt = std::make_shared<NetPacket>(sizeof(ExtendedHeader));

    pkt->timestamp = MonoClock::now();
    pkt->hdr.curhop = radionet_->getThisNodeId();
    pkt->hdr.nexthop = kNodeBroadcast;
    pkt->hdr.flags = {0};
    pkt->hdr.seq = {0};
    pkt->ehdr().data_len = 0;
    pkt->ehdr().src = radionet_->getThisNodeId();
    pkt->ehdr().dest = kNodeBroadcast;

    // Append hello message
    ControlMsg::Hello msg;

    msg.is_gateway = radionet_->getThisNode().is_gateway;

    pkt->appendHello(msg);

    // Echo most recently heard timestamps if we are the time master
    std::optional<NodeId> time_master = radionet_->getTimeMaster();

    if (time_master && *time_master == radionet_->getThisNodeId()) {
        // Report sent timestamps
        {
            std::lock_guard<std::mutex> lock(timestamps_mutex_);
            Timestamps                  &ts = timestamps_[radionet_->getThisNodeId()];

            for (auto it = ts.timestamps_sent.begin(); it != ts.timestamps_sent.end(); ++it) {
                TimestampSeq                tseq = it->first;
                const MonoClock::time_point &t_sent = it->second;

                if (ts.timestamps_echoed.find(tseq) == ts.timestamps_echoed.end()) {
                    pkt->appendTimestampSent(tseq, t_sent);
                    ts.timestamps_echoed.insert(tseq);
                }
            }
        }

        // Report received timestamps
        radionet_->foreach([&] (Node &node) {
            if (node.id != radionet_->getThisNodeId()) {
                std::lock_guard<std::mutex> lock(timestamps_mutex_);
                Timestamps                  &ts = timestamps_[node.id];

                for (auto it = ts.timestamps_recv.begin(); it != ts.timestamps_recv.end(); ++it) {
                    TimestampSeq                tseq = it->first;
                    const MonoClock::time_point &t_recv = it->second;


                    if (ts.timestamps_echoed.find(tseq) == ts.timestamps_echoed.end()) {
                        pkt->appendTimestampRecv(node.id, tseq, t_recv);
                        ts.timestamps_echoed.insert(tseq);
                    }
                }
            }
        });
    }

    // Add timestamp
    TimestampSeq tseq = timestamp_seq_.fetch_add(1, std::memory_order_release);

    pkt->appendTimestamp(tseq);

    // Send a timestamped HELLO
    pkt->mcsidx = mcsidx_broadcast_;
    pkt->g = 1.0;

    netlink_->push_hi(std::move(pkt));
}

void SmartController::sendPing(NodeId dest)
{
    auto pkt = std::make_shared<NetPacket>(sizeof(ExtendedHeader));

    pkt->timestamp = MonoClock::now();
    pkt->hdr.curhop = radionet_->getThisNodeId();
    pkt->hdr.nexthop = dest;
    pkt->hdr.flags = {0};
    pkt->hdr.flags.has_seq = 1;
    pkt->hdr.seq = {0};
    pkt->ehdr().data_len = 0;
    pkt->ehdr().src = radionet_->getThisNodeId();
    pkt->ehdr().dest = dest;

    // Append ping message
    ControlMsg::Ping msg;

    pkt->appendPing(msg);

    // Mark this packet as seed a selective ACK
    pkt->internal_flags.need_selective_ack = 1;

    logAMC(LOGDEBUG, "Ping send: node=%u",
        (unsigned) dest);

    netlink_->push_hi(std::move(pkt));
}

void SmartController::sendPong(NodeId dest)
{
    auto pkt = std::make_shared<NetPacket>(sizeof(ExtendedHeader));

    pkt->timestamp = MonoClock::now();
    pkt->hdr.curhop = radionet_->getThisNodeId();
    pkt->hdr.nexthop = dest;
    pkt->hdr.flags = {0};
    pkt->hdr.flags.has_seq = 1;
    pkt->hdr.seq = {0};
    pkt->ehdr().data_len = 0;
    pkt->ehdr().src = radionet_->getThisNodeId();
    pkt->ehdr().dest = dest;

    // Mark this packet as seed a selective ACK
    pkt->internal_flags.need_selective_ack = 1;

    logAMC(LOGDEBUG, "Pong send: node=%u",
        (unsigned) dest);

    netlink_->push_hi(std::move(pkt));
}

void SmartController::environmentDiscontinuity(void)
{
    std::set<NodeId> nodes;

    logAMC(LOGDEBUG, "Environment discontinuity");

    env_timestamp_ = MonoClock::now();

    {
        std::lock_guard<std::mutex> lock(send_mutex_);

        for (auto it = send_.begin(); it != send_.end(); ++it) {
            SendWindow                  &sendw = it->second;
            std::lock_guard<std::mutex> lock(sendw.mutex);

            nodes.insert(sendw.node.id);

            // Set all MCS transition probabilities to 1.0
            std::vector<double>&v = sendw.mcsidx_prob;

            std::fill(v.begin(), v.end(), 1.0);

            // Set MCS index to initial default
            sendw.setMCS(mcsidx_init_);

            // Don't use previously-sent packets to calculate PER.
            sendw.per_cutoff = sendw.seq;

            // Reset PER estimates
            sendw.resetPEREstimates();

            // Reset EVM and RSSI estimates
            sendw.short_evm.reset();
            sendw.long_evm.reset();
            sendw.short_rssi.reset();
            sendw.long_rssi.reset();
        }
    }

    {
        std::lock_guard<std::mutex> lock(recv_mutex_);

        for (auto it = recv_.begin(); it != recv_.end(); ++it) {
            RecvWindow                  &recvw = it->second;
            std::lock_guard<std::mutex> lock(recvw.mutex);

            nodes.insert(recvw.node.id);

            // Reset EVM and RSSI estimates
            recvw.short_evm.reset();
            recvw.long_evm.reset();
            recvw.short_rssi.reset();
            recvw.long_rssi.reset();
        }
    }

    // Send a ping packet to every node we're communicating with
    for (auto &&it : nodes)
        sendPing(it);
}

void SmartController::retransmitOrDrop(SendWindow::Entry &entry)
{
    assert(entry.pkt);

    if (entry.shouldDrop(max_retransmissions_))
        drop(entry);
    else
        retransmit(entry);
}

/** NOTE: The lock on the send window to which entry belongs MUST be held before
 * calling retransmit.
 */
void SmartController::retransmit(SendWindow::Entry &entry)
{
    // Check in case we have not heard from node recently
    entry.sendw.checkUnheard();

    // Squelch a retransmission when:
    // 1) The destination can't transmit, because we won't be able to hear an
    //    ACK anyway.
    // 2) The destination is unreachable and this retransmission is for any
    //    packet except the next packet we need ACK'ed
    if (entry.sendw.node.emcon ||
        (entry.sendw.node.unreachable && entry.pkt->hdr.seq != entry.sendw.max)) {
        // We need to restart the retransmission timer here so that the packet
        // will be retransmitted if the destination is reachable in the future.
        timer_queue_.cancel(entry);
        startRetransmissionTimer(entry);
        return;
    }

    if (!entry.pkt) {
        logARQ(LOGDEBUG, "attempted to retransmit ACK'ed packet");
        return;
    }

    dprintf("retransmit: node=%u; seq=%u; mcsidx=%u",
        (unsigned) entry.pkt->hdr.nexthop,
        (unsigned) entry.pkt->hdr.seq,
        (unsigned) entry.pkt->mcsidx);

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

    // Clear any control information in the packet
    entry.pkt->clearControl();

    // Mark the packet as a retransmission
    entry.pkt->internal_flags.retransmission = 1;

    // Re-queue the packet. The ACK and MCS will be set properly upon
    // retransmission.
    if (netlink_) {
        // We need to make an explicit new reference to the shared_ptr because
        // repush takes ownership of its argument.
        std::shared_ptr<NetPacket> pkt = entry.pkt;

        netlink_->repush(std::move(pkt));
    }
}

void SmartController::drop(SendWindow::Entry &entry)
{
    SendWindow &sendw = entry.sendw;

    // If the packet has already been ACK'd, forget it
    if (!entry.pending())
        return;

    // Drop the packet
    if (logger)
        logger->logLinkLayerDrop(MonoClock::now(), entry.pkt);

    dprintf("dropping packet: node=%u; seq=%u",
        (unsigned) sendw.node.id,
        (unsigned) entry.pkt->hdr.seq);

    // Cancel retransmission timer
    timer_queue_.cancel(entry);

    // Release the packet
    entry.reset();

    // Advance send window if we can
    advanceSendWindow(sendw);
}

void SmartController::advanceSendWindow(SendWindow &sendw)
{
    // Save current unack
    Seq old_unack = sendw.unack;

    // Advance send window if we can
    while (sendw.unack <= sendw.max && !sendw[sendw.unack].pending())
        ++sendw.unack;

    // Update PER cutoff
    if (sendw.unack > sendw.per_cutoff)
        sendw.per_cutoff = sendw.unack;

    // Increase the send window. We really only need to do this after the
    // initial ACK, but it doesn't hurt to do it every time...
    sendw.win = sendw.maxwin;

    // Indicate that this node's send window is now open
    if (sendw.seq < sendw.unack + sendw.win)
        sendw.setSendWindowOpen(true);

    // See if we locally updated the send window. If so, we need to tell the
    // receiver we've updated our unack
    if (sendw.unack > old_unack)
        sendw.send_set_unack = true;
}

void SmartController::advanceRecvWindow(Seq seq, RecvWindow &recvw)
{
    logARQ(LOGDEBUG, "recv OUTSIDE WINDOW (ADVANCE): node=%u; seq=%u; ack=%u; max=%u; new_ack=%u",
        (unsigned) recvw.node.id,
        (unsigned) seq,
        (unsigned) recvw.ack,
        (unsigned) recvw.max,
        (unsigned) (seq + 1 - recvw.win));

    // We want to slide the window forward so pkt->hdr.seq is the new max
    // packet. We therefore need to "forget" all packets in our current
    // window with sequence numbers less than pkt->hdr.seq - recvw.win. It's
    // possible this number is greater than our max received sequence
    // number, so we must account for that as well!
    Seq new_ack = seq + 1 - recvw.win;
    Seq forget = new_ack > recvw.max ? recvw.max + 1 : new_ack;

    // Go ahead and deliver packets that will be left outside our window.
    for (auto seq = recvw.ack; seq < forget; ++seq) {
        RecvWindow::Entry &entry = recvw[seq];

        // Go ahead and deliver the packet
        if (entry.pkt && !entry.delivered && entry.pkt->ehdr().data_len != 0)
            radio_out.push(std::move(entry.pkt));

        // Release the packet
        entry.reset();
    }

    recvw.ack = new_ack;
}

void SmartController::startRetransmissionTimer(SendWindow::Entry &entry)
{
    // Start the retransmit timer if the packet has not already been ACK'ed and
    // the timer is not already running
    if (entry.pkt && !timer_queue_.running(entry)) {
        dprintf("starting retransmission timer: node=%u; seq=%u",
            (unsigned) entry.sendw.node.id,
            (unsigned) entry.pkt->hdr.seq);
        timer_queue_.run_in(entry, entry.sendw.retransmission_delay);
    }
}

void SmartController::startSACKTimer(RecvWindow &recvw)
{
    // Start the selective ACK timer if it is not already running.
    if (!timer_queue_.running(recvw)) {
        dprintf("starting SACK timer: node=%u",
            (unsigned) recvw.node.id);

        recvw.need_selective_ack = false;
        recvw.timer_for_ack = false;
        timer_queue_.run_in(recvw, sack_delay_);
    }
}

void SmartController::handleCtrlHelloAndPing(RadioPacket &pkt, Node &node)
{
    for(auto it = pkt.begin(); it != pkt.end(); ++it) {
        switch (it->type) {
            case ControlMsg::Type::kHello:
            {
                node.is_gateway = it->hello.is_gateway;

                dprintf("HELLO: node=%u",
                    (unsigned) pkt.hdr.curhop);

                logARQ(LOGDEBUG, "Discovered neighbor: node=%u; gateway=%s",
                    (unsigned) pkt.hdr.curhop,
                    node.is_gateway ? "true" : "false");
            }
            break;

            case ControlMsg::Type::kPing:
            {
                logAMC(LOGDEBUG, "Ping recv: node=%u",
                    (unsigned) pkt.hdr.curhop);

                sendPong(pkt.hdr.curhop);
            }
            break;

            default:
                break;
        }
    }
}

void SmartController::handleCtrlTimestamp(RadioPacket &pkt, Node &node)
{
    std::optional<NodeId> time_master = radionet_->getTimeMaster();
    Node                  &me = radionet_->getThisNode();

    for(auto it = pkt.begin(); it != pkt.end(); ++it) {
        switch (it->type) {
            case ControlMsg::Type::kTimestamp:
            {
                TimestampSeq          tseq;
                MonoClock::time_point t_recv;

                tseq = it->timestamp.tseq;
                t_recv = pkt.timestamp;

                {
                    std::lock_guard<std::mutex> lock(timestamps_mutex_);
                    Timestamps                  &ts = timestamps_[node.id];

                    ts.timestamps_recv.insert_or_assign(tseq, t_recv);
                }

                logTimeSync(LOGDEBUG, "Timestamp: node=%u; tseq=%u; t_recv=%f",
                    (unsigned) pkt.hdr.curhop,
                    (unsigned) tseq,
                    (double) t_recv.get_real_secs());
            }
            break;

            case ControlMsg::Type::kTimestampSent:
            {
                TimestampSeq                tseq = it->timestamp_sent.tseq;
                MonoClock::time_point       t_sent = it->timestamp_sent.t_sent.to_mono_time();
                std::lock_guard<std::mutex> lock(timestamps_mutex_);
                Timestamps                  &ts = timestamps_[node.id];

                // Add sent timestamp
                ts.timestamps_sent.insert_or_assign(tseq, t_sent);

                // Add (send, receive) timestamp pair if we have receive timestamp
                auto tspair = ts.timestamps_recv.find(tseq);

                if (tspair != ts.timestamps_recv.end()) {
                    MonoClock::time_point t_recv = tspair->second;

                    ts.timestamps.insert_or_assign(tseq, std::make_pair(t_sent, t_recv));

                    logTimeSync(LOGDEBUG, "Timestamp pair: node=%u; t_sent=%f; t_recv=%f",
                        (unsigned) pkt.hdr.curhop,
                        (double) t_sent.get_real_secs(),
                        (double) t_recv.get_real_secs());
                }
            }
            break;

            case ControlMsg::Type::kTimestampRecv:
            {
                if (time_master && node.id == *time_master && node.id != me.id && it->timestamp_recv.node == me.id) {
                    TimestampSeq                tseq = it->timestamp_recv.tseq;
                    MonoClock::time_point       t_recv = it->timestamp_recv.t_recv.to_mono_time();
                    std::lock_guard<std::mutex> lock(timestamps_mutex_);
                    Timestamps                  &ts = timestamps_[me.id];

                    // Add received timestamp
                    ts.timestamps_recv.insert_or_assign(tseq, t_recv);

                    // Add (send, receive) timestamp pair if we have sent timestamp
                    auto tspair = ts.timestamps_sent.find(tseq);

                    if (tspair != ts.timestamps_sent.end()) {
                        MonoClock::time_point t_sent = tspair->second;

                        ts.timestamps.insert_or_assign(tseq, std::make_pair(t_sent, t_recv));

                        logTimeSync(LOGDEBUG, "Timestamp pair for us: node=%u; t_sent=%f; t_recv=%f",
                            (unsigned) pkt.hdr.curhop,
                            (double) t_sent.get_real_secs(),
                            (double) t_recv.get_real_secs());
                    }
                }
            }
            break;

            default:
                break;
        }
    }
}

inline void appendSelectiveACK(const std::shared_ptr<NetPacket> &pkt,
                               RecvWindow &recvw,
                               Seq begin,
                               Seq end)
{
    dprintf("send selective ack: node=%u; seq=[%u, %u)",
        (unsigned) recvw.node.id,
        (unsigned) begin,
        (unsigned) end);
    pkt->appendSelectiveAck(begin, end);
}

void SmartController::appendFeedback(const std::shared_ptr<NetPacket> &pkt, RecvWindow &recvw)
{
    // Append statistics
    if (recvw.short_evm && recvw.short_rssi)
        pkt->appendShortTermReceiverStats(*recvw.short_evm, *recvw.short_rssi);

    if (recvw.long_evm && recvw.long_rssi)
        pkt->appendLongTermReceiverStats(*recvw.long_evm, *recvw.long_rssi);

    // Append selective ACKs
    if (!selective_ack_)
        return;

    bool in_run = false; // Are we in the middle of a run of ACK's?
    Seq  begin = recvw.ack;
    Seq  end = recvw.ack;
    int  nsacks = 0;

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
                appendSelectiveACK(pkt, recvw, begin, end + 1);
                nsacks++;

                in_run = false;
            }
        }
    }

    // Close out any final run
    if (in_run) {
        appendSelectiveACK(pkt, recvw, begin, end + 1);
        nsacks++;
    }

    // If we cannot ACK recvw.max, add an empty selective ACK range marking the
    // end of our received packets. This will inform the sender that the last
    // stretch of packets WAS NOT received.
    if (end < recvw.max) {
        appendSelectiveACK(pkt, recvw, recvw.max+1, recvw.max+1);
        nsacks++;
    }

    // If we have too many selective ACK's, keep as many as we can, but keep the
    // *latest* selective ACKs.
    constexpr size_t sack_size = ctrlsize(ControlMsg::kSelectiveAck);
    int              nremove = 0;
    int              nkeep = nsacks;

    if (pkt->size() > getMTU()) {
        nremove = (pkt->size() - getMTU() + sack_size - 1) / sack_size;

        if (nremove > nsacks)
            nremove = nsacks;

        nkeep = nsacks - nremove;
    }

    if (max_sacks_ && nkeep > static_cast<int>(*max_sacks_)) {
        nkeep = static_cast<int>(*max_sacks_);
        nremove = nsacks - nkeep;
    }

    if (nremove > 0) {
        logARQ(LOGDEBUG, "pruning SACKs: node=%u; nremove=%d; nkeep=%d",
            (unsigned) recvw.node.id,
            nremove,
            nkeep);

        unsigned char *sack_start = pkt->data() + pkt->size() -
                                        nsacks*sack_size;

        memmove(sack_start, sack_start+nremove*sack_size, nkeep*sack_size);
        pkt->setControlLen(pkt->getControlLen() - nremove*sack_size);
        pkt->resize(pkt->size() - nremove*sack_size);
    }

    // Mark this packet as containing a selective ACK
    pkt->internal_flags.has_selective_ack = 1;

    // We no longer need a selective ACK
    recvw.need_selective_ack = false;

    // Log SACKs
    if (logger && nsacks > 0)
        logger->logSendSACK(pkt, recvw.node.id, recvw.ack);
}

void SmartController::handleReceiverStats(RadioPacket &pkt, SendWindow &sendw)
{
    for(auto it = pkt.begin(); it != pkt.end(); ++it) {
        switch (it->type) {
            case ControlMsg::Type::kShortTermReceiverStats:
            {
                float temp;

                memcpy(&temp, &it->receiver_stats.evm, sizeof(temp));
                sendw.short_evm = temp;

                memcpy(&temp, &it->receiver_stats.rssi, sizeof(temp));
                sendw.short_rssi = temp;
            }
            break;

            case ControlMsg::Type::kLongTermReceiverStats:
            {
                float temp;

                memcpy(&temp, &it->receiver_stats.evm, sizeof(temp));
                sendw.long_evm = temp;

                memcpy(&temp, &it->receiver_stats.rssi, sizeof(temp));
                sendw.long_rssi = temp;
            }
            break;

            default:
                break;
        }
    }
}

void SmartController::handleACK(SendWindow &sendw, const Seq &seq)
{
    SendWindow::Entry &entry = sendw[seq];

    // If this packet is outside our send window, we're done.
    if (seq < sendw.unack || seq >= sendw.unack + sendw.win) {
        logARQ(LOGDEBUG, "ack for packet outside send window: node=%u; seq=%u; unack=%u; end=%u",
            (unsigned) sendw.node.id,
            (unsigned) seq,
            (unsigned) sendw.unack,
            (unsigned) sendw.unack + sendw.win);

        return;
    }

    // If this packet has already been ACK'ed, we're done.
    if (!entry.pkt) {
        dprintf("ack for already ACK'ed packet: node=%u; seq=%u",
            (unsigned) sendw.node.id,
            (unsigned) seq);

        return;
    }

    // Record ACK delay
    sendw.ack(entry.timestamp);

    // Cancel retransmission timer for ACK'ed packet
    timer_queue_.cancel(entry);

    // Release the packet since it's been ACK'ed
    entry.reset();
}

std::optional<Seq> SmartController::handleNAK(RadioPacket &pkt,
                                              SendWindow &sendw)
{
    std::optional<Seq> result;

    for(auto it = pkt.begin(); it != pkt.end(); ++it) {
        switch (it->type) {
            case ControlMsg::Type::kNak:
            {
                SendWindow::Entry &entry = sendw[it->nak];

                // If this packet is outside our send window, ignore the NAK.
                if (it->nak < sendw.unack || it->nak >= sendw.unack + sendw.win || !entry.pkt) {
                    logARQ(LOGDEBUG, "nak for packet outside send window: node=%u; seq=%u; unack=%u; end=%u",
                        (unsigned) sendw.node.id,
                        (unsigned) it->nak,
                        (unsigned) sendw.unack,
                        (unsigned) sendw.unack + sendw.win);
                // If this packet has already been ACK'ed, ignore the NAK.
                } else if (!entry.pkt) {
                    logARQ(LOGDEBUG, "nak for already ACK'ed packet: node=%u; seq=%u",
                        (unsigned) sendw.node.id,
                        (unsigned) it->nak);
                } else {
                    // Log the NAK
                    dprintf("nak: node=%u; seq=%u",
                        (unsigned) sendw.node.id,
                        (unsigned) it->nak);

                    if (logger)
                        logger->logNAK(pkt.timestamp, sendw.node.id, it->nak);

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

void SmartController::handleSelectiveACK(const std::shared_ptr<RadioPacket> &pkt,
                                         SendWindow &sendw,
                                         MonoClock::time_point tfeedback)
{
    Node &node = sendw.node;
    Seq  nextSeq = sendw.unack;
    bool sawACKRun = false;

    for(auto it = pkt->begin(); it != pkt->end(); ++it) {
        switch (it->type) {
            case ControlMsg::Type::kSelectiveAck:
            {
                // Handle first selective ACK
                if (!sawACKRun) {
                    dprintf("selective ack: node=%u; per_end=%u",
                        (unsigned) node.id,
                        (unsigned) sendw.per_end);

                    // If the selective ACK is from before our send window, send
                    // a set unack control message
                    if (it->ack.begin < sendw.unack) {
                        logARQ(LOGDEBUG, "send set unack: node=%u; per_end=%u; ack=%u, ack_begin=%u; unack=%u",
                            (unsigned) node.id,
                            (unsigned) sendw.per_end,
                            (unsigned) pkt->ehdr().ack,
                            (unsigned) it->ack.begin,
                            (unsigned) sendw.unack);

                        sendw.send_set_unack = true;
                    }
                }

                // Record the gap between the last packet in the previous ACK
                // run and the first packet in this ACK run as failures.
                if (nextSeq < it->ack.begin) {
                    dprintf("selective nak: node=%u; seq=[%u,%u)",
                        (unsigned) node.id,
                        (unsigned) nextSeq,
                        (unsigned) it->ack.begin);

                    for (Seq seq = nextSeq; seq < it->ack.begin; ++seq) {
                        if (seq >= sendw.per_end) {
                            if (sendw[seq].pending()) {
                                if (sendw[seq].timestamp < tfeedback) {
                                    // Record TX failure for PER
                                    if (seq >= sendw.per_cutoff) {
                                        sendw.txFailure();

                                        if (logger)
                                            logger->logSNAK(pkt->timestamp, node.id, seq);

                                        dprintf("txFailure selective nak: node=%u; seq=%u",
                                            (unsigned) node.id,
                                            (unsigned) seq);
                                    }

                                    // Retransmit the NAK'ed packet
                                    retransmit(sendw[seq]);

                                    // Move PER window forward
                                    sendw.per_end = seq + 1;
                                }
                            } else
                                // Move PER window forward
                                sendw.per_end = seq + 1;
                        }
                    }
                }

                dprintf("selective ack: node=%u; seq=[%u,%u)",
                    (unsigned) node.id,
                    (unsigned) it->ack.begin,
                    (unsigned) it->ack.end);

                for (Seq seq = it->ack.begin; seq < it->ack.end; ++seq) {
                    // Handle the ACK
                    if (seq >= sendw.unack)
                        handleACK(sendw, seq);

                    // Update our packet error rate to reflect successful TX
                    if (seq >= sendw.per_end && sendw[seq].timestamp < tfeedback) {
                        // Record TX success for PER
                        sendw.txSuccess();

                        // Move PER window forward
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

    // Log SACKs
    if (logger && sawACKRun)
        logger->logSACK(pkt, sendw.node.id, sendw.unack);
}

void SmartController::handleSetUnack(RadioPacket &pkt, RecvWindow &recvw)
{
    for(auto it = pkt.begin(); it != pkt.end(); ++it) {
        switch (it->type) {
            case ControlMsg::Type::kSetUnack:
            {
                Seq next_ack = it->unack.unack;

                logARQ(LOGDEBUG, "set unack: node=%u; cur_ack=%u; unack=%u",
                    (unsigned) recvw.node.id,
                    (unsigned) recvw.ack,
                    (unsigned) next_ack);

                if (next_ack > recvw.ack) {
                    for (Seq seq = recvw.ack; seq < next_ack; ++seq)
                        recvw[seq].reset();

                    recvw.ack = next_ack;
                }
            }
            break;

            default:
                break;
        }
    }
}

bool SmartController::getPacket(std::shared_ptr<NetPacket>& pkt)
{
    Node &me = radionet_->getThisNode();

    for (;;) {
        if (me.emcon)
            return false;

        // We use a lock here to protect against a race between getting a packet
        // and updating the send window status of the destination. Without this
        // lock, it's possible that we receive two packets for the same
        // destination before we are able to close it's send window while
        // waiting for an ACK.
        std::unique_lock<std::mutex> net_lock(net_mutex_);

        // Get a packet from the network
        if (!net_in.pull(pkt))
            return false;

        assert(pkt);

        // We can always send a broadcast packet
        if (pkt->hdr.nexthop == kNodeBroadcast)
            return true;

        SendWindow                  &sendw = getSendWindow(pkt->hdr.nexthop);
        std::lock_guard<std::mutex> lock(sendw.mutex);

        // If packet is not sequenced, we can always send it---it has control
        // information.
        if (pkt->hdr.flags.has_seq == 0)
            return true;

        // Set the packet sequence number if it doesn't yet have one.
        if (!pkt->internal_flags.assigned_seq) {
            // If we can't fit this packet in our window, move the window along
            // by dropping the oldest packet.
            if (   sendw.seq >= sendw.unack + sendw.win
                && sendw[sendw.unack].mayDrop(max_retransmissions_)) {
                logARQ(LOGDEBUG, "MOVING WINDOW ALONG: node=%u",
                    (unsigned) pkt->hdr.nexthop);
                drop(sendw[sendw.unack]);
            }

            pkt->hdr.seq = sendw.seq++;
            pkt->internal_flags.assigned_seq = 1;

            // If this is the first packet we are sending to the destination,
            // set its SYN flag
            if (sendw.new_window) {
                pkt->hdr.flags.syn = 1;
                sendw.new_window = false;
            }

            // Close the send window if it's full and we're not supposed to
            // "move along." However, if the send window is only 1 packet,
            // ALWAYS close it since we're waiting for the ACK to our SYN!
            if (   sendw.seq >= sendw.unack + sendw.win
                && ((sendw[sendw.unack].pending() && !sendw[sendw.unack].mayDrop(max_retransmissions_)) || !move_along_ || sendw.win == 1))
                sendw.setSendWindowOpen(false);

            return true;
        } else {
            // If this packet comes before our window, drop it. It could have
            // snuck in as a retransmission just before the send window moved
            // forward. Try again!
            if (pkt->hdr.seq < sendw.unack) {
                pkt.reset();
                continue;
            }

            // Otherwise it had better be in our window because we added it back
            // when our window expanded due to an ACK!
            if(pkt->hdr.seq >= sendw.unack + sendw.win) {
                logARQ(LOGERROR, "INVARIANT VIOLATED: got packet outside window: seq=%u; unack=%u; win=%u",
                    (unsigned) pkt->hdr.seq,
                    (unsigned) sendw.unack,
                    (unsigned) sendw.win);

                pkt.reset();
                continue;
            }

            // See if this packet should be dropped. The network queue won't
            // drop a packet with a sequence number, because we need to drop a
            // packet with a sequence number in the controller to ensure the
            // send window is properly adjusted.
            if (pkt->shouldDrop(MonoClock::now())) {
                drop(sendw[pkt->hdr.seq]);
                pkt.reset();
                continue;
            }

            return true;
        }
    }
}

SendWindow &SmartController::getSendWindow(NodeId node_id)
{
    std::lock_guard<std::mutex> lock(send_mutex_);
    auto                        it = send_.find(node_id);

    if (it != send_.end())
        return it->second;
    else {
        auto result = send_.emplace(std::piecewise_construct,
                                    std::forward_as_tuple(node_id),
                                    std::forward_as_tuple((*radionet_)[node_id],
                                                          *this,
                                                          max_sendwin_,
                                                          retransmission_delay_));

        return result.first->second;
    }
}

RecvWindow &SmartController::getRecvWindow(NodeId node_id)
{
    std::lock_guard<std::mutex> lock(recv_mutex_);
    auto                        it = recv_.find(node_id);

    if (it != recv_.end())
        return it->second;
    else {
        auto result = recv_.emplace(std::piecewise_construct,
                                    std::forward_as_tuple(node_id),
                                    std::forward_as_tuple((*radionet_)[node_id],
                                                          *this,
                                                          recvwin_,
                                                          explicit_nak_win_));

        return result.first->second;
    }
}

SendWindow::SendWindow(Node &n,
                       SmartController &controller,
                       Seq::uint_type maxwin,
                       double retransmission_delay_)
    : node(n)
    , controller(controller)
    , mcs_table(controller.phy_->mcs_table)
    , mcsidx(0)
    , new_window(true)
    , window_open(true)
    , last_heard_timestamp(MonoClock::now())
    , seq({0})
    , unack({0})
    , max({0})
    , send_set_unack(false)
    , win(1)
    , maxwin(maxwin)
    , mcsidx_prob(controller.phy_->mcs_table.size(), 1.0)
    , per_cutoff({0})
    , per_end({0})
    , prev_short_per(1)
    , prev_long_per(1)
    , short_per(1)
    , long_per(1)
    , retransmission_delay(retransmission_delay_)
    , ack_delay(1.0)
    , entries_(maxwin, *this)
{
    setMCS(controller.mcsidx_init_);
}

void SendWindow::setSendWindowOpen(bool open)
{
    if (open != window_open) {
        controller.netlink_->setLinkStatus(node.id, open);
        window_open = open;
    }
}

void SendWindow::ack(const MonoClock::time_point &tx_time)
{
    auto now = MonoClock::now();

    ack_delay.update(now, (now - tx_time).get_real_secs());

    if (ack_delay)
        retransmission_delay = std::max(controller.getMinRetransmissionDelay(),
                                        controller.getRetransmissionDelaySlop()*(*ack_delay));
    else
        retransmission_delay = controller.getMinRetransmissionDelay();
}

void SendWindow::txSuccess(void)
{
    short_per.update(0.0);
    long_per.update(0.0);
}

void SendWindow::txFailure(void)
{
    short_per.update(1.0);
    long_per.update(1.0);
}

void SendWindow::updateMCS(bool fast_adjust)
{
#if DEBUG
    bool changed = false;
#endif

    if (short_per && *short_per != prev_short_per) {
        prev_short_per = *short_per;
#if DEBUG
        changed = true;
#endif
    }

    if (long_per && *long_per != prev_long_per) {
        prev_long_per = *long_per;
#if DEBUG
        changed = true;
#endif
    }

#if DEBUG
    if (changed)
        logAMC(LOGDEBUG, "updateMCS: node=%u; short per=%0.2f (%lu samples); long per=%0.2f (%lu samples)",
            node.id,
            short_per.value_or(0),
            short_per.size(),
            long_per.value_or(0),
            long_per.size());
#endif

    // First for high PER, then test for low PER
    if (short_per && *short_per > controller.mcsidx_down_per_threshold_) {
        // Perform hysteresis on future MCS increases by decreasing the
        // probability that we will transition to this MCS index.
        mcsidx_prob[mcsidx] =
            std::max(mcsidx_prob[mcsidx]*controller.mcsidx_alpha_,
                     controller.mcsidx_prob_floor_);

        logAMC(LOGDEBUG, "Transition probability for MCS: node=%u; index=%u; prob=%0.2f",
            node.id,
            (unsigned) mcsidx,
            mcsidx_prob[mcsidx]);

        // Decrease MCS until we hit rock bottom or we hit an MCS that produces
        // packets too large to fit in a slot.
        unsigned n = 0; // Number of MCS levels to decrease

        while (mcsidx > n &&
               mcsidx - n > controller.mcsidx_min_ &&
               mcs_table[mcsidx-(n+1)].valid) {
            // Increment number of MCS levels we will move down
            ++n;

            // If we don't have both an EVM threshold and EVM feedback from the
            // sender, stop. Otherwise, use our EVM information to decide if we
            // should decrease the MCS level further.
            SmartController::evm_thresh_t &next_evm_threshold = controller.evm_thresholds_[mcsidx-n];

            if (!next_evm_threshold || !long_evm || (*long_evm < *next_evm_threshold))
                break;
        }

        // Move down n MCS levels
        if (n != 0)
            moveDownMCS(n);
        else
            resetPEREstimates();
    } else if (fast_adjust && short_evm) {
        mcsidx_t new_mcsidx;
        auto     current_evm = long_evm.value_or(*short_evm);

        for (new_mcsidx = controller.mcsidx_min_; new_mcsidx < controller.mcsidx_max_; ++new_mcsidx) {
            SmartController::evm_thresh_t &evm_threshold = controller.evm_thresholds_[new_mcsidx + 1];

            if (current_evm >= evm_threshold)
                break;
        }

        setMCS(new_mcsidx);
    } else if (long_per && *long_per < controller.mcsidx_up_per_threshold_) {
        double old_prob = mcsidx_prob[mcsidx];

        // Set transition probability of current MCS index to 1.0 since we
        // successfully passed the long PER test
        mcsidx_prob[mcsidx] = 1.0;

        if (mcsidx_prob[mcsidx] != old_prob)
            logAMC(LOGDEBUG, "Transition probability for MCS: node=%u; index=%u; prob=%0.2f",
                node.id,
                (unsigned) mcsidx,
                mcsidx_prob[mcsidx]);

        // Now we see if we can actually increase the MCS index.
        if (mayMoveUpMCS())
            moveUpMCS();
        else
            resetPEREstimates();
    }
}

bool SendWindow::mayMoveUpMCS(void) const
{
    // We can't move up if we're at the top of the MCS hierarchy...
    if (mcsidx == controller.mcsidx_max_ || mcsidx == mcs_table.size() - 1)
        return false;

    // There are two cases where we may move up an MCS level:
    //
    // 1) The next-higher MCS has an EVM threshold that we meet
    // 2) The next-higher MCS *does not* have an EVM threshold, but we pass
    //    the probabilistic transition test.
    SmartController::evm_thresh_t &next_evm_threshold = controller.evm_thresholds_[mcsidx+1];

    if (next_evm_threshold) {
        if (long_evm) {
            logAMC(LOGDEBUG, "EVM threshold: evm_threshold=%0.1f, evm=%0.1f",
                *next_evm_threshold,
                *long_evm);

            return *long_evm < *next_evm_threshold;
        } else
            return false;
    }

    std::lock_guard<std::mutex> lock(controller.gen_mutex_);

    return controller.dist_(controller.gen_) < mcsidx_prob[mcsidx+1];
}

void SendWindow::setMCS(size_t new_mcsidx)
{
    const size_t BUFSIZE = 10;

    assert(new_mcsidx >= 0);
    assert(new_mcsidx < mcs_table.size());

    // Move new MCS index up until we reach a valid MCS
    while (new_mcsidx < mcs_table.size() - 1 &&
           !mcs_table[new_mcsidx].valid)
        ++new_mcsidx;

    // Bail if MCS isn't actually changing
    if (new_mcsidx == mcsidx)
        return;

    // Log before change
    const char *direction = (new_mcsidx > mcsidx) ? "up" : "down";
    auto       old_mcsidx = mcsidx;
    auto       old_short_per = short_per;
    auto       old_long_per = long_per;
    char       short_per_s[BUFSIZE];
    char       long_per_s[BUFSIZE];
    char       short_evm_s[BUFSIZE];
    char       long_evm_s[BUFSIZE];

#if DEBUG
    snprintf(short_per_s, sizeof(short_per_s), "%0.2f", short_per.value_or(0));
    snprintf(long_per_s, sizeof(long_per_s), "%0.2f", long_per.value_or(0));

    dprintf("Moving %s modulation scheme: node=%u; mcsidx=%u; short_per=%s; long_per=%s; swin=%lu; lwin=%lu",
            direction,
            node.id,
            (unsigned) mcsidx,
            short_per ? short_per_s : "none",
            long_per ? long_per_s : "none",
            short_per.getWindowSize(),
            long_per.getWindowSize());
#endif /* DEBUG */

    // Set new MCS index
    mcsidx = new_mcsidx;

    // Set end of PER window
    per_end = seq;

    // Reset PER estimates
    resetPEREstimates();

    // Inform network queue of new MCS
    const MCS *mcs = controller.phy_->mcs_table[new_mcsidx].mcs;

    controller.netlink_->updateMCS(node.id, mcs);

    // Log change
    snprintf(short_per_s, sizeof(short_per_s), "%0.2f", old_short_per.value_or(0));
    snprintf(long_per_s, sizeof(long_per_s), "%0.2f", old_long_per.value_or(0));
    snprintf(short_evm_s, sizeof(short_evm_s), "%0.1f", short_evm.value_or(0));
    snprintf(long_evm_s, sizeof(long_evm_s), "%0.1f", long_evm.value_or(0));

    logAMC(LOGDEBUG, "Moved %s modulation scheme: node=%u; mcsidx=%u (from %u); short_per=%s; long_per=%s; prob=%.02f; short_evm=%s; long_evm=%s; unack=%u; init_seq=%u; swin=%lu; lwin=%lu",
           direction,
           node.id,
           (unsigned) mcsidx,
           (unsigned) old_mcsidx,
           old_short_per ? short_per_s : "none",
           old_long_per ? long_per_s : "none",
           mcsidx_prob[mcsidx],
           short_evm ? short_evm_s : "none",
           long_evm ? long_evm_s : "none",
           (unsigned) unack,
           (unsigned) per_end,
           short_per.getWindowSize(),
           long_per.getWindowSize());
}

void SendWindow::resetPEREstimates(void)
{
    short_per.setWindowSize(std::max(1.0, controller.short_per_window_*controller.min_channel_bandwidth_/controller.max_packet_samples_[mcsidx]));
    short_per.reset();

    long_per.setWindowSize(std::max(1.0, controller.long_per_window_*controller.min_channel_bandwidth_/controller.max_packet_samples_[mcsidx]));
    long_per.reset();
}

void SendWindow::heard(std::optional<MonoClock::time_point> when)
{
    // Record last-heard timestamp
    last_heard_timestamp = when.value_or(MonoClock::now());

    // If the node was unreachable before, mark it as reachable now
    if (node.unreachable) {
        node.unreachable = false;

        // Open send window if we have room
        if (seq < unack + win)
            setSendWindowOpen(true);

        logARQ(LOGDEBUG, "Node now reachable: node=%u",
            (unsigned) node.id);
    }
}

void SendWindow::checkUnheard(void)
{
    if (!node.emcon &&
        !node.unreachable &&
        controller.unreachable_timeout_ &&
        (MonoClock::now() - last_heard_timestamp).get_real_secs() > *controller.unreachable_timeout_) {
        node.unreachable = true;

        setSendWindowOpen(false);

        logARQ(LOGDEBUG, "Node unreachable: node=%u",
            (unsigned) node.id);
    }
}

void SendWindow::Entry::operator()()
{
    sendw.controller.retransmitOnTimeout(*this);
}

RecvWindow::RecvWindow(Node &n,
                       SmartController &controller,
                       Seq::uint_type win,
                       size_t nak_win)
    : node(n)
    , controller(controller)
    , active(false)
    , ack({0})
    , max({0})
    , win(win)
    , need_selective_ack(false)
    , timer_for_ack(false)
    , explicit_nak_win(nak_win)
    , explicit_nak_idx(0)
    , entries_(win)
{
    short_evm.setTimeWindow(controller.short_stats_window_);
    long_evm.setTimeWindow(controller.long_stats_window_);
    short_rssi.setTimeWindow(controller.short_stats_window_);
    long_rssi.setTimeWindow(controller.long_stats_window_);
}

void RecvWindow::reset(Seq seq)
{
    active = true;

    ack = seq;
    max = seq - 1;
    need_selective_ack = false;
    timer_for_ack = false;

    auto nak_win = explicit_nak_win.size();

    explicit_nak_win.clear();
    explicit_nak_win.resize(nak_win);
    explicit_nak_idx = 0;

    entries_.clear();
    entries_.resize(win);
}

void RecvWindow::operator()()
{
    std::lock_guard<std::mutex> lock(this->mutex);

    if (timer_for_ack) {
        controller.ack(*this);
    } else {
        need_selective_ack = true;
        timer_for_ack = true;

        dprintf("starting full ACK timer: node=%u",
            (unsigned) node.id);
        controller.timer_queue_.run_in(*this,
            controller.ack_delay_ - controller.sack_delay_);
    }
}
