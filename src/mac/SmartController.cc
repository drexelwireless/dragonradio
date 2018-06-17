#include "Logger.hh"
#include "RadioConfig.hh"
#include "mac/SmartController.hh"

#define DEBUG 0

#if DEBUG
#define dprintf(...) logEvent(__VA_ARGS__)
#else /* !dprintf */
#define dprintf(...)
#endif /* !DEBUG */

void SendWindow::operator()()
{
    controller.retransmit(*this);
}

void RecvWindow::operator()()
{
    controller.ack(*this);
}

SmartController::SmartController(std::shared_ptr<Net> net,
                                 Seq::uint_type max_sendwin,
                                 Seq::uint_type recvwin,
                                 double modidx_up_per_threshold,
                                 double modidx_down_per_threshold)
  : Controller(net)
  , max_sendwin_(max_sendwin)
  , recvwin_(recvwin)
  , modidx_up_per_threshold_(modidx_up_per_threshold)
  , modidx_down_per_threshold_(modidx_down_per_threshold)
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

    // Get node ID of destination
    NodeId nexthop = pkt->nexthop;

    // If we have received a packet from the destination, ACK.
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
        startRetransmissionTimer(sendw);

        // Update send window metrics
        if (pkt->seq > max)
            sendw.max.store(pkt->seq, std::memory_order_release);
    }

    // Apply TX params
    if (pkt->isFlagSet(kBroadcast)) {
        pkt->tx_params = &broadcast_tx_params;
        pkt->g = broadcast_tx_params.g_0dBFS.getValue();
    } else {
        Node &dest = (*net_)[nexthop];

        dest.updateNetPacketTXParams(*pkt);
    }

    return true;
}

void SmartController::received(std::shared_ptr<RadioPacket>&& pkt)
{
    if (!pkt->isFlagSet(kBroadcast) && pkt->nexthop != net_->getMyNodeId())
        return;

    // Process control info
    if (pkt->isFlagSet(kControl)) {
        for(auto it = pkt->begin(); it != pkt->end(); ++it) {
            switch (it->type) {
                case ControlMsg::Type::kHello:
                {
                    Node &node = net_->addNode(pkt->curhop);

                    node.is_gateway = it->hello.is_gateway;

                    dprintf("ARQ: recv from %u: HELLO",
                        (unsigned) pkt->curhop);

                    if (rc.verbose)
                        fprintf(stderr, "ARQ: Discovered neighbor %u\n",
                            (unsigned) pkt->curhop);
                }
                break;

                default:
                    break;
            }
        }
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
                    // Release the packet since it's been ACK'ed
                    sendw[unack].reset();

                    // Update our packet error rate to reflect successful TX
                    txSuccess(dest);
                }

                // If the max packet we sent was ACK'ed, cancel the retransmit
                // timer.
                if (unack == max + 1) {
                    dprintf("ARQ: recv from %u: canceling retransmission timer",
                        (unsigned) prevhop);
                    timer_queue_.cancel(sendw);
                }

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
            if (ehdr.ack >= unack) {
                if (!sendw[ehdr.ack])
                    fprintf(stderr, "Received NAK from node %d for seq %d, but can't find that packet!\n",
                        (int) prevhop,
                        (int) ehdr.ack);
                else {
                    // We need to make an explicit new reference to the
                    // shared_ptr because push takes ownership of its argument.
                    std::shared_ptr<NetPacket> pkt = sendw[ehdr.ack];

                    // Record the packet error
                    txFailure(dest);

                    if (netq_)
                        netq_->push_hi(std::move(pkt));

                    dprintf("ARQ: recv from %u: nak=%u",
                        (unsigned) prevhop,
                        (unsigned) ehdr.ack);
                }
            }
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

    // Start the ACK timer if it is not already running. Even if this is a
    // duplicate packet, we need to send an ACK because the duplicate may be a
    // retransmission, i.e., our previous ACK could have been lost.
    startACKTimer(recvw);

    // Drop this packet if we have already received it
    if (pkt->seq < recvw.ack) {
        dprintf("ARQ: recv from %u: DUP seq=%u",
            (unsigned) prevhop,
            (unsigned) pkt->seq);
        return;
    }

    // Drop this packet if it is outside our receive window
    if (pkt->seq > recvw.ack + recvw.win) {
        dprintf("ARQ: recv from %u: OUTSIDE WINDOW seq=%u",
            (unsigned) prevhop,
            (unsigned) pkt->seq);
        return;
    }

    // Resize the packet to truncate non-data bytes
    pkt->resize(sizeof(ExtendedHeader) + pkt->data_len);

    // Update the max seq number we've received
    if (pkt->seq > recvw.max)
        recvw.max = pkt->seq;

    // If this is the next packet we expected, send it now and update the
    // receive window
    if (pkt->seq == recvw.ack) {
        recvw.ack++;
        radio_out.push(std::move(pkt));
    } else {
        // Otherwise insert the packet into our receive window
        recvw[pkt->seq] = std::move(pkt);
    }

    // Now drain the receive window until we reach a hole
    for (auto seq = recvw.ack; seq <= recvw.max; ++seq) {
        if (!recvw[seq])
            break;

        radio_out.push(std::move(recvw[seq]));
        recvw[seq].reset();
        ++recvw.ack;
    }
}

void SmartController::retransmit(SendWindow &sendw)
{
    std::lock_guard<spinlock_mutex> lock(sendw.mutex);
    Seq                             unack = sendw.unack.load(std::memory_order_acquire);
    Seq                             max = sendw.max.load(std::memory_order_acquire);

    // We may have received an ACK, in which case we don't need to re-transmit
    if (unack <= max) {
        dprintf("ARQ: send to %u: retransmit seq=%u; max=%u",
            (unsigned) sendw.node_id,
            (unsigned) unack,
            (unsigned) max);

        // We need to make an explicit new reference to the shared_ptr because
        // push takes ownership of its argument.
        std::shared_ptr<NetPacket> pkt = sendw[unack];
        Node                       &dest = (*net_)[sendw.node_id];

        // INVARIANT: packets come from the network in sequence order. If this
        // invariant holds, then we will never have a "hole" in our send window,
        // and this assertion must hold.
        assert(pkt);

        // Record the packet error
        txFailure(dest);

        if (netq_)
            netq_->push_hi(std::move(pkt));
    }

    // Re-start the retransmit timer
    startRetransmissionTimer(sendw);
}

void SmartController::ack(RecvWindow &recvw)
{
    if (!netq_)
        return;

    std::lock_guard<spinlock_mutex> lock(recvw.mutex);

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

    ControlMsg msg;

    msg.type = ControlMsg::Type::kHello;
    msg.hello.is_gateway = rc.is_gateway;

    pkt->appendControl(msg);

    netq_->push_hi(std::move(pkt));
}

void SmartController::startRetransmissionTimer(SendWindow &sendw)
{
    // Start the retransmit timer if it is not already running.
    if (!timer_queue_.running(sendw)) {
        dprintf("ARQ: send to %u: starting retransmission timer",
            (unsigned) sendw.node_id);
        timer_queue_.run_in(sendw, (*net_)[sendw.node_id].retransmission_delay);
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


void SmartController::txSuccess(Node &node)
{
    node.short_per.update(0.0);
    node.long_per.update(0.0);

    if (   node.long_per.getNSamples() >= rc.long_per_npackets
        && node.long_per.getValue() < modidx_up_per_threshold_
        && node.modidx < net_->tx_params.size() - 1) {
        if (rc.verbose)
            fprintf(stderr, "Moving up modulation scheme\n");
        logEvent("AMC: Moving up modulation scheme");
        ++node.modidx;
        node.tx_params = &net_->tx_params[node.modidx];
        node.short_per.reset(node.short_per.getValue());
        node.long_per.reset(node.long_per.getValue());
    }
}

void SmartController::txFailure(Node &node)
{
    node.short_per.update(1.0);
    node.long_per.update(1.0);

    if (   node.short_per.getNSamples() > rc.short_per_npackets
        && node.short_per.getValue() > modidx_down_per_threshold_
        && node.modidx > 0) {
        if (rc.verbose)
            fprintf(stderr, "Moving down modulation scheme\n");
        logEvent("AMC: Moving down modulation scheme");
        --node.modidx;
        node.tx_params = &net_->tx_params[node.modidx];
        node.short_per.reset(node.short_per.getValue());
        node.long_per.reset(node.long_per.getValue());
    }
}

bool SmartController::getPacket(std::shared_ptr<NetPacket>& pkt)
{
    for (;;) {
        // Get a packet from the network
        if (!net_in.pull(pkt))
            return false;

        assert(pkt);

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
        SendWindow &sendw = send_.emplace(std::piecewise_construct,
                                          std::forward_as_tuple(node_id),
                                          std::forward_as_tuple(node_id, *this, max_sendwin_)).first->second;

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
        RecvWindow &recvw = it->second;

        if (!isSYN || (seq < recvw.max - recvw.win || seq >= recvw.ack + recvw.win))
            return recvw;
    }

    RecvWindow &recvw = recv_.emplace(std::piecewise_construct,
                                      std::forward_as_tuple(node_id),
                                      std::forward_as_tuple(node_id, *this, recvwin_)).first->second;

    recvw.ack = recvw.max = seq;

    return recvw;
}
