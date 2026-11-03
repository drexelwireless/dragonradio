Native OFDM in the production stack (P1)
========================================

Status 2026-11-03. ``--phy native-ofdm`` selects a native (non-liquidDSP) OFDM
PHY inside the production radio. Liquid OFDM remains the default. Everything
below was exercised without a radio; no RF was transmitted.

Selecting it
------------

::

    dragonradio scripts/standalone-radio.py --phy native-ofdm \
        --native-max-payload 4096 --native-chase-bits 4 ...

``-M``, ``--cp`` and ``--subcarriers`` keep their meaning and default
(M=48, CP=6, liquid's default mask). ``--taper`` is ignored (the native PHY
does not taper). The header MCS and soft-decoding options do not apply: the
native header section has its own fixed QPSK coding and CRC-32.

The MCS table (``ms``/``amc_table``) must contain only CRC-32, uncoded QPSK
or 16-QAM entries. Anything else, including the production defaults with
``rs8`` or ``v29p78`` coding, is rejected at start-up with a ``ValueError``
naming the entry. There is no silent substitution. Matched FEC is phase P3.

Wire format (native frame v2)
-----------------------------

``src/simulation/NativeFrame.hh`` defines the format:

* Two identical training symbols (Schmidl-Cox detectable, +/-1 on every
  active tone from PRBS-15).
* Header section, always QPSK, 14 bytes: format byte ``0xD2``, the 5-byte
  DragonRadio ``Header`` (host byte order, as the liquid PHYs send it), MCS
  index, payload bits per symbol, payload length and CRC-32. One symbol at
  M=128, two at M=48.
* Payload section at the announced constellation: the packet body
  (``ExtendedHeader``, data and control messages) followed by CRC-32.
* Header and payload bits are scrambled by one per-user PRBS-15 sequence.

The receiver decodes the header, asks ``PHY::wantPacket`` (team, next hop,
own transmissions, snapshot state) and either skips the frame by its
announced length or waits for the payload samples and decodes them. Native
and liquid OFDM are different air interfaces and do not interoperate.

What is implemented
-------------------

* ``NativeOFDM`` (``src/phy/NativeOFDM.cc``): modulation fills ``ModPacket``
  (offset, sample count, shared IQ buffer), applies soft gain, feeds AutoGain
  and records modulation timestamps; demodulation fills ``RadioPacket`` CSI
  (EVM, RSSI, CFO as f/Fs), channel, bandwidth, MCS index, slot timestamp and
  full-rate start/end sample offsets with the same formula as the liquid PHYs,
  and reports snapshot self-transmissions. One engine per modulator and
  demodulator; FFTW planning is serialized with the production ``fftw::mutex``.
* Python: ``_dragonradio.radio.NativeOFDM``; ``Radio.mkPHY`` builds it for
  ``--phy native-ofdm``.
* ``MonoClock`` falls back to the host monotonic clock when no USRP is
  attached (previously a null dereference). With a USRP nothing changes.

Evidence
--------

``tests/fullstack/test_native_ofdm.cc`` sends packets through the real
``NetPacket`` -> ``PacketModulator`` -> IQ -> ``PacketDemodulator`` ->
``RadioPacket`` path for native OFDM at M=48/CP=6 and M=128/CP=16, with liquid
OFDM through the same harness as a control. It checks header fields and flags,
extended headers, byte-exact bodies including NAK/selective-ACK control
messages, ``integrityIntact``, MCS index, exact frame start offsets and
timestamps, ``getModulatedSize``, sequence wraparound, 1500-byte payloads,
broadcast delivery, rejection of other next hops, other teams and the node's
own transmissions, three chunk sizes, 25 dB noise with 0.6 (M=48) and 1.0
(M=128) subcarrier CFO, and two demodulators on separate threads. It also
passed under AddressSanitizer, which found and fixed one stream bug.
``tests/fullstack/python_native_phy.py`` checks the binding and configuration
through the embedded interpreter, including rejection of RS, Viterbi,
64-QAM and CRC-16 entries.

Build workspace
---------------

``grid/setup-fullstack-build.sh`` creates a private Ubuntu 24.04 workspace
without sudo (development packages unpacked from ``apt download``), and
``grid/build-fullstack.sh`` builds the full ``dragonradio`` executable and runs
the tests. This is a **modernized** build: UHD 4.6, Boost 1.83, HDF5 1.10,
Python 3.12, pybind11 2.13.6 and current Python packages, with liquidDSP,
xsimd and firpm at their pinned revisions and portable ``-O2``. It is not the
historical Ubuntu 20.04 / UHD 3.9 / Python 3.8 image, and nothing here was run
against a USRP. Two sources needed missing standard includes for GCC 13.

P2 follow-up: full-stack simulation (``docs/fullstack-simulation.rst``) ran
``Radio`` end to end and found that the native demodulator dropped frames
starting at a receive-buffer boundary, because ``TDChannelizer`` and
``FDChannelizer`` reset it between buffers. ``isFrameOpen`` now stays true
while unscanned samples remain; the channelizer-protocol test was added to
``test_native_ofdm.cc``.

Not done (proposed)
-------------------

* Over-the-air or cabled operation; real-time scheduling under load.
* FEC (P3); backend-neutral MCS metadata (``phy/Modem.hh`` still uses liquid
  types); per-symbol logging (``RadioPacket.symbols``) for native frames.
* OFDMA in the production stack (P5); fragmentation above
  ``--native-max-payload``.
