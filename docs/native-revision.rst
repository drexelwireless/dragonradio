Native OFDM/OFDMA revision and benchmark plan (v2)
==================================================

Prepared 2026-11-02. Scope: the hardware-free native PHY in ``src/simulation``
and its Python wrappers. Production ``--phy ofdm`` and the pinned liquidDSP
comparator are unchanged. Nothing here opens an SDR or transmits RF.

Baseline evidence (v1)
----------------------

* **Error rate.** In AWGN, native v1 tracks the independent hard-bit QPSK packet
  curve, slightly better at the waterfall from bounded CRC-aided Chase
  detection. Rayleigh and beyond-CP losses come from the channel, not the
  demodulator; without FEC or diversity they cannot be removed by receiver tuning.
  The receiver-limited loss is **CFO acquisition**. v1 acquires only within about
  +/-0.25 subcarrier, because coherent correlation over the 288-sample preamble
  decorrelates. NCO-corrected liquid acquires within +/-0.55.
* **Computation.** v1 streaming spends 56 ms of CPU per second of idle noise
  (macOS arm64, 4096-sample chunks); legacy liquid spends 9 ms. Each 256-sample
  hop crosses Python/ctypes and runs a 1024-point FFT correlation.
* **Throughput.** Native framing already beats liquid at every payload size
  (256 B: 0.948 versus 0.837 payload bits per sample). Further gains need a
  higher-order constellation and fewer lost packets, not framing changes.

Revisions
---------

R1. CFO-robust acquisition (error rate, CPU).
    A Schmidl-Cox autocorrelation over the two identical training blocks (lag one
    symbol, 144 samples) gives a CFO-invariant detection metric with O(1) running
    updates. A coarse CFO comes from its phase. Fine timing then maximises a
    segmented, CFO-derotated template correlation (8 segments) in a small window.
    The integer ambiguity of the coarse CFO (multiples of 0.889 subcarrier) is
    resolved by testing three hypotheses with a full coherent correlation. Target
    range: about +/-1.3 subcarrier. Waveform unchanged.
R2. Persistent C++ streaming receiver (CPU).
    The engine owns a bounded buffer, scans every sample with the running metric,
    and decodes only at local maxima above threshold. Python crosses the C ABI once
    per transport chunk, not once per 256-sample hop. Semantics preserved: absolute
    sample offsets, recovery after CRC failures, first-packet acquisition after
    noisy idle time, and a bounded buffer.
R3. Static 16-QAM option (throughput).
    Gray-mapped 16-QAM on data tones; pilots and training unchanged; max-log bit
    reliabilities for Chase. It is configured out of band (``modulation="16qam"``)
    and mirrored in the NumPy reference PHY. The liquid bridge gains a matched
    16-QAM payload mode (QPSK header). QPSK remains the default and is
    sample-identical to v1.
R4. Chase ablation switch (``chase_bits``; 0 disables) for explicit attribution
    of error-rate gains and their CPU cost.

The v1 engine is frozen in ``tests/reference_native_v1`` and built as a
separate library (``scripts/build-phy.py --variant reference-v1``), so every
before/after comparison uses identical inputs in the same process environment.

Acceptance gates (declared before measurement)
---------------------------------------------

* All existing regression tests pass. QPSK v2 TX waveform is byte-identical
  to v1. Zero false accepts in every study.
* CFO: 100% delivery, noiseless, across at least +/-1.0 subcarrier for OFDM and
  both OFDMA users.
* Error rate: v2 PER no worse than v1 within Wilson 95% intervals at every
  AWGN/multipath/Rayleigh/SFO point where v1 had at least 50 packets.
* CPU: v2 streaming CPU per second of airtime at most v1/3, and below legacy
  liquid, at 4096-sample chunks, for idle noise and for loaded traffic.
* Throughput: 16-QAM v2 peak goodput at least 1.8x QPSK v2 at 30 dB AWGN; PER
  curves consistent with Gray 16-QAM theory in the oracle experiment.

Benchmark design (gridgpu01)
---------------------------

Run CPU-only on ``gridgpu01`` (Ubuntu 24.04, 2x Xeon Gold 6240) from a private
workspace under tmux, so the run survives the laptop disconnecting. Build both
native variants and the pinned liquid bridge on that host; no system install.
The plan's existing Linux validation path supplies FFTW and NumPy in the
workspace.

1. Oracle BER versus theory: QPSK and 16-QAM, known timing/channel, Eb/N0 sweep.
2. Practical PER and goodput versus sample SNR: legacy, native v1 and v2,
   OFDM and OFDMA, QPSK and 16-QAM (v2/legacy), seven channel profiles,
   10 seeds x 100 packets per point (1000 per user), 256-byte payloads.
3. CFO sweep -1.5..+1.5 subcarrier, noiseless and 20 dB.
4. Payload sweep 64/256/1024/1500 bytes: nominal and measured goodput.
5. Chase ablation (0 vs 4 bits): PER and CPU per packet.
6. Software: warm TX/RX kernel medians and p99, and streaming CPU per airtime
   second at 512/4096/65536-sample chunks for idle noise and saturated traffic.
   Each backend runs in its own process pinned to one isolated core, in
   alternating randomized blocks.

Quality cells run in parallel worker processes, one cell per process. Timing
runs sequentially on otherwise idle cores. Every report records source and
library hashes, host, compiler and seeds. Results are simulation only: no
OTA, full-stack, FEC-parity or real-time claims follow from them.

Results, 2026-11-03 (executed on gridgpu01)
-------------------------------------------

Run ``native-revision-20261103-0200-4146d151`` at commit ``4146d151`` on
gridgpu01 (2x Xeon Gold 6240, Ubuntu 24.04), unattended under tmux. Simulation
only; no SDR opened, no RF transmitted, no reservation created. Results are in
``simulation-results/grid/`` (git-ignored). The report is regenerated with
``python scripts/report-native-revision.py --run <run dir> --output report.html``.

Nine of eleven declared gates were met:

* Regression suite: 59 tests passed (1 skipped: the C++ header test needs
  system FFTW headers). QPSK v2 frames byte-identical to v1.
* No false accepts in 3,456,000 decode attempts.
* Oracle BER within six sigma of Gray QPSK and Gray 16-QAM theory at all 30
  Eb/N0 points. An earlier run calibrated noise from per-packet measured Es,
  which biased 16-QAM BER 1-3% high; the harness now references nominal Es = 1.
* CFO: every packet delivered from -1.30 to +1.30 subcarrier (v1 +/-0.25,
  pinned liquid +/-0.55 on x86_64), noiseless and at 20 dB.
* Packet delivery never worse than v1 within 95% intervals; identical
  everywhere except +/-0.6 subcarrier CFO, where only v2 decodes.
* Streaming CPU, 4096-sample chunks, two receivers: idle 29 ms per airtime
  second (v1 177, liquid 36); saturated OFDM 74 ms (v1 227, liquid 147).
* **Not met:** saturated OFDMA CPU 50 ms versus v1 64 ms (1.3x, target 3x).
  v1 was already decode-bound there and v2's per-sample scan alone costs
  about 29 ms for two receivers. Still 2.9x below liquid.
* **Not met:** 16-QAM OFDM goodput 1.67x QPSK at 256 bytes (target 1.8x); two
  fixed training symbols cap the ratio at 15/9. 1.92x at 1500 bytes. OFDMA met
  the target at 1.80x (1.90 Mb/s).

Per-frame kernels: v2 OFDM receive 69 us (v1 88, liquid 198), transmit 43 us
(v1 45). Chase search lowers the 90%-delivery SNR from 12 to 10 dB in AWGN at
no cost when CRC passes. An earlier complete run agreed on delivery in all
1296 cells and on streaming CPU within 2.2%.

Remaining limits: Rayleigh and beyond-CP losses need coding or diversity; no
OTA, MAC/ARQ, FEC-parity or real-time scheduling claim follows from these runs.
