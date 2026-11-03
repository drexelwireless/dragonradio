Native OFDM/OFDMA engine, benchmarks, and SDR path
================================================

Supported paths
---------------

The existing hardware radio's ``--phy ofdm`` selection in ``radio.py`` still
constructs the original liquidDSP OFDM PHY. Its per-packet MCS and FEC support
are retained. The new path is selected explicitly through simulation/frame
commands; it does not silently replace the production MAC or change its wire
format.

The shared experimental frame PHY has both modes:

* ``--phy ofdm`` in ``scripts/sim-radio.py``: one full-band user.
* ``--phy ofdma``: two users share the same OFDM symbols/IFFT.
* ``--engine numpy``: portable NumPy implementation.
* ``--engine native``: compiled C++/FFTW implementation, without liquidDSP/UHD.
* ``--engine auto``: use the compiled engine if present, otherwise NumPy.

The new OFDM mode uses the same occupied bandwidth, FFT, CP, and pilot locations
as the two-user OFDMA mode. The experimental packet format differs from liquidDSP
flexframe; cross-vendor or legacy-radio wire compatibility is not claimed.

Build and run
-------------

On macOS install ``fftw`` for the new engine. For the optional liquidDSP comparison
also install ``autoconf`` and ``automake``. Linux equivalents include a C++ compiler,
``make``, ``libfftw3-dev``, and, for the comparison, autotools and Git.

From the checkout::

    .venv-simulation/bin/python scripts/build-phy.py
    make sim-build sim-test PYTHON=.venv-simulation/bin/python
    .venv-simulation/bin/python scripts/sim-radio.py --phy ofdm --engine native
    .venv-simulation/bin/python scripts/sim-radio.py --phy ofdma --engine native
    .venv-simulation/bin/python scripts/frame-radio.py --mode ofdma --engine native

Linux builds use ``/usr`` as the default FFTW prefix; macOS builds use
``/opt/homebrew``. Override with ``--fftw-prefix`` when needed. The build writes
``build/phy/libdragonradio_phy.{so,dylib}``, a manifest, and a build log. Dynamic
dependencies are FFTW3f and the system C++ runtime; liquidDSP and UHD are absent.
Compile on each target architecture rather than copying the Mac binary to Linux.

The optional comparison library directly compiles the existing
``src/liquid/OFDM.hh`` modem wrappers and the pinned liquidDSP revision, leaving
out the full radio's UHD/TAP/MAC dependencies::

    .venv-simulation/bin/python scripts/build-modem.py --fetch
    .venv-simulation/bin/python scripts/benchmark-phy.py \
      --frames 1000 --iterations 1500 --output simulation-results/benchmark.json

Alternatively pass ``--liquid-source`` with a checkout at the exact pinned hash.
The comparison uses uncoded QPSK/CRC32, a 128-point FFT, 16-sample CP, 1 MS/s,
the same carrier/pilot map, and independent 256-byte payloads. This controlled
comparison is not a comparison against every production FEC/MCS setting.

Benchmark interpretation
------------------------

The report distinguishes three comparisons:

1. The frozen ``tests/reference_phy_v1`` OFDMA implementation versus optimized
   NumPy/native versions. Identical received IQ is used, and TX waveform
   equivalence is checked to a 2e-6 absolute tolerance for the float32 FFTW engine.
2. Single-user native OFDM versus the actual liquidDSP frame modem. Framing is
   different, so both receive equal payload sizes, resource maps, modulation,
   channel parameters, and time-domain SNR, rather than identical IQ samples.
3. AWGN and both configured multipath receiver channels at 10, 15, 20, and 30 dB,
   with exact packet delivery and processing latency measured at every point.

Engine timing excludes construction, FFT planning, channel generation, file I/O,
and radio transport. Warm backend measurements are interleaved. Reports include
median/p95/p99 TX/RX times, per-profile mean/median processing times, delivery
counts, and verified payload bits per measured CPU second. A receiver that fails
acquisition early can take less time than one that successfully decodes; useful
payload throughput, rather than fast rejection, is the per-profile performance
criterion. Standalone timing gates also require the native engines to beat the
old prototype and the matched liquidDSP baseline in the working timing profile.

Finite seeded runs do not establish universal superiority, confidence bounds,
production-FEC equivalence, or OTA reliability. See the saved JSON for actual
results and all acceptance gates, including failed gates if a run regresses.

Receiver optimizations
----------------------

NumPy caches immutable geometry/training/scrambling and vectorizes pilot work.
The native engine additionally reuses FFTW plans, performs FFT-based preamble
correlation, avoids repeated complex division, and keeps sample processing in C++.

For reception, known training carriers estimate the channel. A bank of causal FIR
models up to the CP length is selected using BIC, suppressing estimation noise
without access to the simulator's channel taps. Pilot phases are unwrapped and
fit over each short burst to estimate residual CFO/common phase. This assumes a
channel approximately constant over a frame; fast fading and nonlinear phase
noise remain stress-test work.

When hard decisions fail CRC, the native receiver tries single/double flips among
the four least reliable demapper bits, with at most ten additional CRC checks.
This bounded CRC-aided Chase search changes the receiver, not the transmitted
code rate. It uses no expected payload. Reports include search counts and low-SNR
timings so its cost is visible. Strongly corrupted packets still fail, and CRC
validity is independently checked at the Python packet boundary.

Streaming acquisition rejects normalized-correlation artifacts on zero-energy
windows, which is necessary when searching long intervals of silence. Arbitrary
sample chunks, CRC-failed frame recovery, and leading silence have regression
coverage. Each engine/context belongs to one processing thread; construct modem
instances sequentially and do not concurrently call a shared instance.

Frame and packet integration
----------------------------

``FrameEngine.build`` accepts one packet per allocation and produces a single
``ModulatedFrame`` containing IQ and individual ``PacketRecord`` objects. Packets
may have independent sequence numbers. Records retain offsets and sample counts
for future TX notifications. ``StreamReceiver`` acquires/reassembles frames across
arbitrary sample-transport boundaries and reports absolute sample offsets.

``NativePacketAdapter`` converts existing bound ``NetPacket`` objects to this
frame interface and creates bound ``RadioPacket`` objects on reception. It retains
source/destination, sequence, flags/team, and native packet bodies, including
extended headers. Set capacity large enough for the native body plus one flags
byte (for example 1537 for a 1536-byte body). A supplied native timestamp is
advanced by the acquired sample offset; CFO is returned in radians per sample.

The adapter protocol is regression-tested with packet objects. This is not yet a
replacement for the production C++ MAC's synthesizer/controller wiring, ARQ,
allocation-aware admission, or its timing/record notification path. The full
legacy Linux executable has not been built on this Mac. The new native frame
engine and its simulated streaming path are built and exercised independently.
The actual ``Header.cc`` Python bindings were also compiled in isolation and
checked for integer sequence-number access and range validation.

Two small legacy fixes support isolation/integration: initialize a liquid modem's
initial payload MCS from the supplied header MCS instead of implicitly constructing
an unrelated V27 code, and expose ``Header.seq`` as an integer instead of an
unregistered C++ ``Seq`` object. Configured per-packet MCS selection remains intact.

Future SDR-grid use
-------------------

``SimulatedTransport`` and ``UHDTransport`` consume the same frame IQ and use the
same receiver. UHD is imported only for an explicit hardware command. A simulated
run or ``--dry-run`` does not discover, open, flash, reserve, or transmit through
an SDR.

Package a source bundle for a Linux grid host::

    make grid-package PYTHON=.venv-simulation/bin/python

This produces ``build/dragonradio-frame-grid.tar.gz`` plus a SHA256/file manifest.
The archive excludes virtual environments, secrets, captures, and Mac binaries.
After extracting on an appropriate grid image, install NumPy and FFTW development
headers, then run the hardware-free validation::

    PYTHON=python3 sh grid/validate-simulation.sh

The optional benchmark additionally needs autotools and the pinned liquidDSP
source. The bundle does not launch/stop grid nodes, modify FPGA images, or choose
an operating band. The existing grid images' UHD Python API can be used for the
transport; a separate new UHD build is not required for the frame simulator.

Copy ``grid/sdr-config.example.json`` to a per-node configuration. Fill in the
actual device arguments, experiment frequency, gains, antennas, and reference
sources appropriate for that node. Empty device arguments/frequency are rejected
for live operation. Check the plan without touching hardware::

    python3 scripts/frame-radio.py --mode ofdma --transport uhd --role tx \
      --sdr-config tx.json --dry-run

For an eventual assigned one-TX/two-RX experiment, start each receiver before the
finite transmitter burst::

    # Receiver node 1 (use --user-id 2 on receiver node 2)
    python3 scripts/frame-radio.py --mode ofdma --transport uhd --role rx \
      --user-id 1 --sdr-config rx1.json --duration 10 --output rx1-results.json

    # Transmitter node, once the radio experiment is ready to run
    python3 scripts/frame-radio.py --mode ofdma --transport uhd --role tx \
      --sdr-config tx.json --frames 100 --output tx-results.json

UHD TX applies a single linear peak normalization (default 0.5 full scale), handles
partial writes, timestamps the first sample, sends an end-of-burst marker, and
checks asynchronous errors/burst acknowledgment. RX checks metadata errors,
resets acquisition on discontinuities, and stops streaming on exit. Contract tests
use a mock UHD driver; installed UHD API symbols were checked without opening a
radio. Physical transport behavior and OTA performance remain unvalidated.

The default preamble's CFO capture range is approximately +/-0.444 subcarrier
spacings (about +/-3.47 kHz at 1 MS/s/128). Verify reference accuracy or use shared
frequency references before RF testing. Sample-clock drift, RF clipping/fading,
long-duration operation, and real-time execution on grid CPUs are further tests.
