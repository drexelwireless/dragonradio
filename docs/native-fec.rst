Coding for the native PHY (P3)
==============================

Status 2026-11-04. The native OFDM PHY carries the radio's coded MCS entries:
the production payload code ``rs8`` and liquid's convolutional codes,
plain and punctured. A native coded payload is the same bit stream liquid's
packetizer produces for that MCS. The native header section can be coded
too. Everything here ran in simulation on gridgpu01; nothing was
transmitted.

Three tiers
-----------

The plan separates three comparisons, and this work keeps them apart:

* **Uncoded tier.** CRC-32 QPSK or 16-QAM with no FEC, as before. The native
  wire format is unchanged apart from the header (v3), and the CRC-aided
  Chase search still applies only here.
* **Matched-code tier.** Native and liquid OFDM run the same code with the
  same framing (``rs8``, ``v27``, ``v29p78`` in the benchmark). The coded bits
  are identical; the two PHYs differ in modulation details, acquisition,
  channel estimation and soft-bit computation.
* **Production configuration tier.** Liquid OFDM exactly as the radio ships:
  ``rs8`` payload code and a ``v29p78`` BPSK header with soft decoding. Until
  P3 this tier could not run in the build workspace (next section).

Production FEC was missing from the workspace
---------------------------------------------

The historical ``build.sh`` installs libcorrect and its libfec-compatible
shim before liquid-dsp. Liquid's convolutional and Reed-Solomon codes exist
only when liquid finds libfec. The P1 workspace setup skipped that step, so
its liquid build had no ``rs8`` or ``v29p78``. The default liquid header then
failed at start-up, which is why the P2 liquid control needed
``--set header_fec1=none``. ``grid/setup-fullstack-build.sh`` now builds the
pinned libcorrect (``2c8db6d8``, BSD-3) with its shim and checks that liquid
configures with ``HAVE_FEC_H``.

Native coding
-------------

``src/simulation/NativeFEC.hh`` ports liquid-dsp's packetizer chain (MIT
license, attribution in the file):

* CRC-32 appended big-endian, then liquid's whitening.
* Two FEC and interleaver stages: inner (``fec0``) then outer (``fec1``).
  A stage without FEC is not interleaved.
* Reed-Solomon (255,223) with liquid's blocking into shortened codewords.
  This includes a quirk: the residual tail of the last block carries stale
  bytes of the previous block.
* Convolutional ``v27`` and ``v29`` with liquid's polynomials,
  zero-flushed tails, byte padding, and the twelve puncturing matrices.
* Hard decoding, and soft decoding of the outer stage as
  ``packetizer_decode_soft`` does.

The Reed-Solomon and Viterbi cores are libcorrect through its libfec API,
the same functions liquid calls in the production build. Native coding is
compiled only with ``DRAGONRADIO_NATIVE_FEC`` (the production Makefile and
the workspace set it and link ``-lfec``). Without it, coded entries are
rejected and the uncoded PHY is unchanged. The Python simulation library is
built that way.

Soft bits come from approximate LLRs:
``|LLR| = 4 * (d_min / 2) * confidence / N0``, where confidence is the
distance to the decision boundary times ``|H|^2`` and ``N0`` is the mean
pre-equalization error power of the frame. They are mapped to liquid's
0–255 soft-bit convention.

Wire format v3 adds the inner and outer code to the header (16 bytes,
format byte ``0xD3``). The receiver decodes the payload from the header's
codes, not from its own MCS table.

**Coded header.** ``--native-header-fec`` (``none``, ``v27``, ``v29``; default
``v29``) codes the header section with a rate 1/2 convolutional code, decoded
soft. It is a link parameter that both ends must share, like M and CP. At
M=48 it adds two OFDM symbols, about 1.5% of a 1000-byte frame. Liquid's
header is BPSK with ``v29p78``. The native header stays QPSK with a stronger
rate 1/2 code, so the two headers are comparable in robustness but not
identical.

Evidence
--------

* ``tests/fullstack/test_native_fec.cc`` compares the native coder with
  liquid's packetizer in one process. It covers 34 code pairs (every code as
  inner and as outer, plus combinations) and 16 message lengths:

  * Encodings: 1632 identical.
  * Clean decoding: each side decodes the other's packets.
  * Corrupted packets: 4896 cases at three bit error rates, where the two
    decoders return the same message and CRC result.
  * Soft decoding: 544 noisy soft-bit cases, where they also agree.
  * Capability: ``rs8`` corrects 16 symbol errors and not 17, and the soft
    sign convention is checked.

* ``tests/fullstack/test_native_ofdm.cc`` runs coded tables through the
  production PHY classes. They include ``rs8``, ``v29p78``, ``v27+rs8``,
  ``rs8+v29``, soft and hard decoding, and ``v29``/``v27`` headers. The test
  covers exact frame sizes, every wanted frame, CFO with noise, and the
  channelizer buffer protocol.
* ``tests/fullstack/python_native_phy.py`` checks the binding and that the
  production ``rs8`` table is accepted. The Python native-frame tests pass
  against the independent v3 NumPy reference transmitter.

Packet error rate against theory
--------------------------------

``bench/fec_per.cc`` (``grid/run-fec-benchmark.sh``) drives the production
PHY classes over AWGN with real acquisition. It used 1000 packets of 1000
data bytes per point, M=48, CP=6 and liquid's default mask.
``scripts/report-fec.py`` computes every figure and the theory curves from
the CSV. The theory is an ideal reference: AWGN, perfect synchronization
and channel knowledge, independent bit errors and the payload section only.
For ``v27`` it is the union bound with the standard (133,171) distance
spectrum. SNR is per complex sample over the frame. No packet passed CRC
with wrong bytes.

.. list-table:: SNR for 1% packet error rate (dB per sample)
   :header-rows: 1

   * - Code
     - Native, v29 header
     - Native, uncoded header
     - liquid (v29p78 header)
     - Theory
   * - QPSK uncoded
     - 12.7
     - 12.4 (Chase 4: 11.2)
     - 13.8
     - 12.4
   * - QPSK rs8
     - 7.5
     - 11.2
     - 9.2
     - 7.4
   * - QPSK v27 soft
     - 4.2
     - 11.0
     - 5.5
     - 3.3
   * - QPSK v27 hard
     - 5.6
     - 11.0
     - 7.0
     - 5.7
   * - QPSK v29p78 soft
     - 8.4
     - 10.9
     - 8.8
     - —
   * - 16-QAM rs8
     - 14.3
     - 14.2
     - 15.6
     - 14.1

With about 10 errors at each 1% crossing, differences under about 0.3 dB are
within noise.

**Reproduction.** A from-scratch workspace built from commit a9e7d30b with
``grid/setup-fullstack-build.sh`` installed libcorrect and FEC-enabled liquid
and passed every ``tests/fullstack`` program. A 300-packet rerun of the
benchmark agreed with these crossings within its coarser resolution, about
0.5 dB. Examples: native ``rs8`` 7.6 dB, liquid ``rs8`` 9.3 dB, native soft
``v27`` 4.2 dB. The clean-arq, ack-loss, saturation and tap-tcp-bulk
scenarios, and liquid production clean-arq and erasure-arq, passed.

* **The uncoded native header limits coding gain.** With an uncoded header,
  every QPSK code stalls near 11 dB: at 6–8 dB most losses are
  header/acquisition failures, and payloads whose header arrived decode.
  The coded header removes the limit.
* **Native with a coded header tracks theory.** It is within 0.1 dB for
  ``rs8`` and hard ``v27``, 0.9 dB for soft ``v27``, where the approximate
  LLRs and the optimistic union bound both contribute, and 0.3 dB uncoded.
* **Liquid needs about 1.3–1.7 dB more for the same code.** The coded bits
  are identical, so the gap is in liquid's synchronization, estimation and
  demapping.
* **Chase on uncoded QPSK.** The search gains 1.2 dB at 1%. At 6–8 dB,
  where it searches most packets, it roughly doubles demodulation time, from
  about 120–175 µs to 240–250 µs per packet. At 12 dB and above it costs
  nothing measurable.
* **Demodulation time at high SNR.** Times are per packet under parallel
  load:

  * Uncoded: native about 200 µs, liquid about 555 µs.
  * ``rs8``: native about 600 µs, liquid about 694 µs.
  * Soft ``v27``: native about 1390 µs, liquid about 1650 µs.

The report is ``scripts/report-fec.py --run RUN_DIR --output report.html``.
The run's CSV and log are kept with the simulation results, which are not
committed.

Full stack with coded MCS
-------------------------

The P2 suite reran with the native header coded (``--native-header-fec v29``,
the new default). All 23 scenarios passed. A 1000-byte frame grew from 6912
to 7020 samples, still three per TDMA slot, so saturated goodput stayed at
340 kb/s.

Two coded variants ran the clean, erasure, saturation and tap TCP bulk
scenarios. One was the native PHY with the production payload code
(``-c rs8``). The other was liquid OFDM exactly as the radio ships
(``--phy ofdm -c rs8``, ``v29p78`` BPSK header, soft decoding), which needs
no header override now that liquid has its codes. Both passed everything:

.. list-table::
   :header-rows: 1

   * - Scenario
     - Native rs8 (v29 header)
     - liquid production
   * - clean-arq
     - 801/801 each way, every frame received
     - 801/801 each way, every frame received
   * - erasure-arq (20%)
     - 801/801, 212 retransmissions
     - 801/801, 215 retransmissions
   * - saturation
     - 339.9 kb/s, 99.1% of the TDMA bound (8046-sample frames)
     - 340.8 kb/s, 99.4% (8208-sample frames)
   * - tap-tcp-bulk
     - 400 kB at 322.6 kb/s, 97.5% of bound
     - 400 kB at 323.7 kb/s, 97.8% of bound

These links are clean apart from erasure, so the coding gain shown in the
packet error rate study does not appear here. The point is that the
production coded configuration now runs end to end on both PHYs.

Limits
------

* Constellations other than QPSK and 16-QAM are not supported natively.
  The production AMC tables use BPSK through 256-QAM with ``rs8``, so only
  their QPSK and 16-QAM rows run on the native PHY.
* ``v39``, ``v615``, Hamming, Golay, SEC-DED and repetition codes are not
  ported. CRC-16 and other checks are not accepted.
* No theory curve for punctured codes; AWGN only; one run per point.
* Native and liquid headers differ (QPSK + v29 r1/2 against BPSK + v29p78).
