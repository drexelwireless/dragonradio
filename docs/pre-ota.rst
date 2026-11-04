Software for over-the-air evaluation
====================================

Status 2026-11-04. These are the pieces needed before native OFDM, native
OFDMA and liquid OFDM go over the air (P4 items 2.1–2.5):

* a simulated device with hardware-like impairments, and where each PHY
  breaks under oscillator and sample-clock offsets;
* independent node clocks and TDMA time synchronization;
* a full-stack build bundle for radio hosts;
* a dry-run-only OTA runner and a stratified analysis;
* native constellations for the production AMC tables.

Everything ran in simulation on gridgpu01. No radio was opened and nothing
was transmitted.

Hardware-like impairments in the simulated device
-------------------------------------------------

``SimUSRP`` (``src/sim/SimUSRP.*``) gained the following, all off by
default so that existing runs are unchanged:

* **Sample clock offset per link** (``Link.sfo_ppm``; seventh field of
  ``sim-node.py --link``). Each burst is resampled to ``x(k(1 + 1e-6 ppm))``
  with a 32-tap Blackman windowed-sinc interpolator (1024 phases). Error
  against an analytic tone is below 2e-3 at +/-20 and +/-2000 ppm.
* **Gain model** (``--sim-gain-model TX_REF:RX_REF``). Bursts are scaled by
  the transmitter's TX gain, and received signal and thermal noise by the
  receiver's RX gain, relative to reference gains. Link ``gain_db`` is then
  path gain, and ``--sim-noise-db`` is referred to the input at the
  reference RX gain.
* **Receiver IQ imbalance** (``--sim-iq-imbalance AMP_DB:PHASE_DEG``,
  ``I' = I, Q' = a(sin(phi) I + cos(phi) Q)``), **DC offset**
  (``--sim-dc-offset RE:IM``) and **ADC full-scale clipping** of I and Q
  (``--sim-adc-full-scale FS``, counted in ``rx_clipped``). They are applied
  after the RX gain, in that order.
* **One oscillator offset** (``sim-node.py --link-ppm SRC:PPM``). Carrier
  offset ``PPM * fc`` and sample clock offset ``PPM`` together, as on a USRP
  whose LO and ADC share one reference.
* **TX-to-RX leakage** (``--sim-tx-leakage-db``). A node hears its own
  transmissions, so it cannot receive a peer while transmitting.
* **Independent clocks.** See the next section.

Receiver noise now comes from xoshiro256** and Box–Muller instead of
``std::normal_distribution``, which had taken 67% of the MAC RX thread at
5 MS/s. The rendered noise passes mean, I/Q variance, kurtosis and
correlation checks. ``tests/fullstack/test_sim_device.cc`` covers each
impairment against closed forms:

* SFO against an analytic tone;
* the IQ image ratio ``|nu|^2/|mu|^2``;
* DC offset;
* TX/RX gain scaling;
* input-referred noise;
* clipping with its counter;
* the simulated clock;
* leakage.

Oscillator and sample-clock tolerance through the full stack
------------------------------------------------------------

``scripts/sim-osc-sweep.py`` runs the ``clean-arq`` scenario per PHY:
bidirectional 40 x 500 B/s UDP, ARQ, TDMA, high SNR, 1 MS/s, M = 48, rs8
QPSK. Node 2's simulated oscillator is off by PPM relative to node 1, giving
carrier offset ``PPM * fc`` (opposite signs on the two links) and sample
clock offset PPM. A second sweep sets the sample clock offset only. The
subcarrier spacing is 1 MS/s / 48 = 20.8 kHz. N210/X310 oscillators are
specified at a few ppm without a reference, so two independent radios can
differ by up to about twice that.

Frame delivery on both links (1→2 / 2→1). CFO is in subcarriers on the
1→2 link; the 2→1 link sees the opposite sign.

.. list-table::
   :header-rows: 1

   * - Node 2 oscillator
     - CFO (subcarriers)
     - native rs8
     - liquid rs8 (production)
   * - 0–5 ppm at 1 GHz
     - 0 to 0.24
     - 100% / 100%
     - 97–100% (losses are counted late TX bursts on a loaded host)
   * - 10 ppm at 1 GHz
     - 0.48
     - 100% / 100%
     - 98.7% / 99.6%
   * - 15–25 ppm at 1 GHz
     - 0.72 to 1.20
     - 100% / 100%
     - 0% / 0%
   * - 30–50 ppm at 1 GHz
     - 1.44 to 2.40
     - 0% / 0%
     - 0% (30 ppm: run produced no summary, not repeated)
   * - 1–2 ppm at 2.4 GHz
     - 0.12 to 0.23
     - 100% / 100%
     - 100% / 100%
   * - 5 ppm at 2.4 GHz
     - 0.58
     - 100% / 99.7%
     - 9.9% / 15.3%
   * - 10 ppm at 2.4 GHz
     - 1.15
     - 100% / 100%
     - 0% / 0%
   * - 15–50 ppm at 2.4 GHz
     - 1.73 to 5.76
     - 0% / 0%
     - 0% / 0%

Sample clock offset only (no carrier offset), 1 GHz:

.. list-table::
   :header-rows: 1

   * - SFO
     - native rs8: frames, application
     - liquid rs8: frames, application
   * - +/-20 ppm
     - 99.7% / 99.6%; 100%
     - 100% / 100%; 100%
   * - +/-50 ppm
     - 99.0% / 100%; 100%
     - 99.4% / 100%; 100%
   * - +/-100, 200, 500 ppm
     - about 49%; 0% (only short frames survive)
     - 99.4–100%; 100%

* **Carrier offset.** Native acquires to about +/-1.3 subcarriers (the P1
  design range); liquid to about +/-0.5. At 1 MS/s and M = 48, native
  tolerates about 25 ppm of relative oscillator error at 1 GHz and 10 ppm at
  2.4 GHz; liquid 10 ppm and 2 ppm. Two N210s on their internal
  references can differ by several ppm, so liquid at 2.4 GHz and 1 MS/s is
  marginal without a common reference. Wider subcarriers (higher rate or
  smaller M) scale the tolerance up in proportion.
* **Sample clock offset.** Native does not track timing drift within a
  frame (only the common phase per symbol). It holds to 50 ppm and loses
  every long frame at 100 ppm, where an 8000-sample frame drifts by 0.8
  samples. Liquid tolerated 500 ppm. Real oscillator errors (a few ppm)
  are far below native's limit. Their carrier effect, not their sample
  clock effect, limits both PHYs.
* These are single runs per point at high SNR (simulation). Liquid's
  losses below its limit coincided with late TX bursts, counted and caused
  by a loaded host.

Clock synchronization for TDMA across hosts
-------------------------------------------

Real nodes do not share a clock. In simulation, ``Clock::setSimulatedClock``
(``--sim-clock-offset SEC``, ``--sim-clock-ppm PPM``) gives a node with no
USRP attached the device time ``host + offset + ppm (host - epoch)``. With a
USRP nothing changes, and the defaults keep the host clock exactly.

``SimUSRP`` keeps its sample clock on that device time. On the air (relay
protocol v3), burst starts are on the shared host clock and carry the
transmitter's oscillator error. The receiver maps them to its own clock,
resamples by the ratio of the two sample clocks, and adds the carrier offset
of the two oscillators. Application timestamps (``SimTraffic``) use the host
clock, a measuring instrument outside the radios.

The radio already has a synchronization protocol:

* Timestamped HELLO broadcasts (``SmartController::broadcastHello``) carry
  a timestamp sequence. Receivers record their device time of reception.
  The time master (the gateway, ``RadioNet::getTimeMaster``) echoes its send
  times and others' receive times.
* ``radio.synchronizeClock`` (``radio/timesync.py``) regresses the pairs
  into an offset and skew for ``WallClock``, which the TDMA MAC uses for
  slot timing. The collaboration controller normally drives it.
  ``sim-node.py --time-master N --hello-period S --timesync-period S`` runs
  it without that controller.

Scenarios: saturated both ways, TX leakage -30 dB, 35 ms slots with a 10 ms
guard, node 2's clock 15 ms ahead.

.. list-table::
   :header-rows: 1

   * - Scenario
     - Node 2 WallClock vs master
     - 1→2 / 2→1 frame delivery
     - Goodput 1→2 / 2→1
   * - clock-offset-nosync
     - 15 ms ahead throughout
     - 100% / 69.0% (295 retransmissions)
     - 341 / 225 kb/s
   * - clock-offset-sync
     - 15.0 ms at start, 14 us max over the second half
     - 100% / 100%
     - 340 / 339 kb/s
   * - clock-skew-sync (+15 ms and 20 ppm: carrier, sample clock and time)
     - 15.1 ms at start, 13 us max over the second half
     - 100% / 100%
     - 340 / 339 kb/s

Without synchronization, node 2's slot starts 5 ms inside node 1's
transmission. Node 1 cannot hear what node 2 sends while it is itself
transmitting, so 2→1 loses a third of its frames. With the protocol (HELLO
every 0.5 s, fit every 2 s), the WallClocks agree to tens of microseconds,
far inside the guard.

**What real hardware needs.** Either a common reference (GPSDO or
PPS + 10 MHz distributed to every radio, ``--clock-source external
--time-source external``; ``timesync.py`` then fits offset only, with no
skew), or the timestamp protocol above with independent clocks. The
protocol needs a time master marked as gateway, HELLO broadcasts, and
``clock_sync_period`` (or ``--timesync-period``). Until the first fit, TDMA
slots can overlap by the initial offset. Start traffic after
synchronization, or keep the initial offset below the guard: for example,
set each USRP's time from the host's NTP-disciplined clock, as
``Clock::setUSRP`` does, before the protocol refines it. Independent
oscillators also mean carrier and sample-clock offsets, so the acquisition
limits above apply. One-way latency across hosts is only meaningful after
synchronization; use RTT otherwise. The N210 time source is ``none``,
``external`` or ``mimo``, never ``internal``.

Full-stack build bundle for radio hosts
---------------------------------------

``scripts/package-grid.py`` packages only the PHY engine.
``scripts/package-fullstack.py --workspace WS`` packages:

* the complete tree at a commit (``git archive`` plus a ``COMMIT`` file; a
  dirty working tree is recorded, not packaged);
* the pinned liquidDSP checkout, with the generated ``configure``,
  ``config.h.in`` and ``aclocal.m4``;
* xsimd, firpm and libcorrect at their submodule pins (it refuses if a
  workspace checkout differs from its pin);
* the workspace's 56 Ubuntu 24.04 amd64 packages and 34 Python wheels at the
  exact versions;
* ``setup.sh`` and ``MANIFEST.json`` (SHA-256 of every file, submodule pins,
  versions, notes).

``setup.sh WORKSPACE`` verifies the hashes and runs
``grid/setup-fullstack-build.sh`` with ``VENDOR=`` the bundle, so nothing is
fetched. It then runs ``grid/build-fullstack.sh``, which builds the radio
and runs ``tests/fullstack``. No sudo is needed, nothing is installed
system-wide and no radio is opened.

**Verified from scratch, offline.** Bundle ``dragonradio-fullstack-ecafe223``
(200 MB tar; sha256 ``06b47912…``) was extracted and set up inside
``unshare -rn``, a network namespace with only a loopback interface that was
down:

* all 97 files verified;
* every dependency came from the bundle at its pin (xsimd 3d17850e, firpm
  6b41bce2, libcorrect 2c8db6d8, liquid d39f1c0f with libfec, pybind11
  2.13.6, UHD 4.6.0 from the bundled packages);
* all nine ``tests/fullstack`` programs and the embedded-Python test passed.

UHD 3.9 (historical image) versus UHD 4.x (this workspace)
----------------------------------------------------------

The radio's device code (``src/USRP.cc``) uses only the ``multi_usrp``
streaming API: ``get_tx_stream``/``get_rx_stream``,
``tx_streamer::recv_async_msg``, ``set_rx_dc_offset`` and the time and
clock source calls. That API exists in both UHD 3.9 and 4.x. The
differences to reconcile on a radio host are the environment, not the
radio code:

* **FPGA images.** A UHD release only works with FPGA images whose
  compatibility number it supports. N210 and X310 images loaded for UHD 3.9
  are not those UHD 4.x expects, and the reverse is also true. Check each
  radio read-only (``uhd_usrp_probe``, with approval), and never flash
  firmware or FPGA to match without explicit approval. Build against the UHD
  the radios' images match.
* **The image's toolchain.** The historical image is Ubuntu 20.04 with UHD
  3.9 and Python 3.8; the Makefile hardcodes Python 3.8 and
  ``/usr/local``. This bundle is Ubuntu 24.04 with UHD 4.6, Boost 1.83,
  Python 3.12 and pybind11 2.13.6. The pinned pybind11 submodule (787d2c8)
  predates Python 3.11. The bundle's packages and wheels apply only to
  Ubuntu 24.04 x86_64. On a 20.04/UHD 3.9 image, run
  ``setup-fullstack-build.sh`` without ``VENDOR`` against that image's own
  development packages, or provide 20.04 packages. That path has not been
  exercised.
* **Do not overwrite ``/usr/local`` UHD** on shared images to make a
  mismatch disappear. The workspace keeps its UHD private.
* **Python UHD bindings.** UHD 3.9 has none, while the PHY-pilot runner
  (``ota-phy.py``) was checked against UHD 4.x's Python API. The full-stack
  runner does not use UHD from Python; the radio uses libuhd.
* **Default transport frame sizes** differ by version and device (N210 1 GbE
  sc16 is about 363 samples per packet). The P4 streaming benchmark showed
  receive chunk size changes demodulator cost by less than 5%.

Full-stack OTA runner (dry run only)
------------------------------------

``scripts/ota-fullstack.py --plan PLAN.json`` is the OTA counterpart of
``sim-fullstack.py`` (``grid/fullstack-plan.example.json``). It validates the
plan and, by default, only prints the exact ssh command and node script for
each node. ``--execute`` runs the trial and transmits; it was not used here.

The plan must name:

* a reservation and an RF approval that covers forward data, reverse data
  and ACKs, and timesync HELLO broadcasts (TDMA with ARQ transmits from
  every node);
* distinct hosts and device addresses, absolute paths, and the 40-character
  workspace commit;
* finite durations: 5–600 s of traffic, a warm-up of 120 s at most;
* a rate the soak verified (1, 2 or 5 MHz), gains within each family's
  range, N210 time sources of none, external or mimo;
* a time master;
* the strata (pair type, strength, placement, power, clock, carrier map).

Each node script:

1. checks the workspace ``COMMIT``, that no other radio runs, and that 2 GB
   of node-local disk is free;
2. creates a per-trial network namespace for the tap interface and
   applications, optionally moving the USRP's Ethernet interface into it
   (restored on exit);
3. runs the production radio on the real USRP (``sim-node.py --hardware
   --addr``, with ``--arq`` and the timesync options) under ``timeout``;
4. writes a readiness file and waits for the controller's ``GO``, written
   only once every node is ready;
5. runs ``sim-netapp.py`` (kernel UDP and TCP over the tap) for the finite
   duration;
6. counts continuity errors in the radio log (late TX, underflow, RX
   errors, missed slot deadlines) and copies node-local results to the
   shared results directory from an exit trap.

The controller then scores the trial with the simulation's ``evaluate_tap``
and marks any trial with continuity errors invalid for RF conclusions (plan
section 7.9).

``scripts/summarize-fullstack.py`` groups trial summaries by stratum: pair
type, strength, placement, power, clock, carrier map, PHY, rate, frequency
and build. It never pools strata. Trials with continuity errors are
excluded and listed with their cause. Per-flow delivery, UDP latency, TCP
goodput and RTT get trial-level bootstrap 95% intervals, and paired
native − liquid differences are reported where trials share every other
stratum key and a ``pair_block``.

``tests/simulation/test_ota_fullstack.py`` runs the dry run with
``subprocess.Popen`` patched to fail, checks the generated commands and
eleven plan rejections, and checks the summary's stratification and
exclusion.

**Requires on a radio host (not done here):** root or passwordless sudo
for the radio's capabilities (CAP_NET_ADMIN for the tap, CAP_SYS_NICE for
real-time priority), the reservation, the RF approval and the read-only
device probe.

**Updated 2026-11-04.** On the grid, users have full admin rights in their
LXC containers, so no sudo setup or approval is needed. The node script now
runs privileged commands directly as root or through ``sudo -n``
(``"privilege": "auto"``). By default it uses the container's own network
stack instead of a per-trial namespace (``"netns": false``), so the USRP
interface is never moved. It also passes the workspace's library and Python
paths to the radio, which the first version omitted, and starts
applications at a wall-clock time carried in GO. Device identity is now
host plus address, since every grid node reaches its radio at the same
local address. The grid procedure is :doc:`ota-fullstack-grid`.

Native constellations for the production AMC tables
---------------------------------------------------

The production AMC table (``config/srn/radio.conf``) is BPSK, QPSK, 8-, 16-,
32-, 64-, 128- and 256-QAM, all with ``rs8``. ``NativeFrame.hh``
``Constellation`` provides Gray rectangular QAM with 1–8 bits per tone:

* I gets the extra bit for odd orders;
* per axis, a sign bit, then a Gray-coded magnitude;
* unit average power;
* exact max-log soft demapping.

``NativeOFDM`` maps liquid's ``bpsk`` and ``qam8``–``qam256`` to it, so the
same table runs on both PHYs. The points and labels differ from liquid's of
the same order, and the PHYs do not interoperate. QPSK and 16-QAM are
unchanged on the wire: the same mapping, the original demapper (16-QAM keeps
its distance-to-boundary reliabilities), the same scrambler bits, and the
reference-transmitter test still passes. The header's bits-per-tone byte may
now be 1–8.

**Packet error rate** (``bench/fec_per.cc``, ``grid/run-fec-benchmark.sh WS
OUT 500 1000 rs8``): rs8, 1000-byte packets, 500 per point, AWGN, real
acquisition, 1 dB steps. SNR is per sample over the frame. The native
header is v29 (default); the liquid header is v29p78 BPSK (production). In
306 points no packet passed CRC with wrong bytes.

.. list-table:: SNR (dB) for 1% packet error rate, rs8, about +/-0.5 dB
   :header-rows: 1

   * - Constellation
     - native
     - liquid
   * - BPSK
     - 4.5
     - 5.5
   * - QPSK
     - 7.5
     - 9.5
   * - 8-QAM
     - 12.0
     - 13.5
   * - 16-QAM
     - 14.5
     - 15.5
   * - 32-QAM
     - 18.5
     - 19.5
   * - 64-QAM
     - 20.5
     - 21.5
   * - 128-QAM
     - 23.8
     - 25.5
   * - 256-QAM
     - 25.5
     - 27.5

Native needs 1–2 dB less than liquid at every order of the table. That
matches the P3 finding for QPSK and 16-QAM: the coded bits are the same, so
the gap is acquisition, channel estimation and demapping. The demodulation
times in this run were taken under heavy parallel load and are not used.
No theory curves were computed for the new orders.
