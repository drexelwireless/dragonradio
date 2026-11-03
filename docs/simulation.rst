Simulation and static OFDMA prototype
=====================================

Scope
-----

The initial reference implementation described here now also has a compiled
non-liquidDSP engine, a single-user OFDM mode, and a shared streaming/SDR transport
interface. See :doc:`native-phy` for the current build, benchmarks, and integration
scope. ``--engine numpy`` explicitly selects the original portable path;
``--engine auto`` selects the compiled engine when it has been built.

``scripts/standalone-radio.py --simulate`` selects a portable sample-domain PHY
experiment before importing the native radio runtime. It does not instantiate a
USRP, create TAP interfaces, install system packages, or transmit RF. The default
``--phy ofdma`` is a new NumPy reference PHY inside ``dragonradio.simulation``.
It tests a real composite IQ waveform, not a packet-delivery probability model.

This is the first simulation milestone for an OFDMA extension, not an OFDMA
implementation in the production C++ scheduler. It does not exercise the legacy
MAC, ARQ, packet compressor, network interfaces, resamplers, or real-time threading.
The separate ``--phy liquid-ofdm`` baseline exercises the existing liquidDSP modem
when a compiled DragonRadio runtime is available; it never silently falls back to
the reference PHY.

Quick start
-----------

Run these commands from the checkout, using Python 3.10 or newer::

    python3 -m venv .venv-simulation
    .venv-simulation/bin/python -m pip install -r requirements-simulation.txt
    make sim-build sim-test PYTHON=.venv-simulation/bin/python
    make sim PYTHON=.venv-simulation/bin/python

Equivalent direct commands::

    .venv-simulation/bin/python scripts/standalone-radio.py --simulate --frames 200
    .venv-simulation/bin/python scripts/sim-radio.py --profile ideal --frames 100

The legacy installation pins are deliberately separate. Do not install the old
``python/dragonradio/setup.py`` dependency set into this environment. No native
submodules are required for the portable simulation. The upstream native build
targets Ubuntu 20.04 and requires additional native dependencies; a successful
portable simulation does not imply that native build succeeds on macOS.

Waveform and packet format
-------------------------

The default frame uses a 128-point FFT, 16-sample cyclic prefix, and 1 MS/s rate.
Signed carrier indices refer to the FFT grid; DC and Nyquist are unused.

* User 1 occupies carriers -52 through -5. Its pilots are -52, -40, -28, -16.
* User 2 occupies carriers +5 through +52. Its pilots are +5, +17, +29, +41.
* Each user has 44 data carriers and four pilots per data symbol.
* Two identical, known full-band training symbols precede the data symbols.
* Each user's packet has magic/version ``DOF1``, uint8 source and destination,
  uint32 sequence, uint16 payload length, payload, and CRC32 over header+payload.
  Integer fields use network byte order. This experimental format is not
  wire-compatible with the existing DragonRadio liquidDSP frames.
* Uncoded QPSK maps independent user bit streams into the same OFDM symbols.
  One IFFT per symbol creates the composite waveform. Short packets are padded;
  a fixed per-user PRBS scrambles the packet and padding bits to avoid coherent
  peaks from long runs of identical symbols. This is not encryption or FEC.
* A fixed 256-byte maximum payload gives 25 data symbols, or 3888 samples per
  frame. Both user packets share a transmitter and may have independent sequence
  numbers.

Receivers know the static configuration and training sequence. They do not get
the transmitter's payload, timing, CFO, or channel taps. Known-preamble correlation
acquires the burst; phase between repeated training symbols estimates CFO; the
training FFT estimates each occupied subcarrier's channel. User-specific pilots
track common phase on each subsequent symbol. Each receiver only decodes its own
allocation and validates its header and CRC. The experiment runner compares the
decoded packet against the original only after reception, to detect misdelivery.

The repeated-symbol CFO estimator is unambiguous for offsets smaller than
``nfft / (2 * (nfft + cp_len))`` subcarrier spacings (about 0.444 by default).
Acquisition can fail inside that bound with adverse multipath/noise. The initial
validated profile uses offsets +0.12 and -0.18, strong direct paths, and channel
delay spread shorter than the cyclic prefix. This is not a general radio burst
acquisition solution.

Channel experiments
-------------------

The identical TX waveform fans out through two independent receiver channels.
Each channel applies an FIR, integer arrival delay, CFO, phase rotation, and
complex AWGN. Random payload and noise streams use separate deterministic seeds.
SNR is mean post-FIR active-burst sample power divided by complex noise power;
it is not Eb/N0 or the SNR on a particular data carrier.

Profiles:

* ``ideal``: noiseless unity channels, no delay or CFO.
* ``awgn``: 30 dB SNR by default, unity channels.
* ``multipath``: different short FIRs, 17/31-sample delays, +0.12/-0.18 subcarrier
  CFOs, different phases, and 30 dB SNR.

Examples::

    .venv-simulation/bin/python scripts/sim-radio.py --profile multipath \
      --frames 1000 --output simulation-results/multipath.json \
      --export-iq simulation-results/first-frame.npz

    .venv-simulation/bin/python scripts/sim-radio.py --profile awgn \
      --snr-sweep=-5,0,5,10,15,20,25,30 --frames 300 \
      --output simulation-results/snr-sweep.json

    .venv-simulation/bin/python scripts/sim-radio.py --profile awgn \
      --cfo-subcarriers 0.1 -0.15 --delay-samples 25 41 \
      --snr-db 25 --frames 200

Exit status is 0 when every receiver meets ``--max-per`` (default 1%) and no wrong
CRC-valid packets are observed; 1 means the acceptance threshold failed; 2 means
configuration or runtime failure. A sweep containing deliberately bad SNR points
therefore normally exits 1. ``--max-per 1`` permits exploratory sweeps but should
not be used as a performance acceptance test.

Reports include exact configuration, seeds, runtime versions, upstream commit,
per-user delivery counts and PER, failure reasons, timing/CFO errors, decision-
directed EVM, waveform PAPR, and aggregate delivered payload goodput. Goodput is
based on simulated waveform duration; it excludes inter-burst scheduling gaps,
ARQ, host processing time, and hardware overhead. EVM covers padded data symbols
and uses nearest constellation decisions, so it is not a reliable quality measure
when decoding fails. Empirical PER from a finite run is not a confidence guarantee.

The optional NPZ has ``tx``, ``rx_1``, ``rx_2``, ``payload_1``, and ``payload_2``.
The JSON report carries the waveform configuration. Payload arrays are evaluation
ground truth and must not be supplied to the receiver. Replay a default-config
capture independently::

    PYTHONPATH=python/dragonradio .venv-simulation/bin/python - <<'PY'
    import numpy as np
    from dragonradio.simulation import OFDMAReceiver
    with np.load('simulation-results/first-frame.npz', allow_pickle=False) as capture:
        for user in (1, 2):
            result = OFDMAReceiver(user).demodulate(capture[f'rx_{user}'])
            print(user, result.ok, result.error)
    PY

Native DragonRadio modem baseline
--------------------------------

After building the upstream radio on a supported Linux host::

    ./dragonradio scripts/sim-radio.py --phy liquid-ofdm --profile ideal \
      --frames 100 --output simulation-results/native-liquid.json

This creates the actual bound ``OFDMModulator`` and ``OFDMDemodulator``, sends their
IQ through the same channel model, and feeds RX in 97-sample chunks to exercise
incremental synchronization. It uses two sequential single-user frames as a
baseline, with default subcarrier allocation and uncoded QPSK/CRC32. It does not
claim to be multi-user OFDMA or a full-stack simulation. If ``_dragonradio`` is
unavailable it fails explicitly. This optional backend must be validated on a
native build; portable tests cannot certify its native dependencies.
The native payload contains the simulation packet envelope so source, destination,
sequence, and payload can be verified without relying on the upstream Python
binding for the native ``Seq`` type.

Tests and next integration steps
-------------------------------

The regression suite checks exact payloads, empty/max-size packets, unequal
allocations, inactive carriers, timing/CFO recovery, seeded noise, CLI exit codes,
saved-IQ replay, and high/low SNR behavior. A deliberate corruption confined to
one user's data must fail that user's CRC while leaving the other user intact.

Before radio testing:

1. Establish the native liquidDSP baseline on a supported Linux build.
2. Introduce a frame builder at the C++ synthesizer boundary accepting multiple
   ``NetPacket`` objects and producing one composite IQ buffer with per-packet
   TX records. The existing single-packet modulator API cannot represent this
   frame operation without changes.
3. Make admission and size estimation depend on allocated subcarriers/symbols,
   and map decoded packets back to ``RadioPacket`` with correct timing/statistics.
4. Add sample-clock drift, acquisition stress cases, long captures, fading,
   clipping/PAPR backoff, and streaming-state tests. Measure real-time throughput
   before selecting a host implementation or FPGA offload.
5. Validate saved IQ and then a cabled single-transmitter/two-receiver setup.
   Dynamic grants, over-the-air allocation signaling, FEC/link adaptation, and
   multi-transmitter uplink OFDMA are subsequent milestones.

The simulation runner never invokes the hardware radio path. Exported samples
are not automatically transmitted.
