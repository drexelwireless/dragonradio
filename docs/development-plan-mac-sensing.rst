Development plan: MAC techniques, spectrum sensing and teaching
================================================================

Status 2026-11-06: plan only. None of this work has started, and nothing here
changes the OTA study in :doc:`ota-study-plan`. Every RF phase below needs its
own go-ahead from Kapil, with an RF approval reference, as usual.

Effort figures are engineering judgement for one strong graduate student,
including over-the-air validation. They are not measured, and they should be
revised after the first work package.

Source paths refer to the class-image port, branch
``codex/native-radiowars2024``, which is built on ``wip/radiowars2024``. That
is the code base this plan extends.

Goal
----

Turn DragonRadio with the native OFDM and OFDMA PHYs into a platform for
teaching and research on multiple access and spectrum use. It should support
CSMA, FDMA, spectrum sensing and related techniques at reasonable performance.
OFDMA stays in the plan for this purpose even where it is not the fastest
option today.

Assessment
----------

Most of this is achievable, and part of it already exists. The real exception
is fast, Wi-Fi-style CSMA, which host-based SDR timing limits.

.. list-table::
   :header-rows: 1
   :widths: 24 38 14 12 12

   * - Capability
     - Starting point
     - Effort
     - Teaching
     - Research
   * - Slotted ALOHA, FDMA
     - Both MACs exist (``src/mac/SlottedALOHA.cc``, ``src/mac/FDMA.cc``).
       Neither has been through an OTA campaign with the native PHYs.
     - 2–4 weeks
     - High
     - Low
   * - Spectrum sensing (PSD, occupancy)
     - IQ snapshot collector exists (``src/mac/Snapshot.cc``). Live sensing
       does not.
     - 3–5 weeks
     - High
     - Medium
   * - Listen-before-talk CSMA
     - No carrier sensing in the MACs. Slot and burst plumbing exists.
     - 4–8 weeks
     - High
     - Low
   * - Dynamic spectrum access
     - Python control plane and older SC2 code (``channels.py``,
       ``schedule.py``, ``collab.py``, ``sc2/``).
     - 2–3 months
     - High
     - High
   * - Hybrid ARQ, OFDMA schedulers
     - The native PHYs expose internals that liquid hides.
     - 1–3 months
     - Medium
     - High
   * - Uplink OFDMA
     - Needs arrival-time and carrier-offset alignment across users. X310s
       have no shared clock.
     - 3–6 months
     - Medium
     - High
   * - Simulation notebooks for students
     - Harness, scenarios and class companion notebooks exist. No DragonRadio
       notebook has been built or tried.
     - 6–8 weeks for the first five
     - High
     - Low
   * - Microsecond CSMA/CA with RTS/CTS
     - Needs FPGA work. Not recommended.
     - 6+ months
     - Low
     - Medium

Where the PHYs stand
--------------------

Parity with liquid comes first, because every MAC result below depends on it.

**Over the air, 2026-11-06.** X310s at 1.322 GHz and 1 MHz, TX gain 5 dB, RX
gain 10 dB, native QPSK rs8 with commit ``2f9c2fe4`` (FFT window backoff and
centred channel model). Each cell is one 60 s trial with 2400 UDP packets
offered in each direction.

.. list-table::
   :header-rows: 1

   * - Link
     - Before the fix
     - After the fix
   * - grid62 to grid57, native
     - 0 / 2400
     - 2400 / 2400
   * - grid59 to grid57, native
     - 105 / 2400
     - 2400 / 2400
   * - grid59 and grid62 both ways, native (control)
     - 2400 / 2400
     - 2400 / 2400
   * - grid57 and grid62 both ways, liquid (control)
     - not in that run
     - 2400 / 2400

The liquid trial delivered everything but failed the continuity check, with
two receive errors on node 1. This is a single trial per cell, not a
statistical result, and retransmissions have not yet been compared.

**In simulation, native with per-symbol phase tracking.** This change is not
yet committed to the port branch and has not run over the air: its OTA trial
stopped before transmitting, because the grid59 X310 stopped answering
(``x300 fw poke32 - reply timed out``). All 15 fullstack test programs and the
clean-arq scenario pass with it.

* Native with tracking is ahead of liquid near the coding threshold: at 8 dB,
  1048-byte frames, 160 of 160 delivered against 80 of 160.
* Tracking costs a little at the edge of the threshold: at 7 dB, native
  delivers 77 of 160 with it and 116 without, where liquid delivers none.
* Tracking closes the phase-noise gap up to 300 Hz Wiener linewidth: 1048-byte
  frames at 25 dB SNR go from 0 of 40 to 40 of 40 at 100 and 300 Hz, level
  with liquid.
* At 1 kHz linewidth liquid is still ahead, 36 of 40 against 2 of 40. That is
  far more phase noise than the X310 tone tests showed. N210s have not been
  measured.

Constraints that shape the plan
-------------------------------

**Sensing delay against frame length.** The default ``slot_send_lead_time`` is
5 ms. A 1048-byte native frame at 1 MHz lasts about 8 ms. CSMA beats ALOHA only
when the sensing delay is much shorter than a frame, and here the ratio is
about 0.6. The true minimum delay of the host path has not been measured, and
it is the first number work package 6 needs. Three ways to improve the ratio
are longer aggregated frames, narrower bandwidth and a tuned host path. An
FPGA detector would change the picture but is a different scale of project.

**Simulator.** The simulated device already models overlap. Each receiver in
``src/sim/SimUSRP.cc`` adds every overlapping burst to its sample stream, with
per-link gain, carrier offset, delay, sample clock offset and noise, so
collisions and capture happen physically, and per-link gains should be enough
to set up hidden terminals. ``scripts/sim-air.py`` is only the relay between
nodes. What is missing from the device is multipath, phase noise and an
interferer source; those exist only in test code. Simulation runs in real time
on the host clock, which matters for sweeps and for shared classroom machines.

**Validation.** Simulation passed while the grid57 links failed over the air.
The cause turned out to be native's FFT window timing, exposed by fractional
delay and by the channel shape into grid57, which has a 9 dB in-band droop.
The test that reproduces it was written only after the OTA failure. Plan for
about half of each work package to go to OTA validation.

**Collisions and acquisition.** Unslotted MACs stress native's acquisition
with overlapping frames, which has not been tested.

**Radios.** X310s are the right platform for wider bandwidths, but they have
no shared clock. FDMA and ALOHA need no shared clock, which suits them. TX and
RX use different antennas, so links are not reciprocal; this helps a
hidden-terminal lab but limits channel-reciprocity methods. The X310s on
grid44 and grid61 need a power cycle, and grid59 now needs checking.

**Timing scale.** Host-based timing rules out competing on fast MAC design.
The platform's value is cross-layer work at millisecond timescales.

Work packages
-------------

Each package ends in an OTA result and a documentation page. Later packages
depend on earlier ones.

1. **Finish native against liquid** (in progress). Check the grid59 X310.
   Rerun tracking OTA, commit it if it holds up, and complete the comparison
   in :doc:`ota-study-plan`, including retransmissions. *Done when:* native
   and liquid are compared over strong and weak links with enough trials to
   state confidence.

2. **Simulator realism** (1–2 weeks). Add multipath, phase noise and an
   interferer source to the simulated device. *Done when:* simulated ALOHA
   throughput matches the analytic curve across offered load, one collision
   scenario is checked against an OTA run, and a fractional-delay case
   reproduces the grid57 failure in the full stack.

3. **Notebook foundation** (2–3 weeks, then 3–5 days per notebook). See
   `Simulation notebooks`_. *Done when:* the first notebook runs from a clean
   class image, live and headless.

4. **ALOHA and FDMA with native PHYs** (2–4 weeks). Add simulation scenarios
   and run both MACs over the air with native OFDM and liquid. Check guard
   bands against the measured carrier offsets of a few hundred hertz. *Done
   when:* throughput against offered load is measured for both MACs against
   TDMA on the same links.

5. **Spectrum sensing** (3–5 weeks). Add a live PSD and per-channel occupancy
   from the receive stream, logged and available in Python. Measure detection
   probability against false alarm rate. Decide how to sense while
   transmitting; TDMA guard gaps are the simple answer. *Done when:* a Python
   controller can read occupancy, and detection performance is measured
   against a known signal.

6. **Listen-before-talk CSMA** (4–8 weeks). Measure the host sensing delay
   first. Add a MAC with energy-based sensing and random backoff. Sweep the
   ratio of sensing delay to frame length. *Done when:* measured throughput
   follows the theoretical trend across that sweep, whether or not it beats
   ALOHA at every setting.

7. **Dynamic spectrum access** (2–3 months). A Python controller picks
   channels from sensing data and coordinates over a control channel, reusing
   the SC2 code where it still fits. *Done when:* a link moves away from an
   interferer and recovers, with the recovery time measured.

8. **Research tracks** (1–3 months each, chosen after package 5). See below.

Simulation notebooks
--------------------

The goal is for students to learn each technique in simulation before using
the radios over the air. The simulator runs the production MAC, ARQ and PHY
code with only the radio device simulated, so students use real DragonRadio,
not a teaching model. The notebooks extend the class's existing companion
notebooks (``sdr-lab-class``).

.. list-table::
   :header-rows: 1
   :widths: 40 30 30

   * - Notebook
     - Status today
     - Work needed
   * - OFDM PHY: EVM, BER, impairments, native against liquid
     - Mostly ready
     - Multipath and phase noise for the full lesson (package 2)
   * - TDMA, ARQ, queues, rate adaptation
     - Scenarios exist
     - Notebook only
   * - OFDMA with strong and weak users
     - Scenarios exist
     - Notebook only
   * - ALOHA, FDMA
     - MACs exist, no scenarios
     - Days each, assuming they work with native
   * - Spectrum sensing
     - Can start offline in NumPy on recorded IQ
     - Live version needs package 5
   * - CSMA
     - Needs the new MAC
     - Package 6
   * - Dynamic spectrum access
     - Needs sensing and the controller
     - Package 7

**Design.** Each notebook launches the existing harness
(``scripts/sim-fullstack.py``), then plots its summaries and logs. A small
shared helper library handles running, caching and plotting. Importing
DragonRadio into a Jupyter kernel directly has not been verified, so the design
does not depend on it.

**Constraints.**

* Runs happen in real time, about 30–60 s each, so a 10-point sweep takes 5–10
  minutes. Students run one case live and load precomputed results for sweeps.
* Real-time runs need dedicated CPU. Thirty students on one server could fail
  in confusing ways. Use per-student containers on grid nodes, or measure
  server capacity first.
* The harness runs on Linux, so notebooks run inside the class image. Whether
  Jupyter is installed in that image needs confirming.

**Simulation and over the air together.** The same scenario definition should
run in both, so students compare the two directly. The grid57 failure is a
ready-made example: it passed simulation and failed on real radios. Notebooks
can also run headless as regression tests.

Research directions
-------------------

These depend on owning the PHY, which liquid makes hard.

* **Sensing-driven OFDMA.** Blank individual tones around interference and
  schedule users around it at millisecond timescales. This is the strongest
  candidate because it joins packages 5 and 7 to work already in the tree.
* **Hybrid ARQ** using native's soft decoder output.
* **OFDMA schedulers** such as proportional fair across strong and weak users.
* **Uplink OFDMA**, the largest and riskiest of these, because of alignment
  without a shared clock.

Teaching labs
-------------

Each lab starts in a simulation notebook and then moves over the air.

* ALOHA throughput against offered load, measured on real radios against the
  analytic curve.
* CSMA against sensing delay: students vary frame length and watch the
  advantage over ALOHA appear and disappear.
* Hidden terminals on the grid's non-reciprocal links.
* TDMA, FDMA and OFDMA compared on the same strong and weak links.
* Spectrum sensing: detection against false alarm rate with a controlled
  transmitter.
* Writing a channel-selection policy in Python, using the control plane.

Not planned
-----------

* FPGA carrier sensing or microsecond CSMA/CA, unless a funded project needs
  it.
* Replacing liquid. It stays as the comparison baseline.

Open items
----------

* Why the grid57 receive path has a 9 dB in-band droop when its hardware IDs
  match grid59 and grid62.
* Whether the grid59 X310 needs a power cycle.
* The true minimum sensing delay of the host path, on N210 and X310.
* Whether the ALOHA and FDMA MACs work with the native PHYs in simulation.
* Whether Jupyter is in the class image, and how many simultaneous real-time
  runs a classroom server can hold.
* Native's throughput limit at wider bandwidths on the X310s.
