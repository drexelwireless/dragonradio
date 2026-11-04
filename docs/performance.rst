Software performance (P4)
=========================

Status 2026-11-04. This page covers the cost of every receive and transmit
stage of native OFDM and liquid OFDM through the production PHY classes,
the throughput the streaming demodulators sustain, real-time soaks of the
full radio over the simulated device, and the sanitizer builds. Everything
ran in simulation on gridgpu01: a dual Xeon Gold 6240 (2 x 18 cores, 72
threads), Ubuntu 24.04 container, schedutil governor, portable ``-O2``,
g++ 13.3, FFTW 3.3.10 (AVX codelets in use). No radio was opened.

Methods
-------

**No hardware counters.** ``perf`` is not installed in the container and
``kernel.perf_event_paranoid`` is 4. Host settings were not changed. Stage
shares come from an in-process sampler (``bench/sampler.hh``): a POSIX timer
on ``CLOCK_THREAD_CPUTIME_ID`` raises SIGPROF every 500 us of the measured
thread's CPU. The handler records a backtrace, and ``addr2line -f -i``
resolves inline chains. Each sample is attributed to the outermost
recognized stage function: acquisition/detection, FFT and equalization,
demapping, FEC, CRC, Chase, memory, payload or header bookkeeping. CPU-time
timers fire at the 1 kHz kernel tick, so points are repeated until at
least 3 s of CPU (3000 or more samples); shares below about 1% are noise.

**PHY benchmarks** (``bench/phy_profile.cc``, ``bench/stream_throughput.cc``,
``grid/run-p4-bench.sh``) drive ``NativeOFDM`` and ``liquid::OFDM`` with the
production defaults:

* M = 48, CP = 6, liquid's default mask.
* Native header coded v29; liquid header v29p78 BPSK; soft decoding.
* 1000-byte packets in one stream with 200–600-sample noise gaps.
* Fixed receive chunks; one pinned core, CPU 20.

Per-attempt cost is demodulation CPU divided by frames sent. Per-verified
cost divides by packets delivered with correct bytes. The strong link is
25 dB per sample; the weak link is about 1 dB below each code's 1% PER
crossing from P3, and its measured PER is reported. Before/after comparisons
(``grid/run-p4-ab.sh``) alternate the two builds in randomized order in each
block on the same core.

**Determinism.** Weak-link delivery counts vary by about 1–4% between
processes of the *same* binary, for liquid as well as native (for example
434 against 417 of 1000). The likely cause is FFTW choosing codelets by
buffer alignment; this was not verified. Exactness of the optimizations
therefore rests on ``tests/fullstack/test_native_perf.cc``, not on equal
counts.

Stage costs through the production PHY classes
----------------------------------------------

CPU per attempt (us, mean of two blocks). Shares are of demodulation CPU,
including the idle scan between frames, after the native optimizations
below (commit ``dcc5b66f``). Native numbers for the final code
(``a85e3c10``) are in the "Final code" table further down: uncoded 218,
rs8 319, soft v27 1043, 16-QAM rs8 261.

.. list-table::
   :header-rows: 1

   * - Configuration
     - Strong (25 dB)
     - Weak: SNR, PER, per attempt, per verified
     - Largest stages (strong link)
   * - native QPSK uncoded, Chase off
     - 206
     - 11.0 dB, 25%, 210, 280
     - FFT+eq 28%, payload 26%, acquisition 23%, header FEC 8%
   * - native QPSK uncoded, Chase 4
     - 206
     - 11.0 dB, 2.2%, 215, 220
     - same; Chase adds 2% at the weak point
   * - native QPSK rs8
     - 303 (was 505)
     - 6.8 dB, 57%, 370, 874
     - FEC 26%, FFT+eq 21%, payload 20%, acquisition 17%
   * - native QPSK v27 soft
     - 1019 (was 1263)
     - 3.3 dB, 13%, 1144, 1308
     - FEC 59%, payload 12%, FFT+eq 10%
   * - native 16-QAM rs8
     - 242 (was 442)
     - 13.6 dB, 33%, 300, 443
     - FEC 33%, payload 22%, FFT+eq 16%
   * - liquid QPSK uncoded
     - 544
     - 12.3 dB, 21%, 562, 708
     - FFT+eq 35%, acquisition 33%, demapping 19%
   * - liquid QPSK rs8 (production)
     - 678
     - 8.2 dB, 24%, 756, 992
     - FFT+eq 33%, acquisition 29%, demapping 17%, FEC 12%
   * - liquid QPSK v27 soft
     - 1590
     - 4.5 dB, 10%, 1599, 1787
     - FEC 38%, FFT+eq 24%, acquisition 20%
   * - liquid 16-QAM rs8
     - 632
     - 14.6 dB, 31%, 698, 1015
     - demapping 46%, FFT+eq 18%, acquisition 18%

Idle cost (noise only, same level), per sample: native 11.8 ns (85 MS/s on
one core), liquid 17.3 ns (58 MS/s). Modulation per 1000-byte packet: native
uncoded 119 us and rs8 171 us; liquid uncoded 216 us and rs8 303 us.

**Cold construction** (fresh process, median of five, us):

.. list-table::
   :header-rows: 1

   * - PHY
     - PHY object
     - first modulator
     - first demodulator
     - again (warm): object / modulator / demodulator
   * - native
     - 1328
     - 348
     - 408
     - 405 / 256 / 398
   * - liquid
     - 3
     - 708
     - 221
     - 2 / 24 / 34

Native builds its whole frame engine, including FFTW plans and the
channel-fit pseudo-inverses, for each modulator and demodulator (about
0.4 ms each). It also does so in ``getModulatedSize``, which runs only at
start-up (``SlottedMAC``, ``SmartController``). Liquid builds its engines
lazily.

**Finding: the old "native streaming is slower than legacy" no longer holds.**
Before any change in this phase, native cost less than liquid in every
configuration and at idle. The earlier observation came from the Python
streaming harness of the exploratory study, not the C++ production path.

Native hot-path changes (measured)
----------------------------------

Profiling showed native soft-bit generation (a double ``exp`` and
``lround`` per bit) taking 30–45% of coded demodulation, even for ``rs8``,
whose soft bits the decoder hard-decides anyway. Changes, all exact:

* **Hard path for non-convolutional outer codes.** Payloads whose outer
  code is not convolutional decode from hard decisions. Liquid's soft
  packetizer (and NativeFEC's port) hard-decides those soft bits, so the
  result differs only at an exact LLR tie.
* **Soft-bit table.** ``soft_bits`` counts the 127 rounding thresholds of
  ``lround(255 sigmoid(LLR))`` instead of evaluating it. It equals the old
  formula on 4,001,030 test cases, including both sides of every threshold;
  within 1e-12 of the tie it calls the formula.
* **Persistent workspaces.** The per-frame vectors in header and payload
  decoding are now persistent workspaces.

Paired A/B, CPU per attempt (after/before):

* native rs8: 0.60
* native 16-QAM rs8: 0.55
* native soft v27: 0.81–0.83
* native uncoded: 0.99
* liquid (unchanged control): 1.00

A fourth change made things worse and was reverted (``87ee3a74``). A
"branch-free" finite-input check measured 2.4 ns/sample against 0.8 for the
early-exit loop (it did not vectorize at portable ``-O2``), and the A/B
showed native idle rising from 11.8 to 12.2 ns/sample.

**A regression found by the final A/B, bisected and mostly recovered.**
The final paired A/B (``bin-before`` against the code after every later
commit) showed native demodulation 28% slower than the ``dcc5b66f`` numbers
above: uncoded 266 us, rs8 373 us, soft v27 1108 us. Liquid was unchanged.
Building the benchmark at ``10bf82f3`` and ``5db6052f`` isolated the
constellation commit. Two experiments ruled causes out: the larger
receive buffer sized for BPSK, and forced inlining. The cause was code
generation of the payload demap loop, which held both the QPSK/16-QAM
demapper and the new general demapper behind a runtime branch.
``a85e3c10`` instantiates the loop separately for each and hoists the
scrambler pointer. Results are below; the residual cost against
``10bf82f3`` is 5–10%.

**Final code** (``a85e3c10``) against the original baseline (``d8809b80``):
one paired block, core 20, CPU per 1000-byte attempt.

.. list-table::
   :header-rows: 1

   * - Configuration
     - Strong: before → final
     - Weak: before → final
     - Saturated streaming, MS/s per core: before → final
   * - native QPSK uncoded, Chase off
     - 207 → 218 us (1.05)
     - 213 → 224 (1.05)
     - (Chase 4) 33.8 → 32.1 (0.95)
   * - native QPSK rs8
     - 514 → 319 (0.62)
     - 617 → 387 (0.63)
     - 15.7 → 25.3 (1.61)
   * - native QPSK v27 soft
     - 1274 → 1043 (0.82)
     - 1385 → 1163 (0.84)
     -
   * - native 16-QAM rs8
     - 450 → 261 (0.58)
     - 557 → 317 (0.57)
     -
   * - liquid (all four, unchanged code)
     - 1.00
     - 1.00
     - 12.1 → 12.1 (rs8)

The final code is 5% slower than the baseline for native uncoded, a
residual of the constellation change, and faster for every coded
configuration. In this block the baseline's idle measurement for three
configurations read 31 ns/sample, against 11.7–12.5 in every other run,
and is not used. Native idle in the final code is 12.3–12.5 ns/sample.

Streaming receive throughput
----------------------------

MS/s of capture one demodulator sustains on one core (2048-sample chunks).
Headroom at R MS/s is this divided by R. The waveform does not depend on the
rate. This is the PHY demodulator only, without channelizer, MAC or I/O.

.. list-table::
   :header-rows: 1

   * - Capture
     - native rs8 before
     - native rs8 after
     - native uncoded Chase 4
     - liquid rs8
     - liquid uncoded
   * - noise only
     - 78
     - 78 (after the revert)
     - 82
     - 58
     - 58
   * - bursty (50% airtime)
     - 27
     - 39
     - 51
     - 20
     - 24
   * - saturated (99% airtime)
     - 15.7
     - 25.8
     - 33.7
     - 12.1
     - 13.2
   * - bursty, weak SNR
     - 23
     - 34
     - 49
     - 19
     - 23

* Chunk size (363, 1996, 2048, 16384, 65536 samples: N210 and X310
  network frames, the simulated device, channelizer buffers) changes
  throughput by less than 5%.
* Two demodulators on separate physical cores each lose at most 5%.
* At 5 MS/s, saturated, the demodulator alone has 5.2x headroom for native
  rs8 and 2.4x for liquid rs8.

Real-time soak through the full stack
-------------------------------------

``scripts/sim-soak.py`` (``grid/run-sim-soak.sh``) runs two production radios
at wall-clock pace over the simulated device:

* TDMA, 35 ms slots, 10 ms guard.
* rs8 QPSK with ARQ, the native v29 header and liquid's production v29p78
  header.
* Saturating bidirectional UDP: 70 packets/s per MS/s, 1000 bytes, about
  1.5x one node's slot capacity.
* A tail-drop queue of 15 kB per MS/s.
* Each node pinned to six cores; the relay on its own core.

Every rate ran for 30 minutes. The monitor samples ``/proc`` every 5 s.

**Measuring load.** The channelizer FFT worker, the demodulator workers and
the modulator spin (``_mm_pause``/``yield``) while a streaming buffer fills,
so their CPU time reads near 100% at any load: two cores per node at
1 MS/s. They are now metered (``util/WorkMeter.hh``, exposed as
``_dragonradio.radio.work_meters()``) for the time spent processing.
Threads that block when idle use ``/proc`` CPU. The simulated device
renders noise and the link model on the MAC RX thread inside ``burstRX``;
that is metered (``dr-sim-render``) and subtracted as device-model cost. The
gate requires every radio thread to be at most 50% busy (headroom of at
least 2x), zero late, underflow, overflow or queue-drop device events during
traffic, a relay that is never late, queue occupancy within the tail-drop
limit, and an RSS slope over the second half below 100 kB/min.

**Co-loaded host series** (commit ``636768f0``; native and liquid
in parallel on separate sockets while I compiled and ran other simulations on
the remaining cores). Per node (both nodes agree within 1%):

.. list-table::
   :header-rows: 1

   * - PHY, rate
     - Headroom (busiest thread)
     - Work / spinning CPUs
     - Device model
     - App latency p50 / p99 / p99.9 (ms)
     - Demod latency p50 / p99 (ms)
     - Queue p99 (packets)
     - Late TX / late RX during 30 min
     - RSS HWM
   * - native 1 MS/s
     - 32x (dr-logger, dr-demod0)
     - 0.10 / 1.80
     - 7%
     - 496 / 532 / 669–689
     - 35 / 56
     - 16 (limit 15)
     - 6 and 18 / 3 and 4
     - 160 MB
   * - liquid 1 MS/s
     - 20x (dr-demod0)
     - 0.14 / 1.82
     - 7%
     - 521 / 588 / 724–947
     - 37 / 56
     - 16
     - 10 and 15 / 7 and 0
     - 156 MB
   * - native 2 MS/s
     - 17x (dr-demod0)
     - 0.19 / 1.94
     - 13–16%
     - 454 / 496 / 593–599
     - 20 / 30
     - 31 (limit 30)
     - 3 and 4 / 0 and 9
     - 194 MB
   * - liquid 2 MS/s
     - 10x (dr-demod0)
     - 0.26 / 1.98
     - 14%
     - 463 / 501–506 / 618–624
     - 21 / 29
     - 31
     - 6 and 4 / 2 and 6
     - 190 MB
   * - native 5 MS/s
     - 7.1x (dr-demod0)
     - 0.45 / 2.08
     - 34–39%
     - 432 / 472 / 501–570
     - 11 / 15
     - 76 (limit 75)
     - 1 and 1 / 9 and 46
     - 250 MB
   * - liquid 5 MS/s
     - 4.2x (dr-demod0)
     - 0.63 / 2.16
     - 34–35%
     - 432 / 472 / 472–570
     - 11 / 14
     - 76
     - 0 and 1 / 6 and 0
     - 247 MB

Application latency is dominated by the tail-drop queue (15 kB per MS/s)
under saturation. "Demod latency" is the production ``demod_latency``: frame
start to delivery, including receive buffering. Modulation took 0.53 ms
(native) and 0.89 ms (liquid) per packet at p50, under 0.6 and 0.95 ms at
p99, at every rate. Delivery: about 77,000, 150,000 and 386,000 packets per
direction at 1, 2 and 5 MS/s, with no duplicates or corrupt payloads.
Context switches per second on the MAC RX thread scale with the rate (124,
250, 620).

* **Every run had a few late bursts and failed the zero-late gate.** In the
  1 MS/s runs, native's and liquid's late TX bursts happened at the same
  moments (to the 5 s monitoring resolution), although they ran on separate
  sockets with separate relays. They also stopped once my heavy compiles
  stopped. Host interference, not either PHY, is the cause. During part of
  the 1 MS/s run my compiles also ran on hyperthread siblings of the soak's
  cores (a mistake), so that run is the most disturbed.
* **RSS is not a leak.** It moves in steps of +15 to +22 MB, and at 1 MS/s
  once by -22 MB, at the same moment on both nodes and both PHYs, which is
  malloc arena growth and trimming. A linear RSS slope is the wrong test;
  the clean series tracks heap bytes in use (``mallinfo2``) instead.

**Quiet host series** (``9e0cdc53``, same layout, nothing else running;
10 MS/s for 10 minutes):

.. list-table::
   :header-rows: 1

   * - PHY, rate
     - Headroom (busiest radio thread)
     - Work CPUs
     - Device model (MAC RX thread)
     - App latency p50 / p99 / p99.9 (ms)
     - Demod latency p50 / p99 (ms)
     - Queue p99 (limit)
     - Radio TX late / underflow / overflow (node 1, node 2)
     - Late at receiver; at relay
     - Heap slope, 2nd half (kB/min)
   * - native 1 MS/s
     - 31x (dr-demod0)
     - 0.10
     - 7%
     - 496–501 / 532 / 537
     - 36 / 56
     - 16 (15)
     - 1, 0 / 0 / 0
     - 0; 0
     - 9, 11
   * - liquid 1 MS/s
     - 20x (dr-mod0, dr-demod0)
     - 0.14
     - 7%
     - 521 / 588 / 593
     - 37 / 55
     - 16 (15)
     - 1, 0 / 0 / 0
     - 0; 0
     - 28, -10
   * - native 2 MS/s
     - 16x
     - 0.20
     - 14%
     - 454 / 496 / 496
     - 20 / 31
     - 31 (30)
     - 0, 1 / 0 / 0
     - 2, 5; 24, 19
     - 1547, 1577 (HDF5 cache)
   * - liquid 2 MS/s
     - 10x
     - 0.26
     - 13–14%
     - 458–463 / 501 / 506
     - 20 / 29
     - 31 (30)
     - 0, 0 / 0 / 0
     - 5, 1; 6, 4
     - 1370, 1388 (HDF5 cache)
   * - native 5 MS/s
     - 6.3x
     - 0.47
     - 34%
     - 432–436 / 472 / 477
     - 11 / 15
     - 76 (75)
     - 0, 1 / 0 / 0
     - 29, 46; 71, 53
     - 1425, 1480 (HDF5 cache)
   * - liquid 5 MS/s
     - 4.2x
     - 0.62
     - 31–34%
     - 432 / 472 / 486–501
     - 11 / 14
     - 76 (75)
     - 2, 2 / 0 / 0
     - 34, 37; 52, 36
     - 1421, 1425 (HDF5 cache)
   * - native 10 MS/s (10 min)
     - 3.2x
     - 0.93
     - 66%
     - 423 / 432 / 436
     - 8 / 10
     - 151 (150)
     - 0, 0 / 0 / 0
     - 6, 0; 0, 0
     - 4177, 4142 (HDF5 cache)
   * - liquid 10 MS/s (10 min)
     - 2.1x
     - 1.19
     - 60–65%
     - 427 / 463 / 463–467
     - 8 / 9
     - 151 (150)
     - 0, 0 / 0 / 0
     - 0, 6; 0, 0
     - 5789, 5775 (HDF5 cache)

Delivery was 77k, 150–154k, 386k and 257–263k packets per direction. There
were no duplicates, no corrupt payloads, and no device overflows or queue
drops anywhere.

* **CPU headroom.** At least 2x for both PHYs at every rate through
  10 MS/s. Native: 31x, 16x, 6.3x and 3.2x. Liquid: 20x, 10x, 4.2x and 2.1x
  (marginal at 10 MS/s). Liquid uses about 1.3–1.5x native's work CPU at the
  same rate. The busiest thread is the demodulator worker.
* **Late events.** The radios' own late TX bursts were 0–2 per node per run.
  The one in each 1 MS/s run happened at the same instant on both PHYs, when
  I created a git worktree (a disk-write burst) on the otherwise idle host.
  Without real-time priority (no CAP_SYS_NICE in the container), a
  scheduling delay of a few milliseconds is enough. The "late at receiver"
  and "late at relay" counts are the simulated air path: a burst written by
  ``SimUSRP`` on time reached the Python relay up to 20 ms late, against a
  14 ms mean lead. That affected at most 124 of 388k bursts (0.03%) at
  5 MS/s.
* **Heap growth is HDF5's chunk cache, bounded.** Heap in use grows in steps
  of about 16 MB, one ``ExtensibleDataSet`` chunk (131,072 rows of about
  120 bytes), at times proportional to the packet rate. The logger gives
  each dataset a 64 MB raw-data chunk cache (``Logger.cc``
  ``kRDCCNumBytes``). A standalone check with the same settings
  (a small program writing 120-byte rows through ``ExtensibleDataSet``) plateaus at 121 MB for two datasets after 500k rows
  each and stays flat to 2.5M. The 2–10 MS/s runs had not reached that bound
  in 10–30 minutes; at 1 MS/s it is flat (9–28 kB/min). This is not a leak,
  but a long logged run costs up to 64 MB per large dataset. (Changed
  2026-11-04: 8 MB per dataset and 16,384-row chunks; see
  :doc:`defaults-2026-11`.)
* **Verdict.**

  - Strict gate (at least 2x, zero late or overflow, bounded queues, no
    growth): not met at any rate, because of a handful of late events.
    The radio-attributable ones are 0–2 TX bursts per 30 minutes; the rest
    are from the simulated air path.
  - Headroom and bounded queues: met by both PHYs through 10 MS/s.
  - Highest rate with at least 2x: 10 MS/s for both (native 3.2x, liquid
    2.1x); higher rates were not tried. The simulated device's rendering
    already takes 66% of a core at 10 MS/s, so this simulator would limit
    a higher-rate soak before the radio does.
  - On hardware, run with real-time priority and repeat the soak there;
    simulation cannot certify UHD's transport.

Defects found by profiling and the sanitizers
---------------------------------------------

1. **Slot synthesizer re-modulated a packet that did not fit** (production;
   affects hardware runs; ``f0f0f3b8``). ``UnichannelSynthesizer``, which the
   TDMA timedomain and freqdomain synthesizers use, kept a packet that did
   not fit in the current slot. While the slot was still open it modulated
   that packet again in a tight loop, although the packet can never fit
   later in the same slot. Under saturation each node burned a whole core:
   the first soak showed ``dr-mod0`` 100% busy at 1 MS/s, and the
   regression test counts 2601 modulations of one packet in 200 ms. The
   worker now leaves the slot and carries the packet to the next one, as
   ``MultichannelSynthesizer`` already did. Transmitted frames are
   unchanged. ``tests/fullstack/test_synth_slot.cc`` fails before (2601
   modulations) and passes after (2).
2. **Misaligned sequence numbers in control messages** (production; affects
   hardware builds; ``b37cfae9``). UBSan reported a misaligned member call in
   ``SmartController::handleSelectiveACK``. ``ControlMsg`` is packed with the
   union at offset 1, so every ``Seq`` in it sits at an odd address, and
   ``Seq``'s comparison operators were called on it with alignment 2, which
   is undefined behavior. ``Seq`` is now packed (alignment 1); the sizes and
   the packed wire layouts are unchanged. ``tests/fullstack/test_packet_align.cc``
   fails before (four misaligned fields) and passes after.
3. **Only MandateQueue timestamps the queue** (reported, not changed). The
   send log's ``enqueue_latency``, ``dequeue_latency`` and ``queue_latency``
   are filled only by ``MandateQueue``. With the FIFO, tail-drop or RED
   queues ``queue_latency`` is a large negative number. The soak uses
   ``llc_latency`` (network arrival to the controller's pull) instead.

Observability added (no behavior change): every production thread is named
(``dr-mac-rx``, ``dr-chan-fft``, ``dr-demodN``, ``dr-modN``, ``dr-logger``,
``dr-timer``, ``dr-workN``, ``dr-sim-reader`` and others; see ``top -H``),
and the work meters above.

Correctness builds
------------------

``grid/build-sanitized.sh WORKSPACE asan|tsan OUTDIR`` builds the production
objects with ASan+UBSan or TSan in ``obj-asan``/``obj-tsan`` and links
``dragonradio-asan``/``-tsan``. It runs every ``tests/fullstack`` program, the
embedded-Python tests, and full-stack scenarios with the instrumented radio
as both nodes. Suppressions cover only the uninstrumented CPython runtime and
pybind11's interpreter state.

Results, from a frozen snapshot of ``9e0cdc53`` (ASan) and of ``d8758817``
(TSan rerun after the fixes):

* **ASan + UBSan: clean.** Zero reports in all ten ``tests/fullstack``
  programs, the embedded-Python test, and the ``clean-arq``, ``erasure-arq``
  and ``saturation`` scenarios with the instrumented radio as both nodes.
  The first ASan run (on a tree I was still editing) found the misaligned
  ``Seq`` accesses (fixed, ``b37cfae9``) and an uninitialized header in a
  new test of mine (fixed, ``a9718c8d``).
* **TSan needs ASLR off per process.** Under this kernel's high ASLR
  entropy, GCC 13's TSan runtime aborts with "unexpected memory mapping".
  ``build-sanitized.sh`` runs each TSan process under ``setarch -R``; no host
  setting is changed.
* **TSan found real data races.** Fixed:

  - ``IQBuf::waitToStartFilling`` used relaxed loads, so the channelizer's
    reads of the buffer timestamp, frequency and rate raced with the
    receive thread (``d8758817``; production; affects hardware builds;
    same instructions on x86).
  - ``SafeQueue::stop`` set its flag outside the mutex, which also made a
    lost wakeup possible at shutdown. The ``WorkQueue`` and ``TimerQueue``
    stop flags were plain bools (``cf3f8210``; production).
  - The simulated clock's settings (this phase's code) are now atomic.

  Open (pre-existing, reported, not changed tonight):

  - ``NetPacket`` fields written by the modulator (team flag, modulation
    timestamps) and by the MAC (TX timestamp, sample counts) while the
    controller and logger read the same packet, which ARQ keeps for
    retransmission. This is the same with liquid and native.
  - ``IQBuf::resize`` by the receive thread while the channelizer reads
    ``size()``.
  - ``uhd::time_spec_t`` timestamps copied across threads without
    synchronization.
  - A signal-unsafe call inside a signal handler.

  After the fixes, ``clean-arq`` had 7 reports, ``ofdma-both-strong`` 9 and
  ``clock-skew-sync`` 30, all in these classes. The TSan scenarios fail their
  timing checks, as expected, because TSan slows the radio about 5–15x.

Reproducing
-----------

::

    grid/build-fullstack.sh WS dragonradio                 # objects and tests
    grid/run-p4-bench.sh WS OUT [core] [packets]          # stage profile, cold, streaming
    grid/run-p4-ab.sh WS A_BIN B_BIN OUT [blocks] [core]  # paired before/after
    grid/run-sim-soak.sh WS OUT --phy native --rate 1 --duration 1800 \
        --cores1 0,2,4,6,8,10 --cores2 12,14,16,18,20,22 --cores-air 24
    grid/build-sanitized.sh WS asan OUT; grid/build-sanitized.sh WS tsan OUT
    venv/bin/python scripts/report-performance.py --bench OUT --ab AB --soak SOAK --output report.html

Results of this phase, with the HTML report (``report.html``; the
co-loaded soak in ``report-coloaded-soak.html``), are in
``/tmp/dragonradio-fullstack/results/p4-2026-11-04/`` on gridgpu01 (not
committed). The report was checked for layout programmatically; no browser
was available on the host to view it.

Packages installed into the workspace venv this phase: ``docutils`` 0.23
(to check these pages). ``cairosvg`` was installed and removed (the host
has no cairo library). ``h5py`` was already present.

Pin timing runs away from other work, and away from the hyperthread
siblings of their cores (on gridgpu01, CPU N and N+36 share a physical
core; even CPUs are socket 0, odd are socket 1). See the soak section for
what happened when this was not done.
