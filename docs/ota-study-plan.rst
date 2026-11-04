OTA study plan: native OFDM, liquid OFDM and OFDMA on shared-PPS N210s
========================================================================

Status 2026-11-04: plan only. Nothing in it has been reserved or
transmitted. Each RF phase starts only on Kapil's go-ahead, with an RF
approval reference for this experiment. The tooling it builds on is
:doc:`ota-fullstack-grid`. The default changes it runs with are in
:doc:`defaults-2026-11`.

Questions
---------

1. Does the production stack (MAC, ARQ, queues, tap, kernel UDP/TCP) run
   cleanly over the air with each PHY? This means no late bursts,
   underflows or missed slots on the node CPUs, with PPS-aligned TDMA slots.
2. On the same links and at the same transmit power, how do native OFDM and
   liquid OFDM compare? The measures are delivery, latency, goodput, TCP
   request/response time and ARQ retransmissions.
3. How does the answer to 2 change between a link with a large margin and a
   link near the modulation and coding threshold?
4. Later phase: for a base station with one strong and one weak user,
   does native OFDMA beat TDMA with either PHY? This is the over-the-air
   version of the ``ofdma-strong-weak`` simulation scenario.

Nodes: the shared-PPS N210s
---------------------------

Every ceiling N210 except grid02 and grid20 is cabled to the OctoClock
(10 MHz and PPS). grid07 (weak receive path) and grid11 (no radio found)
are out. That leaves 16 nodes:

  grid01, 03, 04, 05, 06, 08, 09, 10, 12, 13, 14, 15, 16, 17, 18, 19

This matches the OpenISAC receive-only sync check of 2026-11-01 and
2026-11-02 (``openisac-grid/data/openisac-ota/run-20261102/sync.json``).
Each of these 16 locked to the external 10 MHz and saw PPS edges once per
second. grid02 locked its reference but saw no PPS. grid20 saw no PPS.

All 16 are N210r4 with SBX daughterboards (radio map node records). Grid
convention puts the radio at ``addr=192.168.10.2``. TX gain range is 0–31.5
dB; the campaign caps it at 20 dB.

Strong and weak links
---------------------

**Source.** The grid-orchestrator radio map
(``~/grid-orchestrator/state/radiomap/radiomap.json`` on the shared home,
``grid-radiomap show``). It holds one survey, ``survey-20261025-183107``,
at 1.3 GHz. Every node in turn transmitted a 100 kHz tone (amplitude 0.3,
TX gain 15 dB) while the others captured at RX gain 15 dB and 1 MS/s.

**What the SNR means.** ``snr_db`` is the tone's peak FFT bin over the
median noise bin, with 65,536 bins and a Hann window. An SNR over the
radio's 1 MHz band is therefore about ``snr_db − 46 dB`` at the same gains.
That holds before the radios' own EVM floor, and assumes the radio's
waveform has the same RMS level as the tone (Phase 1 measures both).

**Spread across the 16 nodes.** 102 of 120 pairs were measured both ways.
The other 18 lost captures in the survey; they are not unreachable. The
weaker direction of a pair ranges from 74 to 103 dB tone SNR, which is
roughly 28 to 57 dB over 1 MHz at 15 dB TX gain. Every node hears every
other: the 16 form one collision domain with no hidden terminals.

**Two factors.** A "weak" pair here still has a large margin for QPSK at
15 dB TX gain. So the study separates two things:

* **Link class**, from the map. *Strong* pairs are in the top tenth (both
  directions ≥ 98.5 dB) with less than 3 dB asymmetry. *Weak* pairs are in
  the bottom tenth (≤ 82 dB) with less than 6 dB asymmetry, so that the
  two directions are alike. Within each class, pairs share no node.
  grid15 is the weak end of many pairs; its receive side is weaker than
  its transmit side, and its TX/RX port showed the strongest local line in
  the 2026-11-02 scan. It is used once, in W1.
* **Operating point**, set by TX gain:

  * *Nominal*: 10 dB, the DragonRadio lab default. Strong links saturated
    at 20 dB.
  * *Low margin*: the gain that puts the measured in-band SNR about 6 dB
    above the QPSK ``rs8`` threshold on that pair, from the Phase 1
    calibration.

  The "disconnected" and "weak" conditions of the simulation can only be
  reached this way.

.. list-table:: Pairs, weaker direction / asymmetry in dB of tone SNR (map of 2026-10-25)
   :header-rows: 1

   * - Role
     - Pair
     - Map SNR
     - Use
   * - S1 strong
     - grid04 – grid08
     - 102.6 / 1.3
     - tonight
   * - W1 weak
     - grid01 – grid15
     - 77.6 / 4.2
     - tonight
   * - S2 strong
     - grid06 – grid19
     - 100.7 / 1.0
     - replication
   * - W2 weak
     - grid09 – grid12
     - 81.4 / 5.6
     - replication
   * - spares
     - strong grid09 – grid13 (100.2); weak grid05 – grid16 (83.6 / 1.3)
     -
     - if a node fails health
   * - T1 OFDMA triad
     - BS grid09; strong UE grid13 (100.2); weak UE grid12 (81.4); UEs
       88.7 apart
     -
     - Phase 4

The map is ten days old, from one survey. Phase 1 measures every link used
with the radio's own per-packet SNR and EVM, and those measurements, not the
map, set the low-margin gains.

Frequency
---------

The map, the class DragonRadio labs (1.3XX GHz, 500 kHz channels) and
OpenISAC all used 1.3 GHz. The OpenISAC receive-only scan of 2026-11-02
(``run-20261102/scan_report.md``, 18 nodes, 21:37–22:32) graded every 20 MHz
sub-band from 1250 to 1350 MHz as BUSY. It saw bursts (aeronautical radar)
and narrow lines (Galileo E6 near 1278 MHz), and found no all-night CLEAR
candidate. The quietest were 1320 MHz (busy 5.4% of sweeps at the 95th
percentile) and 1280 MHz.

The trials therefore carry radar bursts as a nuisance. The design handles
them in four ways:

* blocks randomize the order of PHY and condition in time;
* the radios log every received frame, so bursts show as header errors at
  both PHYs together;
* PHYs are compared within a block, never across nights;
* a 10 s receive-only capture at each node before each block records the
  interference that was present.

The frequency and its approval are Kapil's decision (see
`Decisions needed`_).

Design
------

* **PHYs.** Native OFDM (``--phy native-ofdm``) and liquid OFDM
  (``--phy ofdm``). Both use ``-c rs8 -k none -m qpsk``, 1 MS/s, M = 48,
  TDMA with ARQ and the new defaults (retransmission limit 8). Both transmit
  at the same average power (D3).
* **Clocks.** ``--clock-source external --time-source external
  --sync-time-to-pps`` on every node, with timesync HELLOs still running.
* **Traffic per trial (60 s).** UDP 40 × 500 B/s each way, plus 100 TCP
  request/response exchanges, as in the first-campaign template.
* **Blocking.** Each block runs every cell (pair × operating point) with
  both PHYs, in a seeded random order. Native and liquid are paired within
  a block and cell. Tonight: 2 pairs × 2 operating points × 2 PHYs = 8
  trials per block, 3 blocks, 24 trials, about 3.5 min each, about 1.5 h.
  The replication night adds S2 and W2 the same way, which gives 6 blocks
  per class across two pairs.
* **Analysis.** Per cell: native − liquid differences by block (median and
  range), shown with every block. Three blocks per pair is a pilot, so
  results are reported as observed, with no significance claims. The
  replication night is what supports a claim per link class.

Metrics
-------

The runner and ``scripts/summarize-fullstack.py`` already compute:

* UDP delivery ratio and latency each way;
* TCP request/response completion and round-trip times;
* goodput;
* ARQ retransmissions and drops at the limit;
* continuity counts: late TX, underflows, RX errors, missed slot deadlines.

D5 adds three more, from the HDF5 logs every node already writes:

* per-frame SNR, EVM and RSSI;
* header and payload CRC failures;
* the PPS sync result (``syncTimeToPPS`` fraction) and the residual
  timesync offset.

**Validity.** A trial with any continuity error is reported but kept out
of the PHY comparison, as the runner already marks it. Two invalid trials
in a row stop the campaign and stop the reservation.

Phases and gates
----------------

.. list-table::
   :header-rows: 1

   * - Phase
     - RF
     - What
     - Gate to the next phase
   * - 0 rehearsal (``STOP_BEFORE=5``)
     - no
     - Reserve grid04, 08, 01, 15 (dragonradio/ubuntu2404 image, no
       flash). Install the workspace, run the health checks (including D2:
       external reference locked, PPS edges present, UHD 4.6 accepts the
       N210 FPGA), and run the simulation gate on a node's CPU. Stop the
       reservation. About 40 min.
     - All four healthy. If UHD 4.6 rejects the N210 images, stop: flashing
       needs separate approval.
   * - 1 shakedown and calibration
     - yes, about 25 min on air
     - On S1 at 10 dB: one 60 s trial per PHY, checking PPS sync on both
       nodes and zero continuity errors. Then a TX gain sweep (0, 5, 10,
       15, 20 dB; 30 s each; both PHYs) on S1 and W1. It records in-band
       SNR and EVM against gain and fits the map-to-link offset. Output:
       the low-margin gain per pair and PHY-equal power confirmed on air.
     - Both PHYs deliver ≥ 99% UDP on S1 at 10 dB with no continuity
       errors. If not, stop and debug with the logs.
   * - 2a main, tonight
     - yes, about 1.5 h
     - S1 and W1 at nominal and low margin, 3 blocks (24 trials).
     - Invalid-trial stop rule not hit.
   * - 2b replication
     - yes, about 1.5 h
     - S2 and W2, same design, another night.
     - —
   * - 3 higher MCS
     - yes
     - The nominal cells with 16-QAM (and a higher code rate) for
       throughput, after the QPSK results.
     - —
   * - 4 OFDMA triad
     - yes
     - T1: native OFDMA against TDMA native and TDMA liquid, with the weak UE
       at nominal and low margin. Needs D6.
     - —
   * - 5 without shared clock
     - yes
     - S1 and W1 with internal references, timesync only: the condition
       the radio meets off the grid.
     - —

Development before the first RF phase (no RF)
---------------------------------------------

These run on gridgpu01 and in simulation. D1–D5 are for tonight. About 1.5–2
hours, starting on the go-ahead.

* **D1. Cells in one reservation.** The campaign gains a ``cells`` list
  (nodes, TX gain and strata per cell). The pipeline reserves the union of
  the nodes once, and blocks randomize over cells × PHYs. Today one campaign
  is one node pair at one gain, so each cell would need its own reservation.
* **D2. Reference and PPS in health and in the radio.**
  ``syncTimeToPPS`` also requires the ``ref_locked`` sensor when the clock
  source is external. The stage-3 health check runs the same lock and PPS
  check read-only, like the OpenISAC ``sync.json``.
* **D3. Equal transmit power.** Measure each PHY's average sample power in
  simulation at the trial settings. Set ``--soft-tx-gain`` per PHY so both
  transmit the same average power with a fixed headroom. Record it in the
  plan files and check it on air in Phase 1.
* **D4. N210 campaign files.** Pairs and gains from this plan, at
  ``addr=192.168.10.2``.
* **D5. Calibration and link metrics.** A sweep mode in the campaign (one
  short trial per gain). A summary of per-frame SNR, EVM, RSSI and CRC from
  the HDF5 logs, added to ``summarize-fullstack.py``. A pre-block
  receive-only capture.
* **D6 (Phase 4).** Three-node plans with base-station and user roles, and
  ``native-ofdma`` in the runner's PHY list.

Each item gets a test in ``tests/simulation`` (mock grid) or
``tests/fullstack``. The rehearsal of Phase 0 is the end-to-end check.

Decisions needed
----------------

* **RF approval** for this experiment: reference, frequency (see
  `Frequency`_), and the TX gain cap. The proposed cap is 20 dB; the
  nominal gain is 10 dB.
* **Tonight's scope**: Phase 0, 1 and 2a, reserving four N210s (grid01, 04,
  08, 15) for about 4 hours.
* **Go** for D1–D5, then for each RF phase.

Risks
-----

* **UHD 4.6 and the N210 images.** The image's N210 FPGA and firmware were
  last used with UHD 3.9.5 (class image) and 4.2 (OpenISAC). The Phase 0
  probe answers this without transmitting. Nothing is flashed.
* **Node CPUs.** The node containers' CPUs are unknown. The simulation gate
  and the Phase 1 continuity counts show whether 1 MS/s keeps up.
* **Map age.** Mitigated by measuring every used link in Phase 1.
* **Band occupancy at 1.3 GHz.** See `Frequency`_.
