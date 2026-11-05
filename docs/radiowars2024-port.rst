Native OFDM, downlink OFDMA and full-stack simulation on radiowars2024
======================================================================

Status 2026-11-05, branch ``codex/native-radiowars2024``. Written for Kapil
Dandekar and Geoffrey Mainland.

This branch carries the native OFDM PHY (no liquid-dsp DSP), downlink
OFDMA, the full-stack simulation harness and the OTA runner from
``codex/ofdma-simulation`` onto DragonRadio ``wip/radiowars2024``
(``5cf3e7d``), the version in the GNU Radio 3.10 class image
(``krd26/dragonradio-class-gr310-20261027-v2``: Ubuntu 26.04, GCC 15,
Python 3.14, pybind11 3.0, UHD 3.9.5 ``cc15d9ce``).

Everything here was built and run in the class image on grid62 without
RF. No USRP was opened. All radio results below are from the simulated
device (:ref:`port-sim`) and are labeled as simulation.

.. contents::
   :local:
   :depth: 2

What was ported, and where it lives here
----------------------------------------

``codex/ofdma-simulation`` is based on the 2021 ``main``, 677 commits behind
``wip/radiowars2024``. The latter replaced the synthesizers, the channelizers,
RadioNet, the controller layer and the Python package. Each piece was
therefore re-fitted rather than cherry-picked.

.. list-table::
   :header-rows: 1
   :widths: 25 35 40

   * - Piece
     - On ``codex/ofdma-simulation``
     - On this branch
   * - Native OFDM and OFDMA PHYs
     - ``src/phy/NativeOFDM*``, engines in ``src/simulation``
     - Same files and engines, adapted to this branch's PHY interface:
       per-channel demodulators, oversample factors, ``MonoClock``
       durations, two-field MCS table entries.

       Native FEC goes through the image's libfec API (libcorrect's shim,
       which the image's liquid-dsp also uses). Built with
       ``-fno-finite-math-only`` under the branch's ``-Ofast``.

       ``--phy native-ofdm`` / ``native-ofdma`` (``0a7c7fc6``,
       ``dc0566b9``, ``a61cd244``, ``1d500589``).
   * - OFDMA burst batching
     - ``UnichannelSynthesizer`` (slot synthesizer), with a ``Port::kick``
       to bound the wait for a partner
     - ``ChannelSynthesizer`` (``TDSynthesizer``/``FDSynthesizer``), which
       replaced the Unichannel, Slot and Parallel synthesizers.

       This branch's ports have no kick, so the network queue gets one:
       ``Queue::kick``, which makes one pop blocked on an empty queue
       return false. Only the synthesizer calls it, and only for PHYs with
       ``getMaxBurstUsers() > 1`` (``642470c4``).

       A burst that cannot fit in a slot is split into single packets.
       Batching runs only with one modulation thread, the default
       (``c77fa6ba``, from an independent review).
   * - Per-partner MAC accounting
     - ``MAC::txNotifier``, ``SlottedMAC::missedSlot``
     - ``MAC::txNotifier`` (partners become ModPackets of their own; airtime
       and self-TX snapshots counted once) and ``MAC::abortTXRecord``,
       which is this branch's missed-slot path (``642470c4``).
   * - Slot re-modulation fix
     - ``f0f0f3b8`` in ``UnichannelSynthesizer``
     - The same bug was in ``ChannelSynthesizer``, fixed with a slot
       generation (``543707c6``). ``MultichannelSynthesizer`` did not have
       it.
   * - Simulated device ``SimUSRP``
     - A subclass of the concrete ``USRP`` class; one global simulated
       clock (``Clock::setSimulatedClock``)
     - An implementation of the abstract ``Radio`` device, so the MACs take
       it unchanged. On this branch the device is the clock
       (``MonoClock::TimeKeeper``), so the simulated offset and rate error
       are per device: ``SimUSRP::setClock``.

       The air protocol (v3) is unchanged, so ``scripts/sim-air.py``
       relays either branch's devices (``bc25fa05``, ``9a1cd69d``).
   * - ``sim_*`` options and radio wiring
     - ``dragonradio.radio.Radio.configureUSRP``
     - ``dragonradio.controller.Radio.configureSimulatedDevice``, selected
       by ``--sim-air``. That path never constructs a ``USRP``.

       ``--sim-traffic`` replaces ``TunTap`` with ``SimTraffic``
       (``8687bfb6``).
   * - Scenario harness
     - ``scripts/sim-{fullstack,node,air,netapp}.py``
     - The same scripts. ``sim-node.py`` drives
       ``dragonradio.controller.Radio`` as the class course's
       ``sdr-class-radio.py`` does: ``Config``, ``Radio``, TDMA schedule
       from ``-n``, ``radio.start()``.

       The time master is ``nhood.me.is_gateway`` (RadioNet became
       Neighborhood/Node). Scenarios and pass criteria are unchanged
       (``eb374934``, ``f2ac8ccf``).
   * - PPS time setting
     - ``--sync-time-to-pps``, ``USRP::syncTimeToPPS``
     - This branch's existing ``--use-pps``, now
       ``USRP::syncTime(use_pps)``: waits for an edge, checks the
       reference lock, verifies (``cab14e2e``).
   * - Fixes carried over
     - Various
     - ARQ SYN retransmission (``61094286``); ``canPop`` passes
       unsequenced packets (``43677d24``); tail-drop push-out
       (``dfaed327``); RED gentle region and logger cache (``ccdbfb9f``);
       ``Seq`` alignment (``f9fa0d5d``); TimerQueue/TunTap SIGUSR1
       start-up race (``456a46d3``).
   * - OTA runner
     - ``scripts/ota-fullstack.py``, ``grid/ota-*``
     - The same files, for a class-image node workspace (``2110e7df``).
       See :ref:`port-ota`.

Not ported:

* ``WorkMeter`` (per-thread CPU metering, used by the P4 soak);
* ``scripts/sim-soak.py`` and the sanitizer builds
  (``grid/build-sanitized.sh``);
* the old branch's alternative channelizers (``OverlapTDChannelizer``).

The TSan fixes ``cf3f8210`` and ``d8758817`` were reviewed and needed no
port:

* ``IQBuf::waitToStartFilling`` already uses acquire loads here.
* The ``WorkQueue`` and ``TimerQueue`` stop flags are already atomic.
* This branch's ``SafeQueue`` was redesigned (``disable()`` sets its flag
  under the lock).
* The simulated clock is now ``SimUSRP``'s atomics.

What differs from codex/ofdma-simulation, and why
-------------------------------------------------

**Receive latency is about 14 ms higher.** This is the FD channelizer, and
hardware runs would see the same.

* ``clean-arq`` p50 one-way latency is 87–90 ms here and 76–77 ms on the
  old branch.
* The HDF5 logs show the send side is the same: 44–48 ms from entering
  the radio to the frame's air time on both branches.
* Demodulation latency (frame start to delivery) is 44–45 ms here and
  31 ms there.
* This branch's FD channelizer uses ``FDResampler`` with P = 9601 and
  V = 8: FFT blocks of N = 76,800, of which L = 67,200 samples are new
  (67 ms at 1 MS/s). The old branch's used P = 12,801 and V = 4: L = 38,400
  (38 ms).
* A frame waits on average about L/2 for its block to complete, which
  predicts +14.4 ms. The measured difference is +13 to +14 ms.
* The same 14 ms pushes ``mandate-expiry`` over its 500 ms bound (below).
  ``--channelizer timedomain`` avoids the block wait (see results).

**The receive stream is continuous.** This branch's MAC starts the RX
stream once and reads fixed periods without a start time
(``burstRX(nullopt, ...)``); the old branch's MAC read slot-aligned, timed
bursts. ``SimUSRP`` implements both, as UHD does (``test_sim_device``
tests 16–18).

**FFTW wisdom.** This branch imports and exports FFTW wisdom at
``/tmp/dragonwisdom`` by default; the old branch used none.

* Three simulated radios starting at once on one host raced on that file,
  and one exited with "Could not import wisdom".
* Per-node files avoided the race but made start-up uneven. A node
  without wisdom plans FFTs for 4 s, so it starts its traffic 4 s after its
  peers, and their 4 s drain ends before its last packets arrive (seen
  once in ``three-node``: 394 of 401).
* The harness now runs radios without wisdom (``--set fftw_wisdom=None``),
  as on the old branch.
* Radios on separate hosts are unaffected. Two radios started together on
  one host with the default would still race on the file; that is a
  property of this branch, unchanged here.

**Simulated time and host time.** ``SimUSRP`` time is the host's
``CLOCK_MONOTONIC``, not UNIX time as a USRP's is after ``syncTime``. On
grid62 that clock reads about 1.03e6 s. Two of the old branch's device
computations broke at that magnitude, and both are fixed here with tests:

* **Carrier shift.** For equal tunings it was not exactly zero: a 4e-8 Hz
  FMA residue under ``-Ofast``. The phase of a shift is taken from absolute
  sample time, so bursts were rotated by 0.26 rad. It is now computed as
  ``(fc - rx)(1 + tx_ppm) + rx (tx_ppm - rx_ppm)``, which is exactly zero
  for equal tunings and clocks.
* **Device-to-host time.** ``sim-fullstack.py`` mapped device time to host
  time as ``(t - offset)/(1 + ppm)``, ignoring the clock's epoch: an error
  of ppm × uptime, 20 s at 20 ppm. ``sim-node.py`` now records two
  (host, device) pairs and the harness interpolates.

**TX leakage NaN sentinel.** The old branch marked TX-to-RX leakage "off"
with a NaN gain and tested it with ``std::isnan``. This branch builds with
``-Ofast`` (``-ffinite-math-only``), which folds that test to false.

* Every simulated node therefore rendered NaN into its own receiver while
  transmitting.
* The native PHY threw "stream must contain finite IQ", and liquid
  received 2 of 1645 frames.
* "Off" is now ``std::optional``. ``test_sim_device`` test 20 fails before
  the fix (energy nan) and passes after.
* The device now counts non-finite samples it sends and delivers
  (``tx_nonfinite``, ``rx_nonfinite`` in its stats). Every scenario here
  reports 0.

**The FD channelizer's first FFT** was suspected while chasing the NaN
above and turned out to be fine.

* Its discontinuity check never fires for the MAC's first IQ buffer (seq 1
  against an initial 0).
* But the FFT buffers are ``std::vector``\ s, value-initialized to zero
  when the worker starts.
* ``tests/fullstack/test_fd_channelizer`` checks this with a poisoned heap
  (glibc ``M_PERTURB``): finite input gives finite output.

That test found a latent crash: an ``FDChannelizer`` constructed with
channels (rather than constructed empty and then given channels, as the
radio does) crashes in ``wake_dependents``. The radio never does this, so
it is left unchanged.

**Defaults.** Same as the old branch. See :doc:`defaults-2026-11`.

Building in the image
---------------------

On a node running the class image (no root needed, nothing under
``/usr/local`` changes):

.. code-block:: sh

   git archive HEAD | tar -x -C /tmp/drport/dragonradio   # or a checkout
   bash /tmp/drport/dragonradio/grid/build-in-class-image.sh /tmp/drport/dragonradio $(nproc)
   make -C /tmp/drport/dragonradio/build -j$(nproc) fullstack-tests

``grid/build-in-class-image.sh`` builds the way the image builds its own
DragonRadio:

* the image's ``extern/`` sources, liquid-dsp, libcorrect and firpm;
* the system pybind11 3.0;
* ``-include cstdint -include cstdio``.

It also does three things the image's own build doesn't:

* writes ``dragonradio/version.py``, which setuptools-scm would write on
  install;
* links ``dragonradio`` to ``build/dragonradio``;
* makes ``venv/`` (virtualenv), which sees this tree's ``dragonradio``
  package first and then the image venv's packages.

Run a radio script with:

.. code-block:: sh

   VIRTUAL_ENV=/tmp/drport/dragonradio/venv /tmp/drport/dragonradio/build/dragonradio SCRIPT ARGS

Tests: ``build/tests/fullstack/test_*`` (each prints PASSED or FAILED;
``ctest -L fullstack``). Scenarios: ``grid/run-sim-fullstack.sh TREE
OUTDIR [--scenario NAME ...] [-- RADIO ARGS]``. Tap scenarios need
unprivileged user and network namespaces (``unshare -rn``). They work in
the class-image container on grid62 without sudo.

.. _port-sim:

The simulated device
--------------------

``--sim-air SOCKET`` gives the radio a ``SimUSRP`` connected to the air
relay ``scripts/sim-air.py``. Every node on a host connects to the same
relay; each receiver renders its own stream:

* complex Gaussian noise (``--sim-noise-db``);
* every other node's bursts after the directed link model: gain, carrier
  offset, delay, erasure, jitter and sample clock offset (``sim-node.py
  --link``);
* the tuning difference;
* optionally this device's own TX leakage (``--sim-tx-leakage-db``);
* the gain model, IQ imbalance, DC offset and ADC clipping
  (``--sim-gain-model``, ``--sim-iq-imbalance``, ``--sim-dc-offset``,
  ``--sim-adc-full-scale``).

``--sim-clock-offset`` and ``--sim-clock-ppm`` give the node its own clock.
Runs are paced by the wall clock:

* RX samples for time t are released at t plus ``--sim-rx-latency``;
* a timed TX burst submitted after its time is dropped and counted late,
  as UHD does.

The device never falls back to a USRP: if the relay is unreachable,
construction fails. ``--sim-traffic`` replaces the tap interface with
synthetic UDP flows (``SimTraffic``). Without it, the radio opens a real
tap in the node's namespace.

Test and scenario results
-------------------------

All results are simulation over the simulated device on grid62 (class
image, 128 threads, otherwise idle), at commit ``1d1edf3b`` unless stated
otherwise. No RF.

Test programs
~~~~~~~~~~~~~

All 14 ``tests/fullstack`` programs pass on grid62:

* carried over before this work: ``test_arq_syn``, ``test_logger_chunks``,
  ``test_native_fec``, ``test_native_ofdm``, ``test_native_ofdma``,
  ``test_native_perf``, ``test_native_qam``, ``test_net_queue``,
  ``test_packet_align``, ``test_timer_wake``;
* new or ported in this work: ``test_sim_device``, ``test_synth_slot``,
  ``test_ofdma_batch``, ``test_fd_channelizer``.

Every production fix in this work has a test that fails before it and
passes after:

.. list-table::
   :header-rows: 1

   * - Test
     - Before the fix
     - After
   * - ``test_synth_slot``
     - 10,020 modulations of one packet in a 200 ms slot
     - 2
   * - ``test_ofdma_batch`` case 5
     - 0 of 4 packets sent: the burst is carried forever
     - 4 sent alone, in order
   * - ``test_sim_device`` test 20 (simulation only)
     - Energy NaN at a receiver that hears itself
     - Silent and finite

``tests/simulation/test_ota_fullstack.py`` and ``test_ota_pipeline.py``: 20
tests pass on gridgpu01 against a fake ssh/gridcli.

Native PHY scenarios (``--phy native-ofdm``, as the old branch ran them)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This branch passes 28 of 31. The comparison is the old branch's last full
run with the same defaults (``~/dragonradio-bench/defaults-20261104``),
which passed 19 of 31.

The hosts differ. The old runs used gridgpu01: 72 CPUs, shared and loaded.
Almost every old failure is from host load:

* late TX bursts;
* late or dropped RX bursts on the simulated device;
* the PHY frame losses that follow from those.

Its other full run (``verify-2b9e749a``) passed 23 of 31 with different
failures. On grid62 no scenario had a late or dropped burst.

Fixed seeds do not make runs reproducible: they are paced by the wall
clock.

The three failures here:

* **mandate-expiry**: max one-way latency 513.6 ms against a 500 ms bound
  (p50 445 ms; old branch p50 422 ms, passed). This is the FD channelizer's
  +14 ms. With ``--channelizer timedomain`` it passes (p50 393 ms).
* **ofdma-unequal**: UE 2 delivery 0.208, the same as the old branch's
  0.2079 (see What remains).
* **tap-tcp-rr**: 131 of 150 request/response exchanges in the 20 s window.

  * Old branch: 150/150 and 139/150. Here: 120, 131, 123 and 126 in four
    runs.
  * Each exchange waits for a TDMA slot in each direction. The +14 ms
    receive latency makes more responses miss the peer's next slot: RTT
    p50 139 ms, p95 205 ms.
  * With ``--channelizer timedomain`` it completes 150/150 at RTT 73.7 ms.

``three-node`` failed once, at ``2110e7df``, from uneven start-up (FFTW
wisdom, fixed in ``1d1edf3b``). It passed in the final run and in two
repeats.

Downlink OFDMA (``--phy native-ofdma``) results, with old branch values in
parentheses:

* **ofdma-both-strong**: node 1 sent 196 two-UE bursts out of 259 (old
  branch: 194 of 316). Goodput to UE 3 is the same (77 kb/s). Goodput to
  UE 2 is lower: 94 vs 118 kb/s, because the old branch sent 59 more
  single-user bursts to UE 2.

  The cause is not established. A candidate is that ARQ send windows
  close more often under saturation because the UEs' ACKs arrive 14 ms
  later.
* **ofdma-one-idle**: every burst single-user.
* **ofdma-one-disconnected**: UE 2 kept its service.

The table gives each scenario's application delivery and goodput per flow,
p50 one-way latency, shared bursts, and failed checks. Both columns
inherit the scenario's offered loads (saturation scenarios deliver a
fraction of what is offered).

.. list-table::
   :header-rows: 1
   :widths: 18 41 41

   * - Scenario
     - codex/ofdma-simulation (defaults-20261104, gridgpu01)
     - this branch (1d1edf3b, grid62)
   * - ``clean-arq``
     - PASS: 1->2 1.000 160 kb/s p50 77 ms, 2->1 1.000 160 kb/s p50 76 ms
     - PASS: 1->2 1.000 160 kb/s p50 88 ms, 2->1 1.000 160 kb/s p50 89 ms
   * - ``clean-noarq``
     - PASS: 1->2 1.000 160 kb/s p50 76 ms, 2->1 1.000 160 kb/s p50 74 ms
     - PASS: 1->2 1.000 160 kb/s p50 90 ms, 2->1 1.000 160 kb/s p50 91 ms
   * - ``erasure-noarq``
     - PASS: 1->2 0.792 127 kb/s p50 73 ms
     - PASS: 1->2 0.792 127 kb/s p50 87 ms
   * - ``erasure-arq``
     - PASS: 1->2 1.000 156 kb/s p50 84 ms
     - PASS: 1->2 1.000 154 kb/s p50 98 ms
   * - ``ack-loss``
     - PASS: 1->2 1.000 160 kb/s p50 81 ms
     - PASS: 1->2 1.000 160 kb/s p50 97 ms
   * - ``asymmetric``
     - PASS: 1->2 1.000 158 kb/s p50 82 ms, 2->1 1.000 156 kb/s p50 92 ms
     - PASS: 1->2 1.000 156 kb/s p50 96 ms, 2->1 1.000 158 kb/s p50 116 ms
   * - ``saturation``
     - FAIL (1->2 PHY frame delivery >= 0.999): 1->2 0.338 341 kb/s p50 8757 ms
     - PASS: 1->2 0.337 339 kb/s p50 8810 ms
   * - ``impaired``
     - PASS: 1->2 1.000 160 kb/s p50 74 ms, 2->1 1.000 160 kb/s p50 77 ms
     - PASS: 1->2 1.000 160 kb/s p50 88 ms, 2->1 1.000 160 kb/s p50 91 ms
   * - ``jitter``
     - PASS: 1->2 1.000 161 kb/s p50 75 ms, 2->1 1.000 160 kb/s p50 78 ms
     - PASS: 1->2 1.000 160 kb/s p50 89 ms, 2->1 1.000 160 kb/s p50 91 ms
   * - ``taildrop-saturation``
     - PASS: 1->2 0.289 340 kb/s p50 509 ms
     - PASS: 1->2 0.289 341 kb/s p50 517 ms
   * - ``red-saturation``
     - PASS: 1->2 0.297 338 kb/s p50 2071 ms
     - PASS: 1->2 0.301 341 kb/s p50 2066 ms
   * - ``red-tuned``
     - PASS: 1->2 0.295 340 kb/s p50 1024 ms
     - PASS: 1->2 0.297 341 kb/s p50 1039 ms
   * - ``mandate-expiry``
     - PASS: 1->2 0.289 342 kb/s p50 422 ms
     - FAIL (1->2:5001 max latency 513.5676100000001 ms <= 500 ms): 1->2 0.288 340 kb/s p50 443 ms
   * - ``grant-change``
     - FAIL (node 1: no late TX bursts): 1->2 0.304 314 kb/s p50 8417 ms, 2->1 0.344 355 kb/s p50 13046 ms
     - PASS: 1->2 0.309 321 kb/s p50 7970 ms, 2->1 0.349 361 kb/s p50 12847 ms
   * - ``three-node``
     - PASS: 1->2 1.000 80 kb/s p50 89 ms, 2->3 1.000 80 kb/s p50 90 ms, 2->1 1.000 8 kb/s p50 92 ms, 2->3 1.000 8 kb/s p50 94 ms, 3->1 1.000 80 kb/s p50 93 ms
     - PASS: 1->2 1.000 80 kb/s p50 103 ms, 2->3 1.000 80 kb/s p50 103 ms, 2->1 1.000 8 kb/s p50 104 ms, 2->3 1.000 8 kb/s p50 107 ms, 3->1 1.000 80 kb/s p50 105 ms
   * - ``three-node-erasure``
     - FAIL (node 3: no late or dropped RX bursts): 1->2 1.000 80 kb/s p50 101 ms, 2->3 1.000 77 kb/s p50 109 ms, 2->1 1.000 8 kb/s p50 90 ms, 2->3 0.663 5 kb/s p50 94 ms, 3->1 1.000 82 kb/s p50 102 ms
     - PASS: 1->2 1.000 80 kb/s p50 114 ms, 2->3 1.000 77 kb/s p50 127 ms, 2->1 1.000 8 kb/s p50 108 ms, 2->3 0.703 6 kb/s p50 107 ms, 3->1 1.000 85 kb/s p50 122 ms
   * - ``clock-offset-nosync``
     - FAIL (node 2: no late TX bursts; node 2: no late or dropped RX bursts): 1->2 0.288 338 kb/s p50 510 ms, 2->1 0.197 220 kb/s p50 744 ms
     - PASS: 1->2 0.290 340 kb/s p50 517 ms, 2->1 0.196 226 kb/s p50 768 ms
   * - ``clock-offset-sync``
     - FAIL (1->2 PHY frame delivery >= 0.999): 1->2 0.288 338 kb/s p50 509 ms, 2->1 0.289 340 kb/s p50 509 ms
     - PASS: 1->2 0.282 331 kb/s p50 520 ms, 2->1 0.289 339 kb/s p50 517 ms
   * - ``clock-skew-sync``
     - FAIL (1->2 PHY frame delivery >= 0.999; 2->1 PHY frame delivery >= 0.999; node 2: no late or dropped RX bursts): 1->2 0.288 339 kb/s p50 510 ms, 2->1 0.290 341 kb/s p50 509 ms
     - PASS: 1->2 0.288 338 kb/s p50 516 ms, 2->1 0.283 333 kb/s p50 517 ms
   * - ``ofdma-both-strong``
     - FAIL (3->1 PHY frame delivery >= 0.999; node 3: no late TX bursts): 1->2 0.203 118 kb/s p50 880 ms, 1->3 0.133 77 kb/s p50 880 ms, 194/316 shared bursts
     - PASS: 1->2 0.163 94 kb/s p50 1001 ms, 1->3 0.133 77 kb/s p50 999 ms, 196/259 shared bursts
   * - ``ofdma-strong-weak``
     - FAIL (node 1: no late or dropped RX bursts): 1->2 0.133 77 kb/s p50 1090 ms, 1->3 0.135 77 kb/s p50 1090 ms, 197/208 shared bursts
     - PASS: 1->2 0.201 116 kb/s p50 894 ms, 1->3 0.132 76 kb/s p50 896 ms, 194/324 shared bursts
   * - ``ofdma-one-idle``
     - FAIL (1->2 PHY frame delivery >= 0.999; node 1: no late TX bursts): 1->2 0.194 224 kb/s p50 880 ms, 0/592 shared bursts
     - PASS: 1->2 0.195 226 kb/s p50 892 ms, 0/589 shared bursts
   * - ``ofdma-one-disconnected``
     - FAIL (UE 2 frame delivery 0.9982 >= 0.999 despite the other UE): 1->2 0.366 211 kb/s p50 891 ms, 1->3 0.000, 34/567 shared bursts
     - PASS: 1->2 0.369 212 kb/s p50 913 ms, 1->3 0.000, 34/567 shared bursts
   * - ``ofdma-unequal``
     - FAIL (1->2 PHY frame delivery >= 0.999; 1->3 PHY frame delivery >= 0.999; node 1: no late TX bursts; flow to UE 2 delivery >= 0.995): 1->2 0.208 7 kb/s p50 9866 ms, 1->3 0.204 204 kb/s p50 10224 ms, 21/623 shared bursts
     - FAIL (flow to UE 2 delivery >= 0.995): 1->2 0.208 7 kb/s p50 9687 ms, 1->3 0.207 208 kb/s p50 10165 ms, 20/625 shared bursts
   * - ``tap-udp``
     - PASS: 1->2 1.000 p50 75 ms, 2->1 1.000 p50 75 ms
     - PASS: 1->2 1.000 p50 87 ms, 2->1 1.000 p50 88 ms
   * - ``tap-tcp-bulk``
     - PASS: TCP 400000/400000 B 323 kb/s
     - PASS: TCP 400000/400000 B 323 kb/s
   * - ``tap-tcp-rr``
     - PASS: RR 150/150
     - FAIL (TCP rr 1->2:6001 131/150 exchanges, none mismatched): RR 131/150
   * - ``tap-bidirectional``
     - FAIL (1->2 PHY frame delivery >= 0.999; 2->1 PHY frame delivery >= 0.999; node 1: no late TX bursts; node 2: no late TX bursts): 1->2 1.000 p50 79 ms, 2->1 1.000 p50 78 ms, TCP 150000/150000 B 253 kb/s, TCP 150000/150000 B 255 kb/s, RR 60/60
     - PASS: 1->2 1.000 p50 96 ms, 2->1 1.000 p50 92 ms, TCP 150000/150000 B 262 kb/s, TCP 150000/150000 B 260 kb/s, RR 60/60
   * - ``tap-three-node``
     - PASS: 3->1 1.000 p50 93 ms, TCP 200000/200000 B 210 kb/s, RR 60/60
     - PASS: 3->1 1.000 p50 101 ms, TCP 200000/200000 B 210 kb/s, RR 60/60
   * - ``tap-tcp-erasure-arq``
     - PASS: TCP 200000/200000 B 269 kb/s
     - PASS: TCP 200000/200000 B 240 kb/s
   * - ``tap-tcp-erasure-noarq``
     - PASS: TCP 200000/200000 B 220 kb/s
     - PASS: TCP 200000/200000 B 199 kb/s

Liquid OFDM scenarios (``-- --phy ofdm -c rs8``)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The OFDMA scenarios need the native OFDMA PHY and are not run with liquid.
This branch passes 22 of the other 26 with liquid. The old branch ran only
``clean-arq`` and ``erasure-arq`` with liquid (``p2-fullstack-8a7bf612``),
so most rows have nothing to compare.

The failures:

* **mandate-expiry**: 511.7 ms against 500 ms, as with native.
* **tap-tcp-rr**: 146 of 150, as with native.
* **clock-skew-sync**: node 2's clock runs 20 ppm fast, which at 1 GHz is a
  20 kHz carrier offset. At 1 MHz bandwidth that is about one liquid
  subcarrier, beyond its acquisition range: 0 of 115 frames decoded in
  either direction. HELLOs never got through, so the clocks never synced.

  The native PHY passes the same scenario. The ppm is the scenario's
  choice; USRP TCXOs (about 2.5 ppm) or a shared reference are well inside
  liquid's range.
* **clock-offset-nosync**: the scenario expects overlapping slots to lose
  frames: node 2's clock is 15 ms ahead with a 10 ms guard, and TX leakage
  is -30 dB.

  * Liquid decoded every frame anyway. The peer's frame is 30 dB above the
    receiver's own leakage, so the impairment the scenario checks for is
    not visible.
  * The native PHY does lose those frames. That is a receiver difference
    worth noting for hardware with strong TX-to-RX leakage.

.. list-table::
   :header-rows: 1
   :widths: 18 41 41

   * - Scenario
     - codex/ofdma-simulation (p2 var-liquid, 8a7bf612)
     - this branch (1d1edf3b, grid62), liquid OFDM rs8
   * - ``clean-arq``
     - PASS: 1->2 1.000 160 kb/s p50 76 ms, 2->1 1.000 161 kb/s p50 78 ms
     - PASS: 1->2 1.000 160 kb/s p50 93 ms, 2->1 1.000 161 kb/s p50 91 ms
   * - ``clean-noarq``
     - (not run)
     - PASS: 1->2 1.000 160 kb/s p50 91 ms, 2->1 1.000 160 kb/s p50 92 ms
   * - ``erasure-noarq``
     - (not run)
     - PASS: 1->2 0.792 127 kb/s p50 90 ms
   * - ``erasure-arq``
     - PASS: 1->2 1.000 154 kb/s p50 84 ms
     - PASS: 1->2 1.000 158 kb/s p50 101 ms
   * - ``ack-loss``
     - (not run)
     - PASS: 1->2 1.000 160 kb/s p50 102 ms
   * - ``asymmetric``
     - (not run)
     - PASS: 1->2 1.000 147 kb/s p50 105 ms, 2->1 1.000 157 kb/s p50 126 ms
   * - ``saturation``
     - (not run)
     - PASS: 1->2 0.335 339 kb/s p50 8803 ms
   * - ``impaired``
     - (not run)
     - PASS: 1->2 1.000 160 kb/s p50 92 ms, 2->1 1.000 160 kb/s p50 88 ms
   * - ``jitter``
     - (not run)
     - PASS: 1->2 1.000 160 kb/s p50 91 ms, 2->1 1.000 160 kb/s p50 93 ms
   * - ``taildrop-saturation``
     - (not run)
     - PASS: 1->2 0.289 341 kb/s p50 518 ms
   * - ``red-saturation``
     - (not run)
     - PASS: 1->2 0.299 337 kb/s p50 2113 ms
   * - ``red-tuned``
     - (not run)
     - PASS: 1->2 0.294 337 kb/s p50 1043 ms
   * - ``mandate-expiry``
     - (not run)
     - FAIL (1->2:5001 max latency 511.7389370000001 ms <= 500 ms): 1->2 0.289 340 kb/s p50 445 ms
   * - ``grant-change``
     - (not run)
     - PASS: 1->2 0.276 285 kb/s p50 8331 ms, 2->1 0.315 326 kb/s p50 13537 ms
   * - ``three-node``
     - (not run)
     - PASS: 1->2 1.000 80 kb/s p50 107 ms, 2->3 1.000 80 kb/s p50 107 ms, 2->1 1.000 8 kb/s p50 110 ms, 2->3 1.000 8 kb/s p50 107 ms, 3->1 1.000 80 kb/s p50 107 ms
   * - ``three-node-erasure``
     - (not run)
     - PASS: 1->2 1.000 80 kb/s p50 116 ms, 2->3 1.000 78 kb/s p50 136 ms, 2->1 1.000 8 kb/s p50 118 ms, 2->3 0.733 6 kb/s p50 114 ms, 3->1 1.000 90 kb/s p50 131 ms
   * - ``clock-offset-nosync``
     - (not run)
     - FAIL (worst link PHY frame delivery 1.000 <= 0.95 (the impairment is visible)): 1->2 0.257 301 kb/s p50 571 ms, 2->1 0.254 297 kb/s p50 573 ms
   * - ``clock-offset-sync``
     - (not run)
     - PASS: 1->2 0.248 291 kb/s p50 589 ms, 2->1 0.248 291 kb/s p50 584 ms
   * - ``clock-skew-sync``
     - (not run)
     - FAIL (1->2 PHY frame delivery >= 0.999; 2->1 PHY frame delivery >= 0.999; node 2 WallClock within 1.0 ms of the time master over the second half (max 15.701 ms; first): 1->2 0.000, 2->1 0.000
   * - ``tap-udp``
     - (not run)
     - PASS: 1->2 1.000 p50 88 ms, 2->1 1.000 p50 89 ms
   * - ``tap-tcp-bulk``
     - (not run)
     - PASS: TCP 400000/400000 B 313 kb/s
   * - ``tap-tcp-rr``
     - (not run)
     - FAIL (TCP rr 1->2:6001 146/150 exchanges, none mismatched): RR 146/150
   * - ``tap-bidirectional``
     - (not run)
     - PASS: 1->2 1.000 p50 95 ms, 2->1 1.000 p50 91 ms, TCP 150000/150000 B 235 kb/s, TCP 150000/150000 B 237 kb/s, RR 60/60
   * - ``tap-three-node``
     - (not run)
     - PASS: 3->1 1.000 p50 105 ms, TCP 200000/200000 B 185 kb/s, RR 60/60
   * - ``tap-tcp-erasure-arq``
     - (not run)
     - PASS: TCP 200000/200000 B 257 kb/s
   * - ``tap-tcp-erasure-noarq``
     - (not run)
     - PASS: TCP 200000/200000 B 195 kb/s

Latency breakdown
~~~~~~~~~~~~~~~~~

Medians from the ``clean-arq`` HDF5 logs (``~/dragonradio-port/tools/latency.py``):

.. list-table::
   :header-rows: 1

   * -
     - Network to air (send log)
     - Demodulation latency (recv log)
     - Application one-way p50
   * - codex/ofdma-simulation (gridgpu01)
     - 44–48 ms
     - 31 ms
     - 76–77 ms
   * - this branch, FD channelizer (default)
     - 44–48 ms
     - 44–45 ms
     - 87–90 ms
   * - this branch, ``--channelizer timedomain``
     - (same send side)
     - (not broken down)
     - 56 ms

Where the logs and results are
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Under ``~/dragonradio-port`` (shared home):

* scenario logs: ``sim-native-1d1edf3b.log``, ``sim-liquid-1d1edf3b.log``,
  ``sim-td-*.log``, ``sim-rr*.log``;
* summaries: ``results/*.summary.json``;
* test outputs: ``tests-1d1edf3b/``.

Full scenario directories (node logs, HDF5 logs, air relay statistics) are
on grid62 under ``/tmp/drport/sim/`` while the reservation lasts.

.. _port-ota:

OTA runner on this branch
-------------------------

Nothing in this section was run against real nodes or radios. The tests
use a fake ssh and ``gridcli``.

* ``grid/make-ota-workspace-image.sh [--ssh CMD] NODE OUT_DIR`` builds the
  workspace that the pipeline installs:

  1. Ship the committed HEAD to a fresh ``/tmp/drport/dragonradio`` on a
     class-image node and build it there.
  2. Run every fullstack test, and ``clean-arq`` (native and liquid) over
     the simulated device.
  3. Tar ``/tmp/drport/dragonradio`` with ``build/dragonradio``, the test
     programs and ``venv/`` (no object files) to
     ``OUT_DIR/ws-SHORT.tar.gz``.

  Campaigns use ``"workspace": "/tmp/drport"``. Stage 2 extracts the
  tarball at that path, where the venv's absolute paths hold.
* ``grid/ota-pipeline.sh RUN CAMPAIGN.json``: unchanged stages apart from
  two points.

  * The controller Python ``PY`` must have numpy and h5py; stage 0 checks
    this.
  * The simulation gate (stage 4) runs ``grid/run-sim-fullstack.sh`` on
    the workspace.
* ``scripts/ota-fullstack.py``: each node script runs
  ``WORKSPACE/dragonradio/build/dragonradio scripts/sim-node.py --hardware``
  with ``VIRTUAL_ENV`` set to the tree's venv. That covers:

  * TDMA for N nodes;
  * the readiness file and the GO barrier;
  * warm-up, duration and drain;
  * results and continuity counters;
  * the timesync master with HELLO and sync tasks.

  A shared PPS (``time_source`` external) adds ``--use-pps``. Clock and
  time sources pass through.
* ``grid/ota-node-health.sh``: read-only probe with the image's
  ``uhd_usrp_probe`` and gr-uhd reference check.
* Tests: ``tests/simulation/test_ota_fullstack.py`` and
  ``test_ota_pipeline.py``, 20 tests, pass on gridgpu01 against the fake
  ssh/gridcli.

What remains
------------

* **Hardware.** Nothing on this branch has run over the air. In order:

  1. ``grid/make-ota-workspace-image.sh`` on a class-image node;
  2. a reservation and an RF approval;
  3. ``grid/ota-pipeline.sh`` with ``STOP_BEFORE=5`` (everything up to RF);
  4. then the trials.

  ``--use-pps`` (``USRP::syncTime(use_pps)``) has no hardware-free test.
* **ofdma-unequal** fails here exactly as on the old branch: UE 2 delivery
  0.21, p50 latency about 10 s. The scenario saturates UE 3 through the
  mandate queue with no mandates installed, so both flows share one growing
  queue. The scenario needs mandates (or another per-flow queue) before it
  can test the 1:2 allocation.
* **mandate-expiry** exceeds its 500 ms bound by 11–13 ms because of the FD
  channelizer's block latency (above). Choices:

  * relax the bound;
  * use ``--channelizer timedomain``;
  * give the FD channelizer smaller blocks (a latency/CPU trade-off for
    Geoffrey to judge).
* **The native PHY on non-finite input.** The native PHY throws on
  non-finite input samples, and it does so in a demodulation thread, which
  terminates the radio. Dropping the chunk and counting it would be safer
  on hardware. Not changed: no input has produced non-finite samples since
  the simulated device's leakage bug was fixed.
* **FFTW wisdom.** Two radios started together on one host race on
  ``/tmp/dragonwisdom``. A failed import is fatal (``importFFTWWisdom``
  raises).
* **FDChannelizer constructed with channels** crashes in
  ``wake_dependents`` (latent; the radio constructs it empty).
* **OFDMA batching** runs only with one modulation thread (the default).
  Up to four held packets are discarded when the synthesizer stops.
* **Not ported:** ``WorkMeter``, the P4 soak (``sim-soak.py``) and the
  sanitizer builds. No TSan or ASan run of this branch has been made.
* **Docs build.** Sphinx is not installed on gridgpu01 or in the image, so
  these pages were not rendered.
