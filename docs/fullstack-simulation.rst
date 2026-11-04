Full-stack simulation (P2)
==========================

Status 2026-11-03. The production radio (PHY, channelizer, synthesizer, ARQ
controller, network queue and TDMA MAC) runs unmodified against a simulated
radio device, one ``dragonradio`` process per node. Only the device and the
tap interface are simulated. No RF was transmitted and no privileges are
needed.

Architecture
------------

``USRP`` (``src/USRP.hh``) is now an abstract device interface with two
implementations:

* ``UHDUSRP``: the previous UHD code, unchanged in behavior. ``USRP(...)``
  from Python still constructs it, so hardware scripts are unaffected.
* ``SimUSRP`` (``src/sim/SimUSRP.cc``): selected with ``--sim-air SOCKET``.

The MAC, channelizer and synthesizer only use the interface. ``SimUSRP``
reproduces the UHD semantics the MAC depends on:

* Time is the host monotonic clock, shared by every node on the host. ``MonoClock``
  already falls back to it without a UHD device (P1).
* Sample *n* occurs at time *n/fs*. A timed TX burst lands at its exact sample
  index. Continuation segments follow contiguously, and IQ buffer timestamps
  and ``getNextTXTime`` advance as with UHD.
* A timed burst submitted after its start time is dropped and counted late,
  like UHD's time error. A continuation that arrives after its air time counts
  as an underflow and goes out late.
* RX streams in chunks of ``rx_max_samps``. Chunks before the requested time
  are discarded, and ``undersample`` and ``oversample`` are set as the UHD
  ``burstRX`` loop sets them. Samples are released once their time plus
  ``--sim-rx-latency`` (default 2 ms) has passed. A reader more than 0.5 s
  behind overflows: samples are skipped and counted.
* Received bursts are held in a bounded queue of 2^25 samples, and drops are
  counted.

``scripts/sim-air.py`` relays each node's timestamped bursts to every other
node and records, per burst, how far ahead of its air time it arrived. Each
receiver renders its own sample stream. The stream is complex Gaussian noise
(``--sim-noise-db``, per sample) plus every other node's bursts after a
directed link model: gain, carrier offset as a continuous rotation in
absolute time, integer delay, and burst erasure probability. The difference
between the transmitter's and receiver's tuning is applied as a frequency
shift. Forward and reverse links are independent. Device gains are not
modeled; link gain sets the SNR.

``--sim-traffic`` replaces the tap interface with ``SimTraffic``
(``src/sim/SimTraffic.cc``). It generates the Ethernet/IPv4/UDP frames a tap
device would deliver, with node MAC and IP addresses from ``--tap-macaddr``
and ``--tap-ipaddr``, so ``NetFilter``, the queue and the controller see real
frames. Each payload carries source, destination, flow, sequence number, send
time and an FNV-1a hash. The receiving side verifies the hash, removes
duplicates and records latency.

``sim::RxRecorder`` sits between channelizer and controller in the
simulation script only. It records every frame the PHY hands up, including
retransmissions, invalid payloads and invalid headers. The production logger
records only packets delivered to the network.

Running
-------

Build a workspace as in ``docs/native-fullstack.rst``, then::

    grid/run-sim-fullstack.sh WORKSPACE OUTDIR [--scenario NAME ...] [-- radio options]

For each scenario, ``scripts/sim-fullstack.py`` starts the relay and one
``dragonradio scripts/sim-node.py`` per node. It then scores the run from
three sources:

* Application sequence numbers from ``SimTraffic``.
* Each sender's production HDF5 ``send`` log: every attempt, ``nretrans``, and
  queue or deadline drops.
* The receiver's ``RxRecorder``.

Device counters cover late bursts, overflows and queue drops. Defaults are
2 nodes, TDMA (35 ms slots, 10 ms guard), 1 MHz, native OFDM M=48 uncoded
QPSK, ``--arq`` where stated, and 4 s warm-up, 20 s of traffic and 4 s
drain.

Runs are **wall-clock paced**. They use real time, so CPU load can cause late
bursts or overflows, which are counted and fail the run. Results are not
bit-reproducible. A deterministic virtual-time mode is not implemented.

Results (gridgpu01, 72 threads)
-------------------------------

All scenarios passed. The tables below are from the development workspace. A
from-scratch rerun at commit 8a7bf612 also passed: a fresh workspace from
``git archive`` via ``grid/setup-fullstack-build.sh``, then
``build-fullstack.sh`` with all four ``tests/fullstack`` programs, all eight
scenarios and all eight variant runs. Its numbers agree; for example,
saturated goodput was 340.7 kb/s (99.4%) and erasure-arq needed 218
retransmissions.

Delivery counts unique application packets. A frame is
one PHY transmission attempt. Per-receiver invalid-header counts were zero in
every run.

.. list-table::
   :header-rows: 1

   * - Scenario
     - Flow
     - Delivered / sent
     - Data attempts (retx)
     - Frames rx / tx
     - Median latency
   * - clean-arq
     - 1→2, 2→1 40 pkt/s × 500 B
     - 801/801, 801/801
     - 802 (1), 801 (0)
     - 892/892, 887/887
     - 74 ms, 76 ms
   * - clean-noarq
     - same, DummyController
     - 801/801, 801/801
     - 801, 801
     - 801/801, 801/801
     - 74 ms, 74 ms
   * - erasure-noarq
     - 1→2, 20% burst erasure
     - 634/801 (79.2%)
     - 801 (0)
     - 634/801
     - 75 ms
   * - erasure-arq
     - same, ARQ on
     - 801/801
     - 1016 (215)
     - 806/1016
     - 86 ms
   * - ack-loss
     - 1→2, 30% erasure on 2→1 only
     - 801/801
     - 897 (96)
     - 897/897 fwd, 81/122 rev
     - 78 ms
   * - asymmetric
     - 1→2 50×400 B at 10%; 2→1 20×1000 B at 30%
     - 1002/1002, 401/401
     - 1196 (194), 561 (160)
     - 1146/1275, 486/671
     - 84 ms, 99 ms
   * - saturation
     - 1→2 offered 150×1000 B (1.2 Mb/s)
     - 1019/3004 in 28 s
     - 1015 (5)
     - 1015/1015
     - queue-bound
   * - impaired
     - both ways, -20 dB, 2 kHz / -1.5 kHz CFO, 37/11-sample delay
     - 801/801, 801/801
     - 807 (6), 804 (3)
     - 894/894, 893/893
     - 73 ms, 75 ms

* **Saturation.** Goodput was 340.2 kb/s over the arrival window. That is
  99.2% of the TDMA bound of 342.9 kb/s: three 6912-sample frames fit in each
  25 ms transmit window, one slot of two, 8000 payload bits per frame. The
  generator's surplus waits in the unbounded FIFO queue, so median latency is
  set by the queue (8.8 s). Queue limits and expiry were not exercised.
* **Erasure without ARQ.** Delivery of 79.2% matches the 20% burst erasure,
  with one frame per burst at this load. With ARQ on, all 801 packets arrived
  after 215 retransmissions.
* **Reverse-link loss.** Losing 41 of 122 reverse frames (ACKs) cost 96
  retransmissions. Before the SYN fix (defect 3), this run delivered the
  first packet twice more; after it, nothing was delivered twice.

Every transmitted burst reached the relay at least 3.7 ms before its air
time. The mean lead was 8–11 ms against the MAC's 5 ms send lead, and no
burst was late, underflowed, overflowed or dropped in any run.

Variants (``clean-arq`` and ``erasure-arq``, same settings, ``--`` options):

.. list-table::
   :header-rows: 1

   * - Variant
     - clean-arq delivered (retx)
     - erasure-arq delivered (retx)
     - Median latency, clean
   * - default (freqdomain channelizer and synthesizer)
     - 801/801 + 801/801 (1, 0)
     - 801/801 (215)
     - 74 ms, 76 ms
   * - ``--channelizer timedomain --synthesizer timedomain``
     - 801/801 + 801/801 (0, 0)
     - 801/801 (210)
     - 55 ms, 56 ms
   * - ``--channelizer overlap``
     - 801/801 + 801/801 (3, 0)
     - 801/801 (212)
     - 57 ms, 57 ms
   * - ``--synthesizer multichannel``
     - 801/801 + 801/801 (0, 8)
     - 801/801 (234)
     - 74 ms, 77 ms
   * - liquid OFDM control: ``--phy ofdm --set header_fec1=none``
     - 801/801 + 801/801 (1, 2)
     - 801/801 (218)
     - 76 ms, 78 ms

Every variant received all frames on the clean links with zero invalid
headers or payloads. The frequency-domain channelizer adds about 19 ms of
median latency over the time-domain and overlap channelizers at this
configuration. The liquid control needs an uncoded header (``--set``, a
``sim-node.py`` option) because the workspace's liquidDSP has no libfec for
the default ``v29p78`` header code.

Real tap interfaces and kernel TCP/UDP
--------------------------------------

The ``tap-*`` scenarios drop ``--sim-traffic``. Each node runs in its own
unprivileged user and network namespace (``unshare -rn``). Inside it, the
production ``TunTap`` creates ``tap0`` at 10.10.10.<node>, and the radio's
``RadioNet`` adds the static ARP entries. ``scripts/sim-netapp.py`` uses the
kernel UDP and TCP stacks across the radio link:

* **UDP.** Paced datagrams carry a sequence number, send time and SHA-256.
* **TCP bulk.** A seeded byte stream is sent, and the receiver returns its
  byte count and SHA-256.
* **TCP request/response.** Sequential echo exchanges are timed.

Nothing changes outside the namespaces. The namespaces and their tap devices
disappear when the node exits. The relay socket is shared through the
filesystem, and ``CLOCK_MONOTONIC`` is shared because no time namespace is
created. The kernel's IPv6 and other non-IPv4 frames are dropped by
``NetFilter`` as on hardware. Each scenario ran once, for 20 s of
application time, with the same radio settings as above.

.. list-table::
   :header-rows: 1

   * - Scenario
     - Traffic
     - Result
     - PHY frames rx/tx
   * - tap-udp
     - UDP both ways, 40 × 500 B/s
     - 800/800 and 800/800 delivered; median one-way 78 ms and 77 ms
     - 895/895, 894/894
   * - tap-tcp-bulk
     - 400 kB 1→2
     - Complete, SHA-256 match; 10.0 s, 320 kb/s
     - 312/312, 264/264
   * - tap-tcp-rr
     - 150 × 64 B echo 1→2
     - 150/150; RTT median 82 ms, 95th percentile 189 ms
     - 213/213, 216/216
   * - tap-bidirectional
     - 150 kB TCP each way, 60 echoes, UDP 10 × 300 B/s each way, at once
     - Both transfers complete (267 and 260 kb/s); 60/60 echoes, RTT median
       82 ms, 95th percentile 582 ms; UDP 200/200 and 200/200
     - 553/553, 559/559
   * - tap-tcp-erasure-arq
     - 200 kB 1→2, 10% burst erasure both ways, ARQ on
     - Complete, SHA-256 match; 6.4 s, 249 kb/s; 17 and 23 ARQ retransmissions
     - 163/181, 144/165
   * - tap-tcp-erasure-noarq
     - same, ARQ off
     - Complete, SHA-256 match; 7.4 s, 216 kb/s (TCP recovers alone)
     - 143/159, 115/131

* **TCP bulk capacity.** Full-size 1514-byte frames take 9956 samples, so two
  fit per 25 ms transmit window. With 1448 TCP payload bytes per segment, the
  TDMA bound for one direction is 331 kb/s. TCP reached 320 kb/s, 96.7% of
  the bound, including slow start and the handshake.
* **Bidirectional.** The two transfers used separate slots and together
  carried 527 kb/s. Echo latency at the 95th percentile rose to 582 ms because
  the echoes wait behind bulk segments in the FIFO queue. There is no priority
  for small or interactive flows in this configuration.
* **ARQ under loss.** With 10% burst erasure, link-layer ARQ gave TCP 15% more
  goodput than TCP's own recovery.

Defects found and fixed
-----------------------

All three were found by these runs. Each has a regression test that fails
before the fix and passes after it.

1. **Native PHY dropped frames that started at a receive-buffer boundary**
   (P1 adapter bug). ``TDChannelizer`` and ``FDChannelizer`` reset the
   demodulator at each new receive buffer unless ``isFrameOpen()`` is true.
   The native detector scans with two symbols of lookahead, so the end of
   every buffer is still unscanned. A preamble there was discarded. TDMA frames
   start at slot boundaries, and receive buffers end up to one ``recv`` chunk
   past a slot boundary, so the first frame of a slot was periodically lost.
   Before the fix, the first clean run lost 5 of 823 first transmissions; four
   of them were the first frame of a slot.
   ``NativeOFDM`` now reports busy while the stream holds unscanned samples
   (``FramedStream::holds_samples``). The channelizers still reset on a real
   sequence discontinuity. Test: the channelizer-protocol section of
   ``tests/fullstack/test_native_ofdm.cc``. Before the fix, 20 of 29 frames
   were delivered; after it, all 29. Liquid OFDM delivered all 29 under the
   same test both before and after.

2. **ACKs blocked behind a closed send window** (production ARQ/queue bug).
   When two nodes open at the same time, each sends its SYN and closes its
   send window until the SYN is ACKed. The controller pushes an immediate
   ACK-only packet for the peer's SYN, but ``Queue::canPop`` held back every
   packet to a node with a closed window, the unsequenced ACK included. The
   comments in ``SmartController::getPacket`` say unsequenced packets can
   always be sent. Both ACKs waited about 0.56 s for the SYN retransmission
   timer, and the retransmitted SYN then caused a duplicate delivery
   (defect 3). ``canPop`` now always passes unsequenced packets. The fix applies
   to hardware runs too. Test: ``tests/fullstack/test_net_queue.cc``. After
   the fix, clean bidirectional runs show no retransmissions at start-up and
   no duplicates.

3. **A retransmitted SYN redelivered the first packet** (production ARQ
   bug). ``SmartController::received`` treated every SYN as a new connection,
   resetting the receive window. When a SYN's ACK is lost, the sender
   retransmits the SYN about every 0.56 s, and the receiver delivered
   application packet 0 again each time. That happened twice in ``ack-loss``
   and once in ``asymmetric``. A late duplicate SYN after the window had
   advanced was worse: the reset reopened the window at 0, so ordinary
   duplicates of later packets were delivered again too.

   The receive window now remembers the sequence number and an FNV-1a hash of
   the data bytes of the SYN that opened it (``RecvWindow::syn``). A SYN that
   matches both is a retransmission: the receiver ACKs it again, keeps its
   window and drops it as a duplicate. The hash excludes the extended header
   (which carries the piggybacked ACK) and control messages, which change
   between retransmissions. A SYN with different data still starts a new
   connection, as a restarted sender should.

   Remaining limit: a sender that restarts and sends a first packet
   byte-identical to its previous first packet is taken for a retransmission,
   and that one packet is not delivered. The header has no connection
   identifier or spare flag, so this can't be distinguished without a wire
   format change.

   Test: ``tests/fullstack/test_arq_syn.cc`` drives the real controller with
   a SYN, two retransmissions, more data, a late duplicate SYN, an ordinary
   duplicate, and a restart with different data. Before the fix it delivered
   10 packets; after it, the expected 6. In the rerun, the ``ack-loss``
   receiver saw the SYN three times and ``asymmetric`` twice, with no
   application duplicates. Every scenario now fails on any application
   duplicate.

Known issue (not changed)
-------------------------

**Spurious tail retransmissions.** In the clean runs, the last few packets
before traffic stops are sometimes retransmitted once. Their ACK has no
outgoing data to ride on, so it waits for the delayed-ACK timer and the
peer's next TDMA slot, which together take longer than the adaptive
retransmission timeout learned from piggybacked ACKs. This is tuning, not
loss.

Not done (proposed)
-------------------

* Deterministic virtual-time runs. The MAC, controller and timer queue sleep on
  real time, so this needs a clock and sleep abstraction across them.
* Tap scenarios with more than two nodes, longer and repeated runs, and
  standard tools such as iperf3, which is not installed in the container.
* Jitter, timed grants, queue limits and packet expiry, more than two nodes,
  and the collaboration controller.
* Device gain and AGC modeling, sample-rate conversion between nodes, and
  fractional delay.
* Coded MCS (P3) and OFDMA in the production stack (P5).
