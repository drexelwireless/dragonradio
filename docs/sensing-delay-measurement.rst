Host sensing delay: measurement scope
=====================================

Status 2026-11-10: M0 to M4 are done; see `M0 and M1 results`_, `M2
results`_, `M3 results`_ and `M4 results`_. On an idle host, with a
1-packet receive chunk, the end-to-end sensing delay is 0.45–0.83 ms at p99
(about 2.2 ms on the X310 at 1 MS/s), so CSMA can reach
:math:`a \approx 0.1`. The measurement steps that
transmit (M3 and M4) each need Kapil's go-ahead with an RF approval
reference, as usual, and none of them runs during OpenISAC.

This scope covers the first item of work package 6 in
:doc:`development-plan-mac-sensing`, "measure the host sensing delay first".
The number it produces decides whether listen-before-talk CSMA can beat ALOHA
on DragonRadio. If it can't, the CSMA lab shows students why. The plan also
lists the delay as an open item for both the N210 and the X310.

What is being measured
----------------------

The *sensing delay* :math:`D` is the window in which a node can't yet hear
that another node has started transmitting. It runs from the first sample of
the other node's burst at the antenna to the first sample of this node's burst,
when this node decides to transmit as soon as it sees a clear channel. It has
four parts:

.. list-table::
   :header-rows: 1
   :widths: 22 48 30

   * - Part
     - Where it comes from in the code
     - How it is measured
   * - :math:`D_\mathrm{rx}`, sample arrival
     - ``UHDUSRP::burstRX`` (``src/USRP.cc``) calls ``recv`` for
       ``rx_max_samps_`` samples with ``one_packet = false``, so it returns
       only when the whole chunk is in. ``rx_max_samps_`` is
       ``8 * get_max_num_samps()`` (``src/USRP.cc:76``). The time to fill
       the chunk, plus transport, is the floor of this part.
     - Device time of each chunk's last sample against host time when
       ``recv`` returns (M1, M2)
   * - :math:`D_\mathrm{det}`, detection
     - Energy over a window of :math:`W` samples. :math:`W/f_s` is the
       observation time, and the compute time is small.
     - Timed in the probe (M1)
   * - :math:`D_\mathrm{tx}`, decision to air
     - ``UHDUSRP::burstTX`` sends timed bursts. The MACs schedule them
       ``slot_send_lead_time`` ahead (5 ms by default). A CSMA burst can be
       modulated before the channel is sensed, so modulation is off this
       path. What is left is host-to-device transport and device buffering,
       for a timed burst or an untimed one.
     - Smallest lead with no ``tx_late`` (M3)
   * - :math:`D`, end to end
     - The sum of the three parts, plus anything they miss
     - Reaction test on a single clock (M4)

Why it matters
--------------

Let :math:`T` be the airtime of one frame and :math:`a = D/T`. These are the
textbook nonpersistent CSMA results (Kleinrock and Tobagi) against slotted
ALOHA's 0.368:

.. list-table::
   :header-rows: 1
   :widths: 20 40 40

   * - :math:`a`
     - Unslotted nonpersistent CSMA, best throughput
     - Slotted nonpersistent CSMA, best throughput
   * - 0.01
     - 0.82
     - 0.87
   * - 0.1
     - 0.52
     - 0.62
   * - 0.2
     - 0.39
     - 0.51
   * - 0.3
     - 0.32
     - 0.44
   * - 0.6
     - 0.21
     - 0.32
   * - 1.0
     - 0.14
     - 0.23

Unslotted CSMA beats slotted ALOHA only when :math:`a` is below about 0.25,
and for a clear teaching result the target is :math:`a \le 0.1`. One
1048-byte native frame at 1 MHz lasts about 8 ms, so the 5 ms send lead alone
gives :math:`a \approx 0.6`, and CSMA would lose to slotted ALOHA there.

The receive chunk may matter more than the send lead. It is
:math:`8 \times` the device's samples per packet. That is typically about 363
samples on the N210 over 1 GbE and about 2000 on the X310 at its default
10 GbE frame size, though both still need checking on the class image's
UHD 4.6. At 1 MS/s, filling one chunk takes about 2.9 ms on the N210 and about
16 ms on the X310. On the X310 at default settings, that alone would make
:math:`a` about 2. The measurement has to find out how far
``rx_max_samps_factor`` can be lowered before overflows start.

Simulation can't measure :math:`D_\mathrm{rx}` or :math:`D_\mathrm{tx}`.
``SimUSRP`` releases received samples after a fixed ``--sim-rx-latency``
(2 ms by default), so a simulated delay is whatever that setting says. The
simulator is used here only to check the probe's logic.

Steps
-----

.. list-table::
   :header-rows: 1
   :widths: 8 42 14 16 20

   * - Step
     - Work
     - Effort
     - Radio use
     - Output
   * - M0
     - **Probe and instrumentation.** A small C++ probe
       (``src/probe/SenseDelay.cc``, run by ``scripts/sense-delay.py``),
       built on DragonRadio's ``USRP`` interface, so ``recv`` behaves as it does in
       the radio. Per chunk, it logs device time and host ``steady_clock``
       time. It calibrates the device-to-host clock offset once, at start,
       from the smallest round trip of repeated ``get_time_now`` calls,
       because ``MonoClock::now`` on hardware is itself a network call. It
       also has an energy detector with window :math:`W` and threshold, and a
       *react* mode that sends a prebuilt burst, either timed at now plus
       lead or untimed, when it detects energy. A ``--set
       rx_max_samps_factor`` option exists already, so the full radio can be
       checked with the same factors.
     - 2–3 days
     - None
     - Probe on both branches: this one and the class-image port
       (``codex/native-radiowars2024``)
   * - M1
     - **Simulation check.** Run the probe and a react pair through
       ``SimUSRP`` at two ``--sim-rx-latency`` settings, and confirm that the
       measured :math:`D_\mathrm{rx}` follows the setting and that react mode
       fires once per burst. Measure detection compute time.
     - 1 day
     - None
     - Proof that the probe works, and :math:`D_\mathrm{det}`
   * - M2
     - **Receive only, N210 and X310.** For each radio type: :math:`f_s` of
       1, 2 and 5 MS/s, ``rx_max_samps_factor`` of 8, 4, 2 and 1, and 60 s
       per cell, on the class image. Record :math:`D_\mathrm{rx}` p50, p99
       and max, overflows (``O``), and host CPU load. Use one node of each
       type from the OTA study's reservation if it has spare time.
     - 1 day
     - Receive only, no transmitting
     - :math:`D_\mathrm{rx}` table, and the smallest factor with no
       overflows
   * - M3
     - **Transmit lead.** Same radios. Sweep the timed-burst lead from 10 ms
       down to 0.5 ms, with 1000 bursts per setting, and count ``tx_late``
       (``EVENT_CODE_TIME_ERROR``). Also time untimed bursts from ``send`` to
       air, measured by the radio's own receive chain. Use 1.322 GHz with low
       TX gain.
     - 1 day
     - TX, needs go-ahead
     - Smallest safe lead per radio
   * - M4
     - **Reaction test, end to end.** Node A sends a short burst, and node B
       reacts with the M0 probe. A receives both its own burst and B's reply
       on its own RX antenna, which works because TX and RX use separate
       antennas. So :math:`D` is measured on A's clock alone, with no shared
       clock between the radios. Run 1000 trials per setting, at the best
       settings from M2 and M3 and at the defaults. N210 pair first, then
       X310.
     - 1–2 days
     - TX on two nodes, needs go-ahead
     - :math:`D` distribution, compared against the sum of the parts

Total: about 1.5 weeks, including writing up the results. M0 and M1 need no
radio time and can start now. M2 needs only a reservation.

M0 and M1 results
-----------------

**The probe (M0).** ``src/probe/SenseDelay.{hh,cc}``, bound to Python as
``runSenseDelay``. It is driven by ``scripts/sense-delay.py``, which runs with
the radio executable. The probe calls ``burstRX`` once per receive chunk
(``rx_max_samps``) and ``burstTX`` exactly as the radio does, and it works with
both ``UHDUSRP`` and ``SimUSRP``; no production code changed. The script
refuses to transmit on hardware without ``--tx-ok`` and caps the TX gain at
20 dB. It prints a JSON summary of the arrival delay, the detector, the
react and ping timings, late bursts, gaps between chunks (overflows) and
clock drift. In simulation (M1):

.. code-block:: bash

   grid/run-sense-delay-sim.sh WORKSPACE OUTDIR [DURATION]

For a hardware sweep (M2, receive only):

.. code-block:: bash

   dragonradio scripts/sense-delay.py --addr addr=ADDR --mode rx --rate 1e6 \
       --rx-max-samps-factor 8 --duration 60 --out rx-f8.json

**The simulation check (M1), 2026-11-10.** gridgpu01, 1 MS/s, 20 s per run,
a 32-sample energy window and 128-sample bursts. In each pair run, node 1
reacts to every edge and node 2 pings every 100 ms, hearing itself through
-10 dB TX leakage. All 12 runs had no receive errors, no late bursts and no
gaps between chunks. Every ping was answered, and one own ping was missed at
the start of one run. Times are milliseconds.

.. list-table::
   :header-rows: 1
   :widths: 30 14 14 14 14 14

   * - Case
     - Chunk
     - :math:`D_\mathrm{rx}` p50
     - Planned :math:`D` p50 (node 1)
     - Measured :math:`D` p50 (node 2)
     - Measured :math:`D` min–max
   * - Receive only, 2 ms latency, factor 1
     - 1.02
     - 2.13
     - –
     - –
     - –
   * - Receive only, 2 ms latency, factor 8
     - 8.19
     - 2.62
     - –
     - –
     - –
   * - Receive only, 10 ms latency, factor 1
     - 1.02
     - 10.13
     - –
     - –
     - –
   * - Receive only, 10 ms latency, factor 8
     - 8.19
     - 10.62
     - –
     - –
     - –
   * - Pair, 2 ms latency, 5 ms lead, factor 1
     - 1.02
     - 2.13
     - 7.67
     - 7.62
     - 7.14–8.16
   * - Pair, 2 ms latency, 5 ms lead, factor 8
     - 8.19
     - 2.62
     - 11.81
     - 11.81
     - 7.78–15.78
   * - Pair, 10 ms latency, 5 ms lead, factor 8
     - 8.19
     - 10.62
     - 19.75
     - 19.71
     - 15.74–23.84
   * - Pair, 2 ms latency, untimed reply, factor 1
     - 1.02
     - 2.13
     - –
     - 4.64
     - 4.16–5.15

What this shows:

* **The probe works.** The arrival delay follows the simulated latency. The
  extra 0.13 ms at factor 1, or 0.6 ms at factor 8, grows with the chunk,
  so it is most likely the time ``SimUSRP`` spends generating the chunk after
  its release time (not checked). The
  end-to-end delay node 2 measures on its own clock agrees with the delay
  node 1 planned to within 0.05 ms at the median. Node 2 sees its own ping
  within 21 µs of the requested time, less than one window, so timed TX and
  the detector are consistent.
* **The model adds up.** :math:`D` is the wait for the rest of the chunk
  (uniform between 0 and one chunk), plus :math:`D_\mathrm{rx}`, plus the
  lead. With factor 8, the range of :math:`D` is a full chunk (8 ms) wide,
  so the p99 pays the whole chunk. On hardware, the chunk matters twice:
  once in the floor of :math:`D_\mathrm{rx}` and once in this spread.
* **Detection costs nothing.** A reply was handed to ``burstTX`` 20–30 µs
  after the edge was seen, and ``burstTX`` returned in under 0.1 ms.

**The class-image port, 2026-11-10.** The probe is on
``codex/native-radiowars2024`` (``ddf31fdf`` and ``87849d2d``, local, not
pushed). It takes a ``Radio``, and the caller passes the receive chunk. It was
built in the class image on grid47 (reservation 3308) and the same 12 runs
were repeated there, all clean. Arrival delay was 2.07 and 2.18 ms at the
2 ms setting (factor 1 and 8), and 10.07 and 10.18 ms at 10 ms. The measured
:math:`D` medians were 7.62, 11.26, 19.33 and 4.58 ms, matching node 1's plan
within 0.02 ms. The simulated device adds less on top of its latency there
than on gridgpu01. In the class image, run the probe as
``VIRTUAL_ENV=TREE/venv TREE/build/dragonradio scripts/sense-delay.py ...``.

These numbers say nothing about real radios: the simulated latency is a
setting. They check the instrument that M2 to M4 use. Raw summaries are in
``simulation-results/sense-delay-m1-20261110`` and
``sense-delay-m1-port-20261110`` (not committed).

M2 plan
-------

**What runs.** The probe in receive-only mode on four nodes at once, each
sweeping its own radio. On every cell the radio streams for 60 s; nothing is
sent. The rx mode never calls ``burstTX``, and the TX gain is set to 0 dB.
That is the same exposure as the earlier no-RF health probes, which also
opened the radios.

.. list-table::
   :header-rows: 1
   :widths: 14 12 36 38

   * - Node
     - Radio
     - Why
     - Cells
   * - grid08
     - N210
     - S1 node, used in every N210 campaign
     - 1, 2, 5 MS/s × factor 8, 4, 2, 1 (12 cells, about 14 min)
   * - grid01
     - N210
     - W1 node, a second N210 host
     - Same
   * - grid44
     - X310
     - FPGA 19.0 at the 2026-11-10 02:19 UTC probe
     - 1, 2, 5, 10 MS/s × factor 8, 4, 2, 1 (16 cells, about 18 min)
   * - grid53
     - X310
     - Healthy in step 3 (2026-11-09)
     - Same

RX gain is 20 dB on the N210s and 10 dB on the X310s, as in the OTA
campaigns.

**How it runs.** On gridgpu01:

.. code-block:: bash

   grid/sense-delay-m2-campaign.sh ~/dragonradio-port/ota-ws/ws-sense-b188d573.tar.gz \
       ~/dragonradio-sense/m2 grid08:N210 grid01:N210 grid44:X310 grid53:X310

Both scripts are on ``codex/native-radiowars2024`` (``b188d573``, local, not
pushed). The tarball is the class-image build from grid47. Its C++ is
unchanged since ``ddf31fdf``, and it passed the M1 check. The campaign script:

* refuses to start while an OpenISAC reservation is active;
* reserves the four nodes for 1 h with the class image and ``--no-flash``,
  and with no DragonRadio service and no traffic;
* checks each radio with ``uhd_usrp_probe``. It skips an X310 that does not
  report FPGA compatibility 19 rather than flashing it, because flashing
  needs your OK;
* installs the tree and runs all nodes at once;
* releases the reservation whatever happens.

Expect about 30 minutes in all.

**What each cell records.**

* Samples per packet, through ``rx_max_samps``. These are the class image's
  values on UHD 3.9.5, which answer the first open question below.
* :math:`D_\mathrm{rx}` p50, p99 and max.
* Gaps between chunks, which are overflows.
* Receive errors.
* Clock round-trip and drift.
* Process CPU as a share of one core, and host load.

**What it decides.** For each radio and rate, the smallest factor with no
gaps, and :math:`D_\mathrm{rx}` there and at the default factor of 8. Those
two numbers set the settings M3 and M4 use, and they give the first hardware
estimate of :math:`a` for the CSMA decision.

**Needs.** Your go-ahead for one 1 h reservation of grid08, grid01, grid44
and grid53. Nothing is transmitted, so no RF approval is involved.

M2 results
----------

**Runs, 2026-11-10, receive only, nothing transmitted.** These ran on grid08
and grid01 (N210, i7-6700 hosts) and grid44 and grid53 (X310, EPYC 7452
hosts), using the class image with UHD 3.9.5, at 1.322 GHz.

* **Reservation 3312.** Every cell stopped before streaming, because opening
  a USRP needs root. Fixed in ``649f201e``, which runs the radio through
  ``sudo -n`` as the OTA runner does.
* **Reservation 3313.** All 56 cells ran, but one chunk in five to ten
  disappeared. The probe asked for each next chunk at a start time that
  floating point sometimes rounded just past its first packet, and
  ``burstRX`` dropped that packet. The arrival delays from this run agree
  with run 2, but its gap counts are wrong. Fixed in ``bdf2801e`` on both
  branches.
* **Reservation 3314** (``bdf2801e``). All 56 cells ran cleanly. Every result
  below comes from this run.

UHD reported no overflow and no receive error in any cell. Samples per
packet were 363 on the N210 (1472-byte frames) and 1996 on the X310
(8000-byte frames), as predicted. The receive chunk at the radio's default
factor of 8 is therefore 2.90 ms on the N210 and 15.97 ms on the X310 at
1 MS/s.

:math:`D_\mathrm{rx}` is measured from the end of a chunk's last sample to
the moment the host has it. In milliseconds:

.. list-table::
   :header-rows: 1
   :widths: 16 14 14 14 14 28

   * - Radio
     - Rate
     - p50
     - p99
     - Max
     - CPU (share of one core)
   * - N210
     - 1 MS/s
     - 0.13
     - 0.16
     - 0.3–3.4
     - 0.13–0.15
   * - N210
     - 2 MS/s
     - 0.11
     - 0.37
     - 0.4–3.2
     - 0.05
   * - N210
     - 5 MS/s
     - 0.14–0.15
     - 0.34
     - 0.6–7.8
     - 0.07–0.09
   * - X310
     - 1 MS/s
     - 0.023
     - 0.03
     - 0.04–0.12
     - 0.008
   * - X310
     - 2, 5, 10 MS/s
     - 0.022–0.026
     - 0.027–0.031
     - 0.04–1.9
     - 0.01–0.06

The median and p99 do not depend on the factor. The largest maxima, a few
milliseconds, are rare and come at factors 1 and 2. On both N210 hosts, the
1 MS/s p99 is lower than at 2 and 5 MS/s; the reason was not looked into.

**Gaps.** No cell lost samples at any factor down to 1, at any rate. The one
exception is an artifact. On grid01 at 5 MS/s, factors 8, 4 and 2 showed
about 5,340 steps of exactly one sample each (one every 11 ms), with no
overflow reported. grid08 at the same rate, and grid01 at factor 1, showed
none. That looks like a timestamp effect on that radio, not lost data, but
it is unexplained.

What this shows:

* **The receive delay is the chunk.** Once a chunk is complete, the host has
  it within 0.15 ms on an N210 and 0.03 ms on an X310 (p99 at most 0.38 ms).
  So :math:`D_\mathrm{rx}` is the time to fill a chunk plus a fraction of a
  millisecond. Factor 1 works on both radios at every rate tried, at least
  while only receiving. That gives a chunk of 0.36 ms on the N210 and 2.0 ms
  on the X310 at 1 MS/s, against 2.9 and 16 ms at the default.
* **Only the X310 at low rates still has a long chunk.** It sends 1996
  samples per packet, so even factor 1 means 2 ms at 1 MS/s. At 5 MS/s
  factor 1 is 0.4 ms. A smaller ``recv_frame_size`` would shorten it at
  1 MS/s; this was not tried.
* **The send lead is now the term to beat.** With factor 1 on an N210 at
  1 MS/s, :math:`D` is about 0.2 ms of receive plus the TX lead (5 ms by
  default). For :math:`a \le 0.1` with an 8 ms frame, :math:`D` must be
  under 0.8 ms, so the lead would have to be under about 0.5 ms. M3
  measures how small it can go.
* **These are idle-host numbers.** In the full radio, demodulation competes
  for the CPU. Also, the frequency-domain channelizer works in blocks of
  67,200 samples, which add about 14 ms. A CSMA sensing path should read
  the receive stream directly, not through that channelizer.

Summaries: ``simulation-results/sense-delay-m2-20261110`` (run 2) and
``sense-delay-m2-run1-20261110`` (run 1), not committed; on gridgpu01,
``~/dragonradio-sense``.

M3 plan
-------

**What runs.** Each node in turn sends a short burst every 20 ms. Each burst
is timed at the device time estimate plus a fixed lead. The node counts the
bursts its radio reports late, and it hears its own bursts on its RX
antenna, which shows whether each one left at the requested time. A late
burst is dropped by the radio, so it is also missing on its own receiver;
the two counts check each other. A 20 s cell has about 975 bursts. The
cells are:

* leads of 10, 5, 3, 2, 1.5, 1, 0.75, 0.5, 0.3, 0.2 and 0.1 ms, plus
  untimed bursts;
* each at 1 and 5 MS/s;

so 24 cells, about 11 minutes per node.

The untimed cells measure the delay from ``burstTX`` returning to the burst
on air. That is the other way a reply can be sent.

**RF.**

* 1.322 GHz.
* TX gain 0 dB (the scripts cap it at 20 dB).
* A 128-sample tone at :math:`f_s/8` with 16-sample ramps, so 125 kHz or
  625 kHz off the carrier. The sample rate is at most 5 MS/s, within the
  5 MHz approval.
* Duty cycle 0.6% at 1 MS/s and 0.13% at 5 MS/s.
* Nodes run one at a time, so no node hears another's bursts.
* A watcher checks every 30 s for an OpenISAC reservation. If one appears,
  every node stops before its next 20 s cell. So at worst about 50 s of
  this low-duty transmission could overlap the start of an OpenISAC run.

**Nodes.** grid08 and grid01 (N210) and grid44 and grid53 (X310), as in M2.
Expect about 50 minutes in a 2 h reservation.

**How it runs.** On gridgpu01:

.. code-block:: bash

   bash ~/dragonradio-sense/bin/grid/sense-delay-m3-campaign.sh \
       ~/dragonradio-port/ota-ws/ws-sense-462db648.tar.gz ~/dragonradio-sense/m3 \
       grid08:N210 grid01:N210 grid44:X310 grid53:X310

The scripts are on ``codex/native-radiowars2024`` at ``462db648``, local and
not pushed. The C++ is unchanged since ``bdf2801e``, the M2 build.

**Checked in simulation.** The simulated radio never reports a timed burst
late, so it cannot answer M3; it only checks the plumbing. There, timed
bursts were seen within 0.02 ms of the requested time at leads down to
0.1 ms. Untimed bursts appeared 1.99 ms after ``burstTX`` returned, which
is the simulated device's 2 ms send latency.

**Limits of the method.**

* The device time estimate is off by up to half a clock round trip, about
  0.07 ms on an N210 and 0.01 ms on an X310. Leads below about 0.1 ms
  therefore measure that error, not the radio.
* The host is idle, as in M2.

**Needs.** Your go-ahead to transmit, under the standing approvals:
1.322 GHz, TX gain at most 20 dB (0 dB used), at most 5 MHz, and no
transmission during OpenISAC.

M3 results
----------

**Run, 2026-11-10, reservation 3316.** The run used grid08 and grid01 (N210)
and grid44 and grid53 (X310), one node at a time, starting at 17:42 UTC and
finishing at 18:23 UTC. RF was 1.322 GHz at 0 dB TX gain, run with Kapil's
go-ahead under the standing approvals. No OpenISAC reservation appeared
during the run, and the reservation was released.

All 96 cells ran, each with about 975 bursts:

* **No burst was late** in any cell, at any lead down to 0.1 ms, at 1 or
  5 MS/s, on either radio. That is about 93,600 bursts with no late report
  and no underflow.
* **Every burst left on time.** Each node heard its own bursts, missing at
  most one per cell. The own-burst edge was within 0.022 ms of the requested
  time at 1 MS/s, and within 0.009 ms at 5 MS/s. That is within one 32-sample
  detection window.
* **Untimed bursts reached the air within 0.06 ms** of ``burstTX``
  returning: 0.015–0.037 ms at the median on the N210, and 0.011–0.027 ms on
  the X310.

grid01 saw extra energy edges in a few cells (up to about 700 in 20 s) that
were not its own bursts. That was probably another transmitter in the
building; another user's reservation (3315) was active. Its own bursts still
matched one for one.

What this shows:

* **The send side costs a fraction of a millisecond.** A lead of 0.1 ms was
  never late. The device time estimate is uncertain by up to half a round
  trip, so the true margin was 0.03–0.17 ms on an N210 and about 0.1 ms on an
  X310. A lead of 0.2 ms, or untimed sending, leaves room for a busier host.
  The radio's default ``slot_send_lead_time`` of 5 ms suits a TDMA slot
  plan; it is not a limit of the hardware.
* **D can be under a millisecond.** Adding the parts from M2 and M3 gives an
  estimate for an idle host, still to be checked end to end by M4: one
  detection window, plus a whole chunk (worst case), plus the p99 arrival
  delay, plus a 0.2 ms lead. :math:`T` is the airtime of one 1048-byte native
  frame (8 ms at 1 MS/s, 1.6 ms at 5 MS/s) or of an 8 ms aggregate.

  .. list-table::
     :header-rows: 1
     :widths: 30 14 14 14 14

     * - Setting
       - :math:`D` (ms)
       - :math:`a`, one frame
       - :math:`a`, 8 ms aggregate
       - Default factor 8, 5 ms lead
     * - N210, 1 MS/s, factor 1
       - 0.76
       - 0.10
       - 0.10
       - :math:`a \approx 1.0`
     * - N210, 5 MS/s, factor 1
       - 0.62
       - 0.39
       - 0.08
       - –
     * - X310, 1 MS/s, factor 1
       - 2.26
       - 0.28
       - 0.28
       - :math:`a \approx 2.6`
     * - X310, 5 MS/s, factor 1
       - 0.64
       - 0.40
       - 0.08
       - –

* **Decision, provisionally "fast enough".** With factor 1 and a short lead,
  the N210 at 1 MS/s reaches :math:`a \approx 0.1` with single frames. Both
  radios reach it at 5 MS/s with frames aggregated to about 8 ms. The X310 at
  1 MS/s is held back by its 1996-sample packets; a smaller receive frame
  size should help there. The radio as configured today (factor 8, 5 ms
  lead) sits at :math:`a \ge 1`, where CSMA loses to ALOHA. That makes a
  good teaching contrast.
* **Still open.**

  * M4 measures :math:`D` end to end on air, without relying on the sum.
  * The full radio adds demodulation load and, through its
    frequency-domain channelizer, about 14 ms. So a CSMA MAC needs its own
    sensing path on the raw receive stream, and must run at factor 1 under
    load.

Summaries: ``simulation-results/sense-delay-m3-20261110`` (not committed);
on gridgpu01, ``~/dragonradio-sense/m3``.

M4 plan
-------

**What runs.** Two nodes of the same type, A and B. A sends a 128-sample
burst every period. B listens with the probe's energy detector and replies
to each burst it detects with the same burst. A hears both its own burst and
B's reply on its own RX antenna, so A measures :math:`D` on its own clock,
from the start of its burst to the start of the reply. No clock is shared
between the nodes. This is the reaction test that M1 checked in simulation.

B reacts with three settings:

.. list-table::
   :header-rows: 1
   :widths: 16 22 22 40

   * - Setting
     - ``rx_max_samps_factor``
     - Reply
     - Why
   * - best
     - 1
     - timed, 0.2 ms ahead
     - The settings that M2 and M3 point to
   * - untimed
     - 1
     - untimed
     - The other way to send a reply; M3 put it on air within 0.06 ms
   * - default
     - 8
     - timed, 5 ms ahead
     - The radio as configured today

Each setting runs at 1 and 5 MS/s, in both directions (A and B swap), on
two pairs:

* N210: grid08 and grid01;
* X310: grid44 and grid53.

These are the M2 and M3 nodes, and the October radio map shows both pairs
hearing each other strongly. That makes 24 cells. A pings for 60 s per cell,
at a period of 60 ms (130 ms for the X310 default at 1 MS/s, so that B's
holdoff after each reply never hides a ping). That is about 1000 trials per
cell, or about 460 for the X310 default at 1 MS/s. A stays at factor 8 and a
5 ms lead, as in M3; its settings don't affect the measurement.

**Link check.** Before each pair's sweep, a 15 s cell at 1 MS/s runs at TX
gain 0 dB, then 10 dB, then 20 dB, stopping at the first gain where A gets a
reply to at least 95% of its pings. The whole pair then uses that gain. If
20 dB is not enough, the pair is skipped.

**RF.**

* 1.322 GHz.
* TX gain 0, 10 or 20 dB, from the link check (the scripts cap it at
  20 dB).
* The M3 burst: 128 samples at :math:`f_s/8` with 16-sample ramps, at most
  5 MS/s, within the 5 MHz approval.
* Each node sends one burst per period: duty cycle 0.21% at 1 MS/s and
  0.04% at 5 MS/s.
* One pair transmits at a time; the other pair is idle.
* The OpenISAC watcher from M3 checks every 30 s, and no cell starts after
  it sees an OpenISAC reservation. A cell lasts about 85 s, so at worst
  about 2 minutes of these bursts could overlap the start of an OpenISAC
  run.

**What it measures.**

* :math:`D` p50, p99 and max for each cell, and :math:`a` for one frame and
  for an 8 ms aggregate.
* :math:`D` against B's own estimate of its delay, from the edge it
  detected to the reply. This checks the probe's clock mapping on hardware.
* :math:`D` against the sum of the parts in the M3 results table: 0.76 ms
  (N210, 1 MS/s), 0.62 ms (N210, 5 MS/s), 2.26 ms (X310, 1 MS/s) and
  0.64 ms (X310, 5 MS/s) for the best setting. For the default setting, the
  same sum with the factor-8 chunk and the 5 ms lead gives about 8.3 ms
  (N210) and 21 ms (X310) at 1 MS/s.
* Overflows, late bursts and missed replies on both nodes.

**Time.** About 20 minutes per pair, plus the radio checks: about 50
minutes in a 2 h reservation.

**How it runs.** On gridgpu01, with the tree tarball ``ws-sense-f9dea594``, which adds the M4 scripts
to the M3 build (the C++ is unchanged since ``bdf2801e``):

.. code-block:: bash

   bash ~/dragonradio-sense/bin/grid/sense-delay-m4-campaign.sh \
       ~/dragonradio-port/ota-ws/ws-sense-f9dea594.tar.gz ~/dragonradio-sense/m4 \
       grid08:grid01:N210 grid44:grid53:X310
   python3 ~/dragonradio-sense/bin/grid/sense-delay-m4-table.py ~/dragonradio-sense/m4

The scripts are on ``codex/native-radiowars2024`` at ``f9dea594``, local and not pushed:

* ``grid/sense-delay-m4-campaign.sh`` runs from gridgpu01;
* ``grid/run-sense-delay-m4-cell.sh`` runs one side of one cell on a node;
* ``grid/sense-delay-m4-table.py`` tabulates the results.

A dry run of the campaign (``DRY=1``, nothing reserved or sent) produced
the expected cells and commands. The table script reproduces the M1
simulation result: timed replies within 0.015 ms of B's own estimate, and
untimed replies 2 ms later, which is the simulated device's send latency.

**Limits of the method.**

* Edges are found to within one 32-sample window: 0.032 ms at 1 MS/s,
  0.006 ms at 5 MS/s.
* A takes the first edge after its own burst as the reply. Stray energy,
  like what grid01 saw in M3, can occasionally be taken for a reply, or can
  put B in holdoff so that a ping is missed. Both show up as outliers or as
  missed replies.
* The hosts are idle, as in M2 and M3.

**Needs.** Your go-ahead to transmit on two nodes at once, under the
standing approvals: 1.322 GHz, TX gain at most 20 dB, at most 5 MHz, and no
transmission during OpenISAC. Also your OK for a 2 h reservation of grid08,
grid01, grid44 and grid53 (no flash).

M4 results
----------

**Run, 2026-11-10, reservation 3320.** The pairs were grid08 and grid01
(N210) and grid44 and grid53 (X310), one pair at a time, from 21:45 to
22:21 UTC. RF was 1.322 GHz, with Kapil's go-ahead under the standing
approvals. Both link checks passed at TX gain 0 dB, with replies to all 241
pings, so every cell ran at 0 dB. No OpenISAC reservation appeared, and the
reservation was released.

All 24 cells ran, with 991 pings each (457 for the X310 default at 1 MS/s).
There were no late bursts and no underflows on any node. The two directions
of each pair agree within 0.03 ms at p50 and p99, so the table gives the
larger value of the two.

.. list-table:: End-to-end sensing delay :math:`D`, measured on the pinging node's clock
   :header-rows: 1
   :widths: 22 14 12 12 14 13 13

   * - Radio, rate
     - B's setting
     - :math:`D` p50 (ms)
     - :math:`D` p99 (ms)
     - M3 estimate (ms)
     - :math:`a`, one frame
     - :math:`a`, 8 ms aggregate
   * - N210, 1 MS/s
     - best
     - 0.64
     - 0.83
     - 0.76
     - 0.10
     - 0.10
   * -
     - untimed
     - 0.48
     - 0.67
     - –
     - 0.08
     - 0.08
   * -
     - default
     - 6.66
     - 8.10
     - 8.3
     - 1.01
     - 1.01
   * - N210, 5 MS/s
     - best
     - 0.40
     - 0.60
     - 0.62
     - 0.37
     - 0.08
   * -
     - untimed
     - 0.27
     - 0.51
     - –
     - 0.31
     - 0.06
   * -
     - default
     - 5.48
     - 5.89
     - –
     - 3.66
     - 0.74
   * - X310, 1 MS/s
     - best
     - 1.28
     - 2.27
     - 2.26
     - 0.28
     - 0.28
   * -
     - untimed
     - 1.15
     - 2.08
     - –
     - 0.26
     - 0.26
   * -
     - default
     - 13.06
     - 20.90
     - 21
     - 2.60
     - 2.61
   * - X310, 5 MS/s
     - best
     - 0.45
     - 0.65
     - 0.64
     - 0.40
     - 0.08
   * -
     - untimed
     - 0.26
     - 0.45
     - –
     - 0.28
     - 0.06
   * -
     - default
     - 6.68
     - 8.22
     - –
     - 5.11
     - 1.03

Settings: *best* is ``rx_max_samps_factor`` 1 with a timed reply 0.2 ms
ahead; *untimed* is factor 1 with an untimed reply; *default* is factor 8
with a 5 ms lead. :math:`a` uses :math:`D` p99, with one 8046-sample native
frame (8 ms at 1 MS/s, 1.6 ms at 5 MS/s) or an 8 ms aggregate.

What this shows:

* **The parts add up.** The sum of the M2 and M3 parts predicted :math:`D`
  p99 within 0.07 ms in every cell where it was computed, defaults
  included. Each reacting node's own estimate of its delay, from the edge it
  detected to the reply, matched the delay measured on the other node within
  0.03 ms at p50 for timed replies. For untimed replies the gap was
  0.01–0.08 ms, which is the send-to-air delay M3 measured.
* **Untimed replies are fastest**, about 0.15 ms ahead of a timed reply with
  a 0.2 ms lead. That matters most at 5 MS/s.
* **What remains of** :math:`D` **is the receive chunk.** From p50 to p99,
  :math:`D` spreads over about one chunk: 0.36 ms on the N210 at 1 MS/s,
  2 ms on the X310 at 1 MS/s.
* **Decision: "fast enough", confirmed on air** for an idle host. With
  factor 1, :math:`a` at p99 is 0.08–0.10 on the N210 at 1 MS/s with single
  frames, and 0.06–0.08 on both radios at 5 MS/s with 8 ms aggregates. The
  X310 at 1 MS/s is marginal (0.26–0.28) because of its 1996-sample
  packets. The radio's defaults give :math:`a` of 1 to 5, where CSMA loses
  to slotted ALOHA.

Things to know about the data:

* In the X310 untimed cells at 1 MS/s, about 4% of pings show no reply,
  though B replied to every ping. Those replies started within about
  0.07 ms of the end of A's own 0.128 ms burst, so A's detector saw one
  edge, not two. These are the fastest replies, so the low tail of these
  two cells is cut off below about 0.2 ms, and p50 reads slightly high.
  p99 is not affected.
* grid01 at 5 MS/s with factor 8 again showed one-sample timestamp steps
  (7304 in 60 s), the artifact seen in M2, with no effect on :math:`D`.
* One untimed reply on the N210 at 5 MS/s took 5.8 ms; that cell's p99 is
  0.48–0.51 ms.

Still open:

* These are idle hosts. A CSMA MAC needs its own sensing path on the raw
  receive stream, ahead of the 14 ms channelizer, and has to hold factor 1
  under demodulation load.
* A smaller receive frame size on the X310 should bring the 1 MS/s case
  down toward the N210's.

Summaries and logs: ``simulation-results/sense-delay-m4-20261110`` (not
committed), with ``m4-table.csv``; on gridgpu01, ``~/dragonradio-sense/m4``.
The table comes from ``grid/sense-delay-m4-table.py`` on
``codex/native-radiowars2024`` (``5d0b2c83``).

Result and decision
-------------------

The result page will give :math:`D` (p50 and p99) for each radio at each
setting, and the frame length needed for :math:`a = 0.1` and :math:`a = 0.25`
at 1, 2 and 5 MHz. Work package 6 then picks one of three paths:

* **Fast enough:** p99 :math:`a \le 0.1` with frames of reasonable length
  (aggregated frames of up to about 50 ms). Build the CSMA MAC as planned and expect it
  to beat ALOHA.
* **Marginal:** :math:`0.1 < a \le 0.3`. Build it, and plan the lab around the sweep:
  students lengthen frames and watch CSMA overtake ALOHA.
* **Too slow:** :math:`a > 0.3` at every practical setting. Build only the minimal MAC
  for a lab that shows the failure, and move the effort to sensing for
  dynamic spectrum access (packages 5 and 7), which works at much slower
  timescales.

Knobs to try if :math:`D` is too large
--------------------------------------

* A lower ``rx_max_samps_factor``, or ``one_packet = true`` in ``recv``.
  This trades overflow risk for latency, and M2 measures the trade.
* A smaller receive frame size on the X310 (``recv_frame_size``).
* Real-time priority and CPU pinning for the receive and transmit threads.
* Longer frames by aggregation, or a narrower bandwidth. These don't shorten
  :math:`D`, but they lower :math:`a`.

Open questions
--------------

* Whether factor 1 still holds with the full radio's demodulation load.
* Whether the channelizer starts on a partly filled buffer as soon as each
  ``recv`` chunk arrives. ``MAC::rxWorker`` pushes the buffer before it
  fills, and M1 should confirm that the full radio adds nothing beyond the
  chunk.
