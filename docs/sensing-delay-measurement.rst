Host sensing delay: measurement scope
=====================================

Status 2026-11-09: scope only. Nothing here has run. The measurement steps
that transmit (M3 and M4) each need Kapil's go-ahead with an RF approval
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
     - **Probe and instrumentation.** A small C++ probe, ``tools/sense-delay``,
       built on DragonRadio's ``UHDUSRP``, so ``recv`` behaves as it does in
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
