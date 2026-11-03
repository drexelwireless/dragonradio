Initial OFDMA simulation evaluation
===================================

This evaluation validates the new portable reference PHY. The original C++
MAC/ARQ/USRP path has not been validated, and no RF was transmitted.

Reproduction
------------

Upstream revision: ``da3808d267b54995a6ef95f317c66d1d1b4f22a9``.
Runtime: Python 3.14.4, NumPy 2.5.4. Random seed: 2026.
The implementation is on the local ``codex/ofdma-simulation`` branch.

Run from the checkout::

    make sim-build sim-test PYTHON=.venv-simulation/bin/python
    .venv-simulation/bin/python scripts/standalone-radio.py --simulate \
      --profile multipath --frames 1000 \
      --output simulation-results/multipath.json \
      --export-iq simulation-results/first-frame.npz
    .venv-simulation/bin/python scripts/sim-radio.py --profile awgn \
      --snr-sweep=-5,0,5,10,15,20,25,30 --frames 300 \
      --output simulation-results/snr-sweep.json

The sweep intentionally exits 1 because low-SNR points fail acceptance.

Noisy multipath result
----------------------

At 30 dB channel SNR, both receivers delivered 1000/1000 packets with exact
source, destination, sequence, and payload verification; zero wrong CRC-valid
packets were observed. The users carried independent 256-byte payloads in each
shared waveform. Receiver channels used different FIRs, delays, phases, and CFOs.

.. list-table:: Receiver measurements
   :header-rows: 1

   * - User
     - Mean EVM (dB)
     - Maximum absolute CFO error (subcarrier spacings)
     - Maximum timing error (samples)
   * - 1
     - -27.948
     - 0.001208
     - 0
   * - 2
     - -28.444
     - 0.001259
     - 0

Aggregate simulated payload goodput: 1.053 Mbit/s. This excludes MAC gaps,
retransmissions, hardware overhead, and host execution time. Worst frame PAPR:
12.14 dB; hardware testing requires amplitude normalization and backoff.

AWGN sweep
----------

Each point used 300 frames per receiver and the same payload seed. These are
empirical measurements of uncoded QPSK with this receiver, not a PHY standard
performance claim or an estimate of radio performance.

.. list-table:: Exact packet delivery
   :header-rows: 1

   * - Channel SNR (dB)
     - User 1
     - User 2
     - Meets 1% PER threshold
   * - -5
     - 0/300
     - 0/300
     - No
   * - 0
     - 0/300
     - 0/300
     - No
   * - 5
     - 0/300
     - 0/300
     - No
   * - 10
     - 7/300
     - 10/300
     - No
   * - 15
     - 300/300
     - 300/300
     - Yes
   * - 20
     - 300/300
     - 300/300
     - Yes
   * - 25
     - 300/300
     - 300/300
     - Yes
   * - 30
     - 300/300
     - 300/300
     - Yes

Verification and remaining limitation
-------------------------------------

All 21 portable regression tests passed, including deliberate per-user data
corruption, high/low SNR behavior, IQ replay, and the existing standalone-radio
entry point with ``--simulate``. The compile check passed.

The original ``make dragonradio`` failed at ``src/Clock.hh:10`` because
``uhd/usrp/multi_usrp.hpp`` is unavailable on this Mac. The legacy build also
targets Linux/Python 3.8 and has Linux-specific dependencies. Consequently the
optional native liquidDSP modem baseline remains unvalidated. It fails clearly
when the native runtime is absent instead of substituting the NumPy model.

Local raw artifacts (ignored by Git): ``simulation-results/multipath.json``,
``snr-sweep.json``, ``first-frame.npz``, ``tests.log``, ``native-build.log``, and
``environment.txt``. See :doc:`simulation` for model assumptions and the native
integration work required before cabled radio testing.
