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
it is the first number work package 4 needs. Three ways to improve the ratio
are longer aggregated frames, narrower bandwidth and a tuned host path. An
FPGA detector would change the picture but is a different scale of project.

**Simulator.** A keyword search of ``scripts/sim-air.py`` finds no collision
or interference model; this has not been confirmed by reading it closely. MAC
work needs one, or every result depends on grid time.

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

2. **Interference in the simulator** (3–6 weeks). Add overlapping
   transmissions, power-dependent capture and a topology with hidden nodes to
   the simulated device. *Done when:* simulated ALOHA throughput matches the
   analytic curve across offered load, and one collision scenario is checked
   against an OTA run.

3. **ALOHA and FDMA with native PHYs** (2–4 weeks). Run both MACs over the air
   with native OFDM and liquid. Check guard bands against the measured carrier
   offsets of a few hundred hertz. *Done when:* throughput against offered
   load is measured for both MACs against TDMA on the same links.

4. **Spectrum sensing** (3–5 weeks). Add a live PSD and per-channel occupancy
   from the receive stream, logged and available in Python. Measure detection
   probability against false alarm rate. Decide how to sense while
   transmitting; TDMA guard gaps are the simple answer. *Done when:* a Python
   controller can read occupancy, and detection performance is measured
   against a known signal.

5. **Listen-before-talk CSMA** (4–8 weeks). Measure the host sensing delay
   first. Add a MAC with energy-based sensing and random backoff. Sweep the
   ratio of sensing delay to frame length. *Done when:* measured throughput
   follows the theoretical trend across that sweep, whether or not it beats
   ALOHA at every setting.

6. **Dynamic spectrum access** (2–3 months). A Python controller picks
   channels from sensing data and coordinates over a control channel, reusing
   the SC2 code where it still fits. *Done when:* a link moves away from an
   interferer and recovers, with the recovery time measured.

7. **Research tracks** (1–3 months each, chosen after package 4). See below.

Research directions
-------------------

These depend on owning the PHY, which liquid makes hard.

* **Sensing-driven OFDMA.** Blank individual tones around interference and
  schedule users around it at millisecond timescales. This is the strongest
  candidate because it joins packages 4 and 6 to work already in the tree.
* **Hybrid ARQ** using native's soft decoder output.
* **OFDMA schedulers** such as proportional fair across strong and weak users.
* **Uplink OFDMA**, the largest and riskiest of these, because of alignment
  without a shared clock.

Teaching labs
-------------

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
* Whether ``scripts/sim-air.py`` has any interference model at all.
* Native's throughput limit at wider bandwidths on the X310s.
