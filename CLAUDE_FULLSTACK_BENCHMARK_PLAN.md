# Full-stack DragonRadio development and benchmark execution plan

Prepared 2026-11-02. This file is a handoff for Claude Code working in the
DragonRadio repository, normally `/Users/krd26/VSCodeProjects/dragonradio`.
Read this file, `docs/ota-grid.rst`, and `docs/characterization.rst` before work.
The user wants a complete radio, reproducible simulation, and mixed N210/X310
OTA comparisons. **Do not treat the existing PHY demonstration as a complete
network stack, or simulation results as RF results.**

## 1. Starting state and non-negotiable distinctions

- Branch: `codex/ofdma-simulation`; upstream starting revision
  `da3808d267b54995a6ef95f317c66d1d1b4f22a9`. Much of this work is uncommitted,
  including untracked source/tests/docs. Inspect `git status --short`; preserve
  it all. Never reset/clean the checkout to manufacture a baseline. Create a
  recoverable checkpoint including untracked files before refactoring.
- Production `--phy ofdm` constructs `dragonradio.liquid.OFDM` in
  `python/dragonradio/dragonradio/radio/radio.py::Radio.mkPHY`. Preserve it.
- The new engine is `src/simulation/FastPHYCore.hh` (FFTW, no liquid/UHD; extracted
  from `FastPHY.cc` in P1 step 1), with a thin C ABI in `src/simulation/FastPHY.cc`
  and ctypes bindings in `simulation/fast.py`. It implements static uncoded QPSK OFDM and
  two-user downlink OFDMA. It is not currently selected by the production MAC.
- `simulation/frame.py::NativePacketAdapter` retains native packet fields at a
  frame boundary; it is **not** a completed C++ PacketModulator/PacketDemodulator.
- `simulation/liquid.py` and `src/simulation/LiquidBridge.cc` run real original
  DragonRadio OFDM wrappers at pinned liquidDSP revision
  `d39f1c0f738c2adca23dec9caa63e7c5bdcebfcb`, isolated from the full MAC/UHD.
- Current bridge comparison: CRC32 + uncoded QPSK, header and payload. Production
  defaults include payload `rs8` and header `v29p78`; neither is parity-tested
  against a native FEC implementation yet. No claim of production replacement.
- A native OFDM waveform and the legacy OFDM waveform are different air
  interfaces. Interoperability between them is not implied. Run homogeneous
  backend endpoints first; reject mismatched modes explicitly.
- “Native PHY” means no liquid algorithms in its modulation/demodulation path.
  Shared stack filters/resamplers and legacy builds may still depend on liquid.
  Audit transitive dependencies separately; do not claim the whole executable
  is liquid-free because `libdragonradio_phy` is.
- Existing `ota-phy.py` is capture-first and finite, not a full-stack network
  runner. Offline decode capacity is not evidence of real-time scheduling.

## 2. Deliverables and phase order

Do these phases sequentially through their gates. CPU-only work can proceed
without RF parameters. Complete the concrete configuration and reviewable
commands before requesting approval for hardware actions requiring it.

| Phase | Deliverable | Required exit evidence |
|---|---|---|
| P0: preserve and reproduce | Source/dependency manifests; Linux baseline; current characterization | Existing tests pass; theory calibration valid; all three backends identified by hashes |
| P1: backend contract and native OFDM | Production C++ native OFDM adapter and backend registry; legacy remains selectable | Packet/header/control/timestamp compatibility; exact payload delivery through original queues |
| P2: full-stack simulated I/O | Actual production packet/MAC/ARQ path over simulated IQ transport | UDP/TCP, retransmission, ACK-loss and queue tests through identical production code |
| P3: matched FEC and weak-link behavior | Neutral MCS/FEC contract; pinned native coding implementation | Bit-exact codec vectors and interop within each backend; matched coded/uncoded curves |
| P4: software hardening | Bounded streaming queues, C++ acquisition hot path, observability | Sustained load; CPU/tails/RSS; no drops; performance margin against legacy |
| P5: two-user full-stack OFDMA | One composite burst with per-user queue/ACK ownership and feedback slots | Independent ARQ, strong/weak fairness, batching and empty-user behavior in simulation |
| P6: cabled hardware | N210/X310 cross-family driver/rate/power calibration | No overruns/underruns; known attenuated-link results; simulation/OTA provenance matches |
| P7: OTA matrices | Randomized repeated strong/weak pair and two-user tests | Complete captures/full-stack logs and paired confidence intervals; no excluded failures |
| P8: release/decision | Analysis, reproducible artifacts, default-selection decision | Acceptance criteria met or explicit blockers; legacy OFDM retained |

Do not delay a **short capture-first cabled pilot** until all full-stack work is
finished: after P0/P4's PHY-only gates, it is useful to discover hardware CFO,
SFO, clipping, and rate issues. Label that pilot PHY-only. Full-stack OTA claims
require P1–P5. No automatic production-default change is part of this plan.

## 3. Reproduce the current CPU-only baseline

On macOS, use `.venv-simulation/bin/python`; the optional plotting packages live
in `requirements-benchmark.txt`. On Linux set `PYTHON` to an interpreter with
NumPy and the image's compatible UHD bindings when eventually needed. Do not
mix a local macOS dylib with Linux. The portable path requires no TAP/UHD::

```sh
python3 -m venv .venv-simulation
.venv-simulation/bin/python -m pip install -r requirements-simulation.txt
# macOS uses /opt/homebrew; provisioned Linux normally uses /usr:
.venv-simulation/bin/python scripts/build-phy.py --fftw-prefix /usr
.venv-simulation/bin/python scripts/build-modem.py --fetch --fftw-prefix /usr
PYTHONPATH=python/dragonradio .venv-simulation/bin/python -m unittest discover -s tests/simulation -v
.venv-simulation/bin/python scripts/benchmark-phy.py --frames 1000 --iterations 1000 --output simulation-results/baseline.json
.venv-simulation/bin/python scripts/characterize-phy.py --frames 100 --output simulation-results/characterization.json
.venv-simulation/bin/python scripts/ota-phy.py prepare --campaign simulation-results/pilot01 --frames 100
.venv-simulation/bin/python scripts/ota-phy.py simulate --campaign simulation-results/pilot01
```

Use a fresh campaign directory per trial. The characterization quick study is
exploratory; release curves need multiple independent seeds, enough error events,
and preregistered stopping rules. Save stdout/stderr and full configuration.
Run software timings separately from quality simulations to avoid CPU contention.
Reproduce the stream-copy optimization with
`python scripts/compare-software.py --frames 100 --repetitions 7 --blocks 3 --output simulation-results/software-copy-comparison.json`;
the frozen `tests/reference_stream_v1.py` avoids editing source during measurements.
Pin numerical-library threads; record governor, CPU affinity, compiler, FFTW,
Python/NumPy, physical CPU, container/image, build flags, and background load.

The full upstream `build.sh` is **not** a safe portable test command: it performs
system package installs, installs UHD/liquid, and uses old Python pins and
`-march=native`. Do not run it on the login server or controller. Instead build
in an owner-approved isolated Linux development container. First reproduce the
legacy executable on that image, pin its complete package/submodule manifest,
then modernize build configuration one dependency at a time. The top-level
Makefile hardcodes Python 3.8 and several library paths; resolve these explicitly.
Use portable flags for cross-node deployment; retain an optional local-tuned
build only as a separately labelled experiment.

## 4. Full-stack implementation details (proposed, not already implemented)

### P0 baseline triage

**Done 2026-11-02; see section 10.** Root cause: undefined negative float to
`uint32_t` conversion in the pinned `nco.c`, saturating on arm64. Original task:
Reproduce the negative-CFO issue in the isolated legacy bridge and the complete
legacy receiver using the same IQ, then isolate mask/training, estimator and
wrapper effects. Preserve the pinned result, but do not turn a configuration
fault into a claim that all liquidDSP receivers fail. If a correction is justified,
report both pinned and corrected baselines as separate versions and rerun their
matched curves; never silently replace the comparator midway through a study.

### P1: native OFDM in the production stack

1. Add `src/phy/NativeOFDM.hh/.cc` implementing `PHY` and its two nested contracts
   in `src/phy/PHY.hh`, using the FFTW engine without a Python round trip.
   Refactor `FastPHY.cc` into a reusable C++ core/header plus thin C ABI; keep
   the existing ABI/reference waveforms and tests stable during extraction.
2. Preserve `NetPacket`, `Header`, extended headers, flags/team, destination,
   broadcast/control frames, sequence wraparound, MCS index and payload length.
   The prototype currently excludes broadcast destination 255; extend and test
   that explicitly. Decide and version a wire-format header rather than silently truncating an
   MTU to the prototype's 256-byte fixed frame. Implement fragmentation/reassembly
   or larger supported frames with bounded allocation and explicit limits.
3. Fill `ModPacket` sample count/offset, shared IQ ownership, soft gain,
   modulation timestamps and AutoGain accounting as the liquid implementation
   does. Implement receiver reset/isFrameOpen/timestamp/callback semantics,
   including negative offsets, resampler delay, snapshot offsets and RX rate.
   Use device sample time rather than wall-clock arrival for packet timestamps.
4. Bind the backend in `src/python/PHY.cc` (or a new binding unit), add explicit
   `native-ofdm` configuration and selection in `radio/config.py` and
   `radio.py::mkPHY`, and add sources to the build. These flags do not exist yet.
5. `src/phy/Modem.hh` currently includes liquid headers despite an abstract MCS.
   Separate backend-neutral metadata from liquid enums. Capability-check MCS
   combinations and return an explicit unsupported-MCS error; no silent fallback.
6. Test packet filtering, payload integrity, ACK/SYN/control frames, sequence
   wrap, max MTU, resampling offsets, single/multiple demod threads and shutdown.
   Compare radio packet counters with injected traffic, not only PHY CRC counts.

### P2: use the real MAC and controller in simulation

`Radio` currently creates a USRP before PHY/logger setup. Introduce an I/O
transport/clock interface at the C++ `USRP`/IQ boundary, rather than another
Python packet-level simulator that bypasses MAC behavior. Preserve UHD as the
hardware implementation. The simulated implementation needs bounded IQ queues,
virtual device/sample clocks, TX/RX metadata, burst timing and discontinuities.
Inspect `src/USRP.hh/.cc` and all synthesizer/channelizer consumers before defining
that interface; avoid a large unrelated rewrite.

- Run the same `src/mac/TDMA.*`, `SlottedMAC.*`, `src/llc/SmartController.*`,
  `phy/*Synthesizer*`, channelizer, queues, and packet filters with simulated IQ.
- Support deterministic virtual-time correctness runs and separately labelled
  wall-clock paced throughput/load runs. Never derive CPU capacity from an
  accelerated virtual-time scheduler's simulated seconds.
- Model forward and reverse links independently, including ACK loss, jitter,
  asymmetric rates, timed grants, queue limits and packet expiry. No direct
  expected-packet injection into the receiver/controller.
- Start with synthetic native packets without TAP; then two isolated network
  namespaces/TAP interfaces inside the development container. Test UDP delivery,
  TCP bulk, small request/response traffic, and simultaneous bidirectional flows.
  Host network changes require separate authorization; container-local test
  namespaces are the preferred reversible implementation.
- Log application sequence/hash, enqueue/dequeue, modulation, scheduled/actual
  TX, RX sample timestamp, decoder result, ACK and retransmission per attempt.
  Account for all losses: application, queue, MAC deadline, acquisition, header,
  FEC/CRC, UHD discontinuity. Deduplicate delivered application data.

### P3: coding and theoretical references

Keep an uncoded QPSK apples-to-apples tier, a matched-code tier, and a production
configuration tier separate. Inspect pinned `dependencies/libcorrect` support
for exact legacy `rs8` and `v29p78` framing, termination, puncturing, interleaving
and soft metrics. Reusing libcorrect directly can avoid liquid in the native PHY,
but confirm capabilities and licensing; do not assume code names imply wire
compatibility. Add golden vectors, error injection, LLR sign/scaling tests,
encoded-length/airtime checks and no-error cross-tests. If exact parity is not
possible, publish that as an explicit comparison limitation and do not claim
production FEC equivalence.

The native prototype's bounded CRC-aided Chase correction can improve delivery
even though it has no conventional FEC. Report it explicitly and implement an
on/off ablation with CPU cost at each SNR before attributing all gains to the
FFT/equalizer. The analytic independent hard-bit PER curve excludes that search.

Export optional diagnostic pre-CRC decisions/LLRs and header/payload outcomes
from **both** receivers before publishing practical raw BER. It must be disabled
in performance runs, and must never let expected bits influence production
reception. Counting bit errors only in accepted CRC packets biases BER downward.
Keep the existing ideal known-timing/channel BER experiment separately labelled.

### P4: software performance as a first-class result

The latest study found native OFDM streaming replay slower than legacy despite
faster warm burst decoding; two-user OFDMA results vary by platform/chunk size. Treat this as an open performance item. The copy-removal
optimization is one measured improvement, not proof of blanket software parity.
Prioritize persistent C++ streaming acquisition, sliding/overlap FFT correlation,
reusable workspaces, amortized FFI calls, early address/header rejection, bounded
ring buffers, and preallocation. Preserve first-packet acquisition after noisy
idle time and recovery after corrupted frames; do not consume an unverified
candidate as a whole frame. Profile before changing algorithms.

Measure CPU and wall time for acquisition, FFT/equalization, FEC, CRC, copies,
queueing, MAC scheduling and I/O; cold construction/planning separately. Measure
per-attempt and per-verified-packet cost at strong and weak links, where bounded
CRC-aided correction has different workload. Include noise-only and bursty idle
captures, small/large UHD chunks, sustained traffic, one and two receivers,
1/2/5 MS/s where supported, and worker/thread scaling. Check allocations,
process RSS high-water/steady state, leaks during >=30-minute runs, context
switches, queue depths, overflow, and p50/p95/p99/p99.9 service/latency.

- Use `perf stat`/`perf record` where permitted inside the reserved container;
  if counters are unavailable, say so rather than changing host perf policy.
- Use optimized unsanitized builds for performance; ASan/UBSan/TSan correctness
  builds separately. Check compiler/FFTW SIMD and avoid unintentional BLAS thread
  pools. Baseline and candidate runs alternate in randomized blocks on the same
  idle host; isolated subprocess RSS is not per-function allocation attribution.
- Compare all three layers: warm frame kernels; streaming decoder + idle search;
  complete app-to-app radio. Never combine their timing denominators.
- Real-time gate: >=2x measured headroom at declared target load, p99 service
  below its processing budget, bounded p99 queues, no discontinuities or host
  drops over >=30 minutes. This is an engineering target, not an existing result.
  If native costs more CPU than legacy, report the regression and optimize it;
  don't substitute good BER or faster TX to hide slower RX streaming.

### P5: full-stack downlink OFDMA

The existing `ModPacket` owns one `NetPacket`. Add an explicit batch/burst object
with one shared IQ buffer, one scheduled TX event, and per-user packet references,
MCS, sequence and ACK state. Do not enqueue duplicate copies of a composite
waveform or count its airtime twice. Extend the synthesizer/queue accounting at
`ModPacketQueue`, `TDChannelModulator`, `MultichannelSynthesizer`, and the chosen
slot scheduler with tests that verify this invariant.

Begin with fixed allocations and scheduled **time-separated uplink feedback**;
UEs never transmit simultaneously until a separate uplink design is implemented.
A common robust control header announces allocation/length/MCS and version.
Support one user idle, missing/expired packets, one user disconnected, bounded
batch wait, per-user retry budgets, destination-specific ACKs and eventual
adaptive allocation. Per-user retransmission must not replay already delivered
partner data as new data. Weak user failures must not stall the strong user's
queue. Treat grants, ACK slots and batching delay as real airtime/latency costs.
Compare equal allocations first, then unequal traffic/deadlines and optionally
adaptive scheduling. Keep legacy and native OFDM modes working throughout.

## 5. Simulation experiment design

The machine-readable design is `grid/benchmark-matrix.json`. Keep actual node
and RF approval records out of the source template. Every result needs a unique
run ID and immutable source/library/configuration hashes.

### Channel ladder

1. Noiseless loopback, then complex AWGN calibrated against QPSK theory.
2. Fixed receiver noise with gain/attenuation sweeps; block Rayleigh and Rician
   fading (Rician/time-correlated fading are future additions), independent UEs.
3. Measured within-CP multipath, deep spectral notches, and explicitly out-of-CP
   stress cases. Estimate delay spread before choosing CP; an out-of-CP failure
   is outside a CP16 claim, not a bug to suppress in reports.
4. CFO both signs, including independent N210/X310 oscillator offsets; SFO both
   signs and long streams, timing offset, drift, phase noise, gain changes,
   quantization/IQ imbalance/DC/clipping. Only AWGN/CFO/short FIR/block Rayleigh/
   +50 ppm burst SFO are in the new executed quick study. Other items need code.
5. Replay measured hardware IQ/noise/channel estimates through both decoders;
   freeze training/tuning captures and hold out evaluation captures to avoid
   optimizing the receiver to the test set.

`ChannelConfig(snr_db=...)` historically means SNR relative to post-FIR power.
For weak-link/fading experiments use `snr_db=None, noise_power=<fixed complex
variance>` so fading does not reduce noise automatically. Define SNR reference
plane (sample-domain or per-tone) and do not include arbitrary idle padding in
signal-power estimation. The quick study sets fixed noise from TX active frame
power. Multipath normalization and receiver gains are recorded by profile.

### Counts, controls and metrics

- Start with the executed 64/256-byte payload and 0/4/8/12/16/20/30 dB sweep;
  refine the waterfall in 1 dB steps and extend payload/MTU after P1/P3.
- Use >=10 independent seed/channel blocks. Target >=100 error events near the
  waterfall or cap at >=1e6 packets/point for rare-error work. Predetermine caps
  and intervals; don't stop at a lucky zero-error run. To claim PER <1e-3 with
  zero observed independent errors requires roughly 3000 trials at 95% one-sided
  confidence; fading-correlated packets need independent blocks, not that rule.
- Report PER/BLER, true pre-CRC BER when available, acquisition/header/CRC/FEC
  failure rates, false accepts, EVM, CFO/SFO estimation error, per-user PDR,
  retries, net verified app goodput, spectral efficiency, queueing/service and
  app latency distributions, Jain fairness and worst-user performance.
- Separate saturated equal-payload/equal-total-power comparisons from equal
  offered-load (10/50/80/95/110% of baseline capacity) latency/queue tests.
  Native OFDM and legacy framing differ; account for every header/training/
  pilot/CP/gap. OFDMA divides total power across users and must include grants
  and feedback. Also present optional equal per-user energy results as a
  separately labelled operating point, not a free OFDMA power advantage.
- Bootstrap paired trial blocks for backend differences; predeclare a PDR
  noninferiority margin (initial target 1 percentage point) and application
  goodput/latency target. Report uncertainty even for a negative result.

### Theory to plot, and its limits

For coherent Gray QPSK in complex AWGN:
`Pb = Q(sqrt(2 Eb/N0)) = 0.5 erfc(sqrt(Eb/N0))`. The independent hard-bit packet
reference is `PER_iid = 1 - (1-Pb)^Nbits`; use numerically stable log/expm1.
It excludes acquisition, channel estimation, correlated errors and CRC-aided
Chase correction, so is not an exact practical PER curve or a universal bound.

For the unitary FFT in the native modem, complex sample noise variance `N0`
is preserved in each FFT bin. `Es/N0 = E|X_data|^2/N0`, and QPSK bit
`Eb/N0 = Es/(2 N0)`. Convert sample SNR using actual active-tone energy, occupied
bins and power normalization; account separately for payload-energy overhead
from pilots, CP, training, headers and padding. Never relabel sample SNR dB as
Eb/N0 dB. The oracle experiment measures actual native FFT symbol energy.

For an ideal coherent flat-Rayleigh channel the averaged QPSK bit reference is
`Pb = 0.5*(1 - sqrt(gamma_bar/(1+gamma_bar)))`, with gamma_bar **bit** Eb/N0;
within-packet block fading makes `1-(1-E[Pb])^N` an incorrect packet model.
Integrate conditional packet error over channel power or simulate independent
blocks. For frequency-selective channels integrate per-tone effective SINR
only under explicitly stated CP/synchronization/equalizer assumptions.

Plot uncoded packet-airtime goodput `8*Lapp*Psuccess/Tframe`, and then the full
stack renewal estimate `E[delivered app bits]/E[cycle time]` including retries,
ACK/control, batching and gaps. Independent loss `p` with at most `T` attempts
has success probability `1-p^T` and expected attempts `(1-p^T)/(1-p)`;
ACK loss, correlated fades and queues invalidate that simple approximation.
Shannon `B log2(1+SNR)` is an ideal channel-capacity reference with consistently
defined bandwidth/power, not a prediction that this fixed QPSK protocol achieves
capacity. FEC comparisons require matching code/rate/decoder assumptions.

Primary references:
- QPSK analytical BER: https://www.mathworks.com/help/comm/ug/analytical-expressions-used-in-berawgn-function-and-bit-error-rate-analysis-app.html
- QPSK/OFDM normalization example: https://www.mathworks.com/help/comm/gs/qpsk-and-ofdm-with-matlab-system-objects-1.html
- Ettus bandwidth/transport limits: https://kb.ettus.com/About_USRP_Bandwidths_and_Sampling_Rates
- UHD device manuals: https://files.ettus.com/manual/page_usrp2.html and https://files.ettus.com/manual/page_usrp_x3x0.html

## 6. Hardware matrix, strong/weak definitions and calibration

Run four directed pair types: N210->N210, N210->X310, X310->N210, X310->X310.
For two-user downlink, run both transmitter families with ordered receiver
pairs (N210,N210), (N210,X310), (X310,N210), (X310,X310): eight role combinations.
For each use strong/strong, strong/weak, weak/strong, weak/weak links. Swap
subbands and receiver physical positions in controlled separate blocks; role
order matters because the lower/upper bands and locations can differ.

Define strong/weak from a calibrated reference curve rather than radio gain:
strong = reference PER <=1e-3 with adequate sample size and no clipping;
weak = reference PER around 10–30% near the waterfall. If a known CFO/driver
fault causes failure even at high SNR, classify it as a fault, not a weak link.
Keep the chosen attenuation/geometry and total radiated TX power fixed while
comparing modes; do not retune each backend until it wins. Keep measured SNR,
noise floor, attenuation and baseline curves as continuous data as well.

Start Fs1 MS/s and common occupied spectrum for all combinations. Increase to
2/5 MS/s only after each device's actual rate/readback and driver compatibility
passes; don't infer supported rates from the API request. N210 GigE sc16 is
nominally limited to 25 MS/s and X310 transport/daughterboard bandwidth varies;
those product ceilings are not promises about this grid's CPU, NIC or images.
Do not compare a 1-MS/s N210 trial with a 10-MS/s X310 trial as a PHY improvement.

Cabled setup first: TX -> rated attenuator -> splitter -> independent calibrated
RX attenuators -> UEs; log splitter/cable losses, power measurements and input
limits. No direct high-power TX-to-RX connection. OTA follows in approved space
with recorded antennas/polarization/heights/positions, LOS and stable multipath.
Use independent internal clocks initially; optional common reference/PPS is a
separate condition. N210 time source is `none`/external/mimo, not `internal`;
empty `time_source` in the runner preserves defaults. Do not assume independent
USRPs have a shared epoch or claim one-way latency without clock calibration.
Use RTT first; later synchronize/measure clock error for one-way latency.

## 7. Grid build, deployment and actual test execution

Read the installed `gridcli ... --help` and current inventory; never assume a
node list or image from old logs is still valid. `dwslgrid` is the head node.
Use an owner-approved image with compatible Python UHD and current FPGA.
The historical DragonRadio image used UHD3.9, while the current Python runner
was checked against UHD4.x APIs. Reconcile versions explicitly; do not overwrite
/usr/local UHD or flash radios to make the mismatch disappear.

1. Prepare an exact owner/node/image/fingerprint/reservation/frequency/bandwidth/
   gain/power/antenna/attenuation/duration/run-count plan. Obtain explicit RF
   authorization for this experiment and authorization for any new resource
   allocation or firmware/host changes. Prior OpenISAC approval is unrelated.
2. Once authorized, schedule one finite reservation containing only chosen free
   radios, with `--no-flash --no-dragonradio-service --no-traffic`. Syntax pattern
   (fill real approved values; do not execute placeholders):
   `gridcli reservation schedule OWNER/IMAGE TX RX1 RX2 --name NAME --duration DURATION --no-flash --no-dragonradio-service --no-traffic --async`.
   Store the returned **ID**; names can be ambiguous. Verify RUNNING and ownership.
   Setup a trap to stop only that reservation on every exit; don't stop the
   persistent controller or another experiment. Investigate host-key changes.
3. Build from a hash-verified source bundle in a unique container-local directory
   on the target architecture. `python scripts/package-grid.py` produces the
   portable PHY-only bundle and SHA manifest. **It does not include the complete
   stack.** P1–P5 need a separate full-source/submodule build artifact plus image
   manifest; implement and verify that packager instead of misusing this bundle.
4. Probe each reserved radio read-only, check serial/UHD/FPGA, clock/time source,
   antennas, actual rate/frequency/gain and LO locks. Check disk/RAM/NIC and run
   the identical-code simulation gate. Do not auto-remediate firmware mismatch.
5. For a current PHY pilot, prepare/simulate a fresh campaign, fill separate SDR
   configs and `grid/trial-plan.example.json`, inspect dry commands, then execute
   `python grid/run-trial.py --plan PLAN --mode MODE --execute` in the approved
   window. Run all three modes in randomized paired order. Both RX hosts must
   be ready before TX; controller and node jobs are bounded with cleanup.
6. Capture to node-local disk first, collect completed files afterward; NFS is
   appropriate for ready/control files but can bottleneck IQ. The example runner
   defaults to shared results, so set up local capture/explicit collection for
   load tests before claiming real-time throughput. 1-MS/s complex-float32 is
   8 MB/s per RX; provision the full trial matrix's storage and retention.
7. Run `ota-phy.py analyze` for each mode and `summarize-ota.py` across independent
   complete paired campaigns. Stratify hardware/strength/placement/power/clock/
   carrier map/build. Current summary refuses mixed settings; don't defeat that
   guard to average incompatible experiments. Extend it with explicit strata.
8. For full stack, implement the P1–P5 runner first; use the same manifest,
   readiness, finite duration, TX acknowledgement and continuity protections,
   with app traffic/ARQ/queue instrumentation. Obtain RF approval for UE feedback
   and reverse-data transmission as well as base-station TX before enabling TDD;
   current PHY-pilot UEs remain RX-only. Calibrate reverse ACK links separately
   rather than assuming a forward strong link implies a strong return link.
   These full-stack commands do not
   exist yet; do not substitute `frame-radio.py` and call it full-stack testing.
9. Any overflow/underflow/late packet/clock reset makes a trial invalid for an
   RF-only loss conclusion. Retain the failure and root cause. No silent data
   exclusion, unexplained retuning, changed FPGA, or unlimited retry loops.

## 8. Analysis package and final acceptance

Each result stratum includes: source/submodule/build hashes; image/driver/radio
inventory; RF approval and calibration reference; geometry; requested/actual
rates/power; waveform/config/channel seeds; application/PHY/MAC/I/O counters;
raw IQ or a documented retention subset; pcap/event logs; performance traces;
paired trial order and all failures. Keep credentials and private operational
logs outside Git. Deliver generated reports separately from source.

Plots: theory/oracle/practical BER and PER vs properly labelled SNR/EbNo;
per-user/aggregate net goodput vs SNR and offered load; coding overhead and
spectral efficiency; retransmission/latency CDFs; fairness under asymmetric
links; CPU ns/sample and per verified byte; service-time tails and queue depth;
RSS over time; measured CFO/SFO/delay distributions; matched N210/X310 comparisons.
Overlay both backend differences and uncertainty, not only winning points.

Proposed release gate (agree targets before confirmatory measurements):
legacy OFDM preserved; no correctness regressions or false delivery; native
OFDM PDR lower confidence bound within 1 percentage point of legacy in the
supported channel envelope; useful throughput no lower at matched power/load;
software cost and tail budgets meet P4, including weak/noise-only conditions;
OFDMA serves both users with independent retry state and demonstrates an
aggregate improvement with worst-user reliability meeting its target. Explicitly
report cases outside the envelope (CP too short, unsupported FEC/rate, hardware
fault). If a gate fails, keep the legacy default, name the failing metric, fix
and rerun the affected stratum plus regression suite.

At each phase update a short status section with: implemented vs proposed,
commands actually run, report paths/hashes, passed/failed gates and next blocker.
Do not claim OTA, full-stack support, production FEC parity or software dominance
on the basis of the existing successful uncoded frame demonstrations.

## 9. Completed exploratory work in this handoff

- Added and executed theory-calibrated characterization: 294 practical cells,
  12 oracle BER points and 12 strong/weak two-user/TDMA controls, with 300 packets
  per user/cell. Ideal BER agreed with the stated QPSK test and no incorrect
  payload was accepted. This is not rare-error certification.
- Added fixed receiver-noise variance so path attenuation/fading changes SNR.
- Removed repeated remainder copies from native streaming reception. The final
  three-block, seven-repetition, 100-packet/user comparison reproduced identical
  inputs and payloads. Whole-capture paired median speedups were about 4.5x for
  native OFDM and 4.3x for OFDMA. Small-chunk changes are modest relative to
  timing/control drift. Native OFDM streaming still costs more than legacy;
  two-user OFDMA results vary by platform and chunk size.
- The regression suite passed all 47 tests on macOS and Linux. Generated evidence is in
  `simulation-results/characterization.json`, `software-copy-comparison.json`,
  and their per-block/worker files. Completed Linux reports are
  `simulation-results/characterization-linux.json` and
  `simulation-results/characterization-software-linux.json`. `docs/characterization.rst` is the study
  explanation; `docs/images/characterization.png` is the figure.
- No full-stack native adapter, matched production FEC, full-stack simulated
  transport, new reservation, radio firmware update or OTA transmission was
  performed in this exploratory round. P1–P8 remain development/execution work.

## 10. Phase status, 2026-11-02 (second session)

Everything below ran on macOS 26.5 arm64 (Apple clang 21, FFTW 3.3.11, Python
3.14.4, NumPy 2.5.4) with no SDR opened, no RF transmitted, no reservation, and no
firmware or host change. Generated reports live in `simulation-results/` (git-ignored).

### Implemented and verified

| Item | Evidence | Gate |
|---|---|---|
| P0 recoverable checkpoint | `refs/checkpoints/p0-2026-11-02` = `37d91adc`: commit of the whole working tree incl. untracked files on top of `da3808d2`. Branch, index and working tree untouched. Restore one file with `git checkout refs/checkpoints/p0-2026-11-02 -- PATH`. | n/a |
| P0 regression suite | 47 existing tests passed before changes; 48 (47 + new core-header test) after. | pass |
| P0 theory calibration | `scripts/characterize-phy.py --quality-only --frames 100 --profiles awgn cfo-positive cfo-negative --snrs 0 8 20 30` -> `p0-recheck-macos.json` (sha256 `2ca89322…`). 12/12 oracle BER points within the 6-sigma QPSK check; 72 practical cells and 12 asymmetric controls, zero false accepts. Reduced profile set; the full 294-cell study is the earlier `characterization.json`. | pass |
| P0 negative-CFO triage | `scripts/triage-liquid-cfo.py` -> `liquid-cfo-triage-macos.json` (`e261e1aa…`). Pinned build, all three masks: full delivery only from -0.05 to +0.55 subcarrier. NCO-fixed copy: -0.55 to +0.55. UBSan flags `nco.c:345`. Linux x86_64 report already showed 300/300 at -0.18. | root cause isolated |
| P0 provenance manifest | `scripts/baseline-manifest.py` -> `baseline-manifest-macos.json`: git HEAD/checkpoint, SHA-256 of every dirty/untracked source, the three backends, toolchain and host. | pass |
| P1 step 1: core extraction | `FastPHYCore.hh` (namespace `dragonradio::native`) + thin ABI. Same six exported `dr_fast_*` symbols. Golden check (`simulation-results/p1-core-extraction/golden.py`): waveform and decoder-metric hashes for OFDM, OFDMA and 64-byte OFDM identical before/after, 60 packets x 3 channels each. FFTW planner lock is now one `inline` object across translation units; channel-model and TX-size validation moved into `add_channel_model`/`write_checked`. `tests/simulation/test_core_header.py` builds two C++ units from the header with `-Wall -Werror` and checks a bit-identical frame vs ctypes. | pass |

### New findings that change later phases

- **Legacy comparator is architecture-dependent.** Existing pinned studies remain
  valid as "pinned, arm64" or "pinned, x86_64". Any corrected-NCO comparator must be a
  separately labelled version with rerun curves; it was not substituted anywhere.
  Record compiler and architecture with every legacy result.
- **Native CFO acquisition is narrow.** Native OFDM/OFDMA acquire only within about
  +/-0.25 subcarrier (+/-2 kHz at 1 MS/s, FFT128). The preamble correlation metric
  drops to 0.176 at 0.30, below the 0.20 threshold. Corrected liquid reaches +/-0.55.
  Each 1 ppm of relative oscillator error is 1 kHz at a 1 GHz carrier, so independent
  N210/X310 clocks can exceed native's range. Measure actual offsets in the first
  cabled PHY pilot. Widening acquisition is P4 work and is **not implemented**.

### Proposed / not started (unchanged scope)

- P1 steps 2-6: `src/phy/NativeOFDM.*` implementing `PHY`, header/MTU versioning,
  `ModPacket`/timestamp semantics, Python binding and `--phy native-ofdm`, Modem.hh
  liquid separation. **Blocker:** production headers need UHD, liquid, xsimd and
  pybind11, none installed on this Mac (`src/Clock.hh`, `IQBuffer.hh`, `Packet.hh`).
  Build and test in the owner-approved isolated Linux container (section 3); writing
  untestable production code here would not meet the P1 gate.
- P2-P5 (simulated I/O through the real MAC, matched FEC, software hardening, full-
  stack OFDMA): not started; depend on P1.
- P6-P8 (cabled hardware, OTA matrices, release decision): not started; need grid
  reservation, RF approval and owner sign-off. Nothing here authorizes them.
- Linux reruns of the two new scripts on the grid controller workspace: not run.

### Next blocker

Provision the isolated Linux build container with the legacy dependency set, reproduce
the legacy `dragonradio` build there, then start P1 step 2 against `FastPHYCore.hh`.

## 11. Native PHY v2 revision status, 2026-11-03

Implemented and measured (simulation only, gridgpu01): CFO-robust acquisition
(+/-1.30 subcarrier), persistent C++ streaming receiver, early header rejection,
static 16-QAM with a matched liquid 16-QAM mode, and a Chase on/off switch. The
frozen v1 engine is kept in `tests/reference_native_v1`. Nine of eleven declared
gates met; the two misses (saturated-OFDMA CPU versus v1/3, single-user 16-QAM
gain at 256 bytes) are explained in `docs/native-revision.rst`, which also holds
the plan, method and results. This narrows the P4 software item and the native
CFO risk noted in section 10; it does not change the P1-P8 order, and no
production `--phy` default changed.


## 12. P1 status, 2026-11-03: native OFDM in the production stack

Implemented and tested without a radio (details: `docs/native-fullstack.rst`):

- `--phy native-ofdm` builds `NativeOFDM` (`src/phy/NativeOFDM.cc`) on the framed
  native engine (`src/simulation/NativeFrame.hh`, wire format v2: QPSK header
  section with the DragonRadio Header, MCS index, constellation, length and
  CRC-32; variable QPSK/16-QAM payload with CRC-32). Liquid OFDM stays default.
- Production team/next-hop/own-transmission filtering runs on the decoded header
  before any payload work; unwanted frames are skipped by length.
- Only CRC-32 uncoded QPSK/16-QAM MCS entries are accepted; RS/Viterbi/64-QAM/
  CRC-16 are rejected with an explicit error. The production default coded
  tables therefore cannot be used with the native PHY until P3.
- `MonoClock` uses the host clock when no USRP is attached (was a null
  dereference). Two missing standard includes fixed for GCC 13.
- Evidence: `tests/fullstack/test_native_ofdm.cc` and `python_native_phy.py`
  pass on gridgpu01 from a from-scratch workspace built from commit 635b31ae
  (`grid/setup-fullstack-build.sh` + `grid/build-fullstack.sh`), and the C++
  test also passed under AddressSanitizer. The workspace is a modernized
  toolchain (UHD 4.6, Python 3.12, pybind11 2.13.6), not the historical image.

P1 items still open: backend-neutral MCS metadata (`phy/Modem.hh` still uses
liquid types), per-symbol logging for native frames, fragmentation above the
configured maximum payload, and an end-to-end `Radio` run, which needs the P2
simulated I/O boundary because `Radio` opens a USRP before building the PHY.
Next: P2 (simulated IQ transport/clock at the USRP boundary so the real MAC,
ARQ, synthesizer and channelizer run against simulated channels).
