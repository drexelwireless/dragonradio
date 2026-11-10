Host sensing delay: measurement scope
=====================================

Status 2026-11-10: M0 (the probe) and M1 (the simulation check) are done on
this branch and on the class-image port branch; see `M0 and M1 results`_.
M2 to M4 have not run. The measurement steps that
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

M2 plan (ready, not run)
------------------------

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

* The samples per packet that UHD 4.6 reports on the class image for each
  radio type. M2 logs them.
* Whether the grid's node CPUs can keep up at factor 1 and 5 MS/s without
  overflows.
* Whether the channelizer starts on a partly filled buffer as soon as each
  ``recv`` chunk arrives. ``MAC::rxWorker`` pushes the buffer before it
  fills, and M1 should confirm that the full radio adds nothing beyond the
  chunk.
