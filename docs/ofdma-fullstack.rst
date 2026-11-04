Downlink OFDMA in the production stack (P5)
===========================================

Status 2026-11-04. ``--phy native-ofdma`` carries up to two packets for
different destinations in one burst, through the production synthesizer,
MAC, ARQ and queues. It ran in simulation only (three-node scenarios over
the simulated device), and nothing was transmitted. Liquid OFDM stays the
default; ``native-ofdm`` and liquid OFDM are unchanged and keep working (all
P2 scenarios use them).

What is implemented
-------------------

**Frame engine** (``src/simulation/NativeOFDMA.hh``, format byte ``0xD4``):

* Two training symbols, then a 30-byte common control header on every data
  tone (QPSK, v29 r1/2, soft decoded, CRC-32). The header gives the
  allocation and, for each of two user slots, the DragonRadio header (next
  hop, sequence number, flags), MCS index, constellation (1–8 bits per
  tone), codes and length.
* Payloads on disjoint contiguous tone sets: equal halves, 1:2, 2:1, or
  every tone to one user. Each user has its own constellation, code and
  scrambler, and the pilots are common.
* One shared IQ buffer, as long as the longer user needs.

The receiver decodes the control header and demodulates only the slots its
filter wants. A corrupted control header yields nothing, and the next frame
is still found. Because the control header uses the whole band, it is at
least as robust as either user's payload.

**Production plumbing:**

* ``ModPacket.partners`` holds further packets in the same burst, with one
  shared IQ buffer and one scheduled transmission. ``PHY::getMaxBurstUsers``,
  ``PacketModulator::modulateBurst`` and ``ChannelModulator::modulateBurst``
  carry them; resampling and mixing are shared.
* ``NativeOFDMA`` (``src/phy/NativeOFDMA.*``) reuses ``NativeOFDM``'s MCS
  validation, mask, codes and constellations.
* The slot synthesizer (``UnichannelSynthesizer``) batches. After the first
  unicast packet it takes a held packet for another destination, or waits
  at most ``--ofdma-burst-wait`` (2 ms) for one; a kicker thread bounds the
  blocking pull. Unsuitable packets are held in order (at most 4). A burst
  that does not fit carries all its packets to the next slot.
* The MAC expands partners into per-packet records for TX timestamps, the
  send log and the controller, counting airtime once
  (``ModPacket::burst_partner``). Missed slots return every packet to the
  controller.
* Every packet keeps its own header, sequence number and send window. ARQ
  is therefore per destination, and a retransmission carries only that
  destination's packet, never a partner's delivered data.
* Uplink feedback (ACKs, UE data) uses the UEs' own TDMA slots, so it is
  time separated. UEs never transmit at the same time.

Evidence
--------

``tests/fullstack/test_native_ofdma.cc`` (engine) covers:

* equal and unequal allocations;
* BPSK through 256-QAM, uncoded, rs8 and v27;
* an idle user and an empty payload;
* three chunk sizes;
* one burst stream received at 35 dB by one UE and 3 dB by another (the
  strong UE decodes every frame; the weak one none; no partner slot is ever
  demodulated);
* recovery after a corrupted control header.

Full-stack scenarios (``scripts/sim-fullstack.py``): node 1 is the base
station and nodes 2 and 3 are UEs, TDMA with three slots, rs8 QPSK, ARQ, a
15 kB tail-drop queue, and 1000-byte UDP. "Shared" is the fraction of node
1's packets that rode in a two-user burst.

.. list-table::
   :header-rows: 1

   * - Scenario
     - Result (one run each)
   * - Both UEs strong, both saturated
     - Every frame received; shared 66–86%. Goodput UE 2 / UE 3: 148 / 77
       and 95 / 77 kb/s in two runs, and 116 / 76 kb/s with UE 3's link at
       -33 dB, where it still received every frame. With ``native-ofdm``
       (one packet per burst) on the same setup: 149 / 77 kb/s.
   * - One UE idle (traffic to UE 2 only)
     - Single-user bursts on every tone; 586 delivered, 226 kb/s, every
       frame received.
   * - UE 3 disconnected (both directions erased)
     - Before the queue fix: UE 2 got 21 packets (1480 dropped at the
       queue), with OFDMA and with ``native-ofdm`` alike. After it: UE 2
       got 557 delivered, 213 kb/s.
   * - UE 3 weak (link -34 dB: 63–71% frame delivery)
     - UE 2 kept 100% frame delivery and 86–87 kb/s. UE 3 got 156–167
       delivered after 67 retransmissions (budget 3) or 94 (unlimited).
       Shared 82–86%: UE 3's retransmissions rode alongside UE 2's packets.
   * - UE 3 effectively dead but heard (-35 dB: header decodes, payload
       never)
     - With unlimited retransmissions (the default), 564 of the base
       station's 675 transmissions went to UE 3 and UE 2 got only 77
       packets. A finite ``--max-retransmissions`` is required.
   * - 1:2 allocation, UE 2 at 5 packets/s, UE 3 saturated, per-flow queue
     - Not demonstrated: UE 2's packets waited about 10 s behind UE 3's
       backlog (21 of 101 delivered). This needs a per-destination fair
       scheduler.

No application duplicates and no corrupt payloads in any run.

Findings
--------

1. **A blocked destination starved the others** (production; affects
   hardware runs with ``--taildrop``; fixed in ``fe23d959``). Packets to a
   node whose send window is closed cannot leave the queue. In a full
   tail-drop queue they caused every arrival for the healthy destination to
   be dropped. The queue now pushes out the oldest packets that cannot leave
   when an arrival can. ``test_net_queue`` fails before and passes after.
2. **No capacity gain at this configuration.** A two-user burst at half
   the tones is about 12.8k samples (two 1000-byte rs8 packets). One fits in
   a 25 ms slot window of 25k samples, plus at most one single-user frame;
   single-user TDMA fits three 8k frames. Aggregate goodput was therefore
   equal to TDMA or up to a quarter lower. OFDMA's benefits here would come
   from frequency-selective scheduling, unequal traffic and shorter
   packets, none of which these flat-channel scenarios exercise.
3. **Per-destination scheduling is missing.** FIFO and mandate queues do
   not share airtime between destinations by demand, so neither unequal
   allocation nor a weak user's retry load is controlled beyond a retry
   budget. The base station's split between UEs (about 2:1) is the same
   with ``native-ofdm``, so it is upstream of the PHY.

Limits and next steps
---------------------

* Two users; fixed allocations (no adaptive allocation); one OFDMA control
  format; uplink single-user only.
* No per-destination weighted scheduling or deadlines at the base station
  (needed for unequal allocations to matter).
* The weak-user region of the rs8 waterfall is narrow (-33 dB delivers
  everything, -35 dB nothing); results are single runs at a few points.
* The UEs decode the whole-band control header but only their own slot. A
  UE cannot use its partner's payload.
* No OTA validation. Total TX power is shared between users (each tone has
  the same energy as in a single-user burst); equal per-user energy is not
  an option.
