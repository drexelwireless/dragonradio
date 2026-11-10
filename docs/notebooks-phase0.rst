Teaching notebooks, Phase 0: feasibility
========================================

Status 2026-11-10, branch ``codex/native-radiowars2024``. Written for Kapil
Dandekar and Geoffrey Mainland. This is Phase 0 of
:doc:`teaching-notebooks-plan`.

Everything was built and run on grid47 (reservation 3308) in the class image
``krd26/dragonradio-class-gr310-20261027-v2``: Ubuntu 26.04, Python 3.14.4,
64 threads (AMD EPYC 7452), 125 GB. grid47 has an X310 attached. **No USRP
was opened and no UHD tool was run.** Every radio result below comes from
the simulated device (``--sim-air``) or from NumPy channels, and is labeled
as simulation. Nothing used sudo.

Logs and figures are on the shared home under ``~/dragonradio-nb/`` (results
in ``~/dragonradio-nb/results/``), not in git. Run directories with HDF5
logs are on grid47 under ``/tmp/drnb/``.

.. contents::
   :local:
   :depth: 2

Answers
-------

.. list-table::
   :header-rows: 1
   :widths: 30 12 58

   * - Question
     - Answer
     - Evidence
   * - 1. Build the port branch in the class image and run its tests on the
       node?
     - Yes
     - ``grid/build-in-class-image.sh`` plus ``fullstack-tests``: 63 s.
       ``ctest -L fullstack``: 15 of 15 passed in 38.3 s. ``clean-arq``
       passed with both PHYs (below).
   * - 2. Install the notebook packages in a layer above the image without
       changing it?
     - Yes
     - ``grid/build-notebook-layer.sh`` and ``requirements-notebooks.txt``.
       Every package installed from a py3 or cp314 wheel, so nothing was
       compiled and nothing was dropped. Versions are under
       `Pinned versions`_.
   * - 3. Can a Jupyter kernel call the native and liquid PHYs in-process?
     - Yes
     - The kernel's interpreter is the ``dragonradio`` executable itself (see
       `phy mode`_). Native demodulation also needs a small binding, built
       only with ``-DDRAGONRADIO_NOTEBOOK=ON`` (off by default).
   * - 4. drgui's views in a notebook (ipympl) on logs from a simulated run?
     - Yes, with one broken plot class
     - Receive, send and snapshot views run unchanged under
       ``%matplotlib widget``, including their buttons and sliders.
       ``TrafficPlot`` fails on matplotlib 3.10 (in the CLI too). The
       receive view's IQ window looks misplaced in these simulated logs
       (`drgui`_).
   * - 5. Drive a ``live`` full-stack simulation run from a notebook
       reliably?
     - Yes, on an idle node
     - ``clean-arq`` on one hyperthread pair at nice 10, strict checks: 5 of
       5 sequential and 4 of 4 concurrent runs passed with native, and the
       same with liquid. Zero late bursts in all 18 runs.
   * - 6. Does JupyterLab serve from the node through an SSH tunnel?
     - Yes
     - The server binds 127.0.0.1 only. The REST API answers with the token
       (403 without), a cell executed over the websocket, and a tunnel
       through ``dwslgrid`` was tested end to end. The server was stopped
       afterwards.

Build and baseline
------------------

``~/dragonradio-nb/node-build.sh fullstack-tests`` at ``a328367b`` (logs
``~/dragonradio-nb/build-a328367b.log``, ``ctest-a328367b.log``):

* **The first run failed** with ``ln: failed to create symbolic link
  'extern/Catch2'``. ``node-build.sh`` rsyncs with ``--exclude /extern``,
  so a fresh ``/tmp/drnb/dragonradio`` has no ``extern/`` directory.
  ``mkdir -p /tmp/drnb/dragonradio/extern`` once on the node fixed it.
  ``node-build.sh`` is unchanged.
* Configure, build and ``fullstack-tests`` take 63 s with 64 jobs.
  ``ctest -L fullstack``: **15 of 15 passed, 38.3 s**.
* ``clean-arq`` through ``grid/run-sim-fullstack.sh``, simulation (logs
  ``~/dragonradio-nb/baseline-clean-arq.log``, copies in
  ``results/task2/``):

  .. list-table::
     :header-rows: 1

     * - PHY
       - Result
       - 1->2
       - 2->1
       - Wall
     * - native
       - PASSED
       - 801/801, 160.0 kb/s, p50 87.5 ms, 7 retransmissions
       - 801/801, 160.5 kb/s, p50 89.9 ms
       - 36.2 s
     * - liquid (``-- --phy ofdm -c rs8``)
       - PASSED
       - 801/801, 160.0 kb/s, p50 93.9 ms, 7 retransmissions
       - 801/801, 160.0 kb/s, p50 91.3 ms
       - 36.2 s

The notebook layer
------------------

``grid/build-notebook-layer.sh SRC_DIR [NBVENV]`` runs after
``build-in-class-image.sh`` and changes nothing under ``/usr/local``. It is
idempotent: a rerun takes 33 s, the first run about 61 s.

* **NBVENV** (default ``/tmp/drnb/nbvenv``) is a ``virtualenv
  --system-site-packages`` venv. ``.pth`` files put this tree's
  ``python/dragonradio/src`` and ``python/dragonradio-tools/src`` first and
  the image venv's site-packages after them, the same way
  ``build-in-class-image.sh`` builds ``venv/``.
* It installs ``requirements-notebooks.txt``. pip shadowed four Debian
  packages inside NBVENV only (typing-extensions, tinycss2, markdown-it-py,
  babel: "Not uninstalling ... outside environment"). ``pip check`` reports
  only Debian ``types-*`` stub packages that were already in the image.
* It builds ``dragonradiocpp_tools_mgen`` into NBVENV. ``dragonradio.tools``
  imports this pybind11 extension (``python/dragonradio-tools/csrc``), and
  neither the image venv nor ``build-in-class-image.sh`` builds it.
* It builds the tree again with ``-DDRAGONRADIO_NOTEBOOK=ON`` into
  ``NBVENV/dragonradio-build``. That is outside the tree, so
  ``node-build.sh``'s ``rsync --delete`` leaves it alone. The tree's own
  ``build/`` stays the default build.
* It links ``NBVENV/bin/dragonradio-python`` to that executable.
* It installs two kernelspecs in ``NBVENV/share/jupyter/kernels``:
  ``dragonradio`` (in-process PHYs) and ``drnb-python3`` (NBVENV's own
  Python, no ``dragonradiocpp``). The image's ``python3`` kernel runs
  ``/usr/bin/python3``, which does not see NBVENV.

``jupyter_core`` and ``jupyter_client`` come from the image, so NBVENV has
no ``bin/jupyter``. Use ``NBVENV/bin/jupyter-lab``, ``papermill`` and
``jupytext`` directly.

Pinned versions
~~~~~~~~~~~~~~~

Requested: jupyterlab 4.6.6, ipympl 0.10.0, papermill 2.7.0, nbclient 0.10.4
(the image's copy; not reinstalled), jupytext 1.19.6, nbstripout 0.9.1,
myst-nb 1.4.0.

Added for log reading: xdg 6.0.0, which ``dragonradio.tools`` imports, and
fastparquet 2026.9.0, the engine of its DataFrame cache, together with
cramjam 2.14.0 and fsspec 2026.9.0. The image lacks both. All 47
dependencies are pinned as resolved.

The image provides the rest: ipykernel 7.1.0, ipywidgets 8.1.5,
matplotlib 3.10.7, numpy 2.3.5, pandas 2.3.3, h5py 3.15.1, scipy 1.16.3,
notebook 6.4.13.

.. _phy mode:

``phy`` mode: PHYs in the kernel
--------------------------------

(a) Image Python: fails
~~~~~~~~~~~~~~~~~~~~~~~

``import dragonradiocpp`` appears to work. In fact it imports the stub
package
``/usr/local/dragonradio/venv/.../site-packages/dragonradiocpp``, which holds
only ``.pyi`` files, as an empty namespace package. ``import
dragonradiocpp.radio``, ``dragonradio.radio`` and ``dragonradio.liquid`` then
raise ``ModuleNotFoundError``. The tree's venv behaves the same way. The
cause: ``dragonradiocpp`` exists only as an embedded (inittab) module of the
executable.

(b) The executable as the kernel's interpreter: works (chosen)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``src/main.cc`` already provides for this. When ``argv[0]`` ends in
``python``, ``main`` calls ``Py_BytesMain(argc, argv)``: the normal CPython
command line (``-m``, ``-c``, scripts) with ``dragonradiocpp`` built in. The
image already ships such a link (``/usr/local/dragonradio/venv/bin/dragonpython``).
No launcher script is needed.

* ``NBVENV/bin/dragonradio-python -> NBVENV/dragonradio-build/dragonradio``.
  Because the link lives in ``NBVENV/bin``, CPython finds NBVENV's
  ``pyvenv.cfg``, so ``sys.prefix`` is NBVENV and the kernel sees the layer's
  packages. ``dragonradiocpp`` reports itself as ``(built-in)``.
* The kernelspec ``argv`` is ``[NBVENV/bin/dragonradio-python, -m,
  ipykernel_launcher, -f, {connection_file}]``, with ``VIRTUAL_ENV`` set in
  ``env``.
* Lifecycle through ``jupyter_client``
  (``results/kernel-lifecycle.log``, ``kernel-interrupt2.log``):

  * start in 1.4 s, restart in 1.6 s, shutdown in 0.5 s;
  * interrupting Python code takes 0.19 s;
  * an interrupt during a long C++ call (which releases the GIL) is raised
    when the call returns. A 150-million-sample ``demodulate`` ran 3.5 s,
    then ``KeyboardInterrupt`` fired before the next statement.
* The kernel opens no radio and needs no root. It runs as uid 50005 with
  ``CapEff`` 0: the sudo and capability code in ``main`` is skipped in
  Python mode. ``libuhd`` is mapped because it is linked in, and UHD prints
  its version banner on stderr at start. No device is opened.
* Caveat: the executable is built with ``-Ofast``, so flush-to-zero is on
  for the whole kernel process. numpy warns at import that the smallest
  float32 subnormal is zero. Only denormals are affected.

Approach (c), building ``dragonradiocpp`` as a loadable module, was not
needed and not tried.

The binding native demodulation needs
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Liquid OFDM already has Python modem bindings. The native PHY does not: its
``PacketDemodulator.demodulate`` is bound with a raw ``const complex<float>*``
argument, and ``setCallback`` is not bound at all. So native packets cannot
be demodulated from Python in any mode.

``-DDRAGONRADIO_NOTEBOOK=ON`` (``CMakeLists.txt``, off by default) compiles
``src/python/Notebook.cc`` into the executable and adds:

* ``dragonradiocpp.notebook.modulate(phy, payload, mcsidx=0, curhop=1,
  nexthop=2, seq=0, team=0, gain=1.0)``, which returns ``complex64`` IQ;
* ``dragonradiocpp.notebook.demodulate(phy, iq, node_id=2, team=0, chunk=0,
  rx_rate=1e6)``, which returns one dict per frame whose header decoded.

Both work for any PHY. They drive the modulator and demodulator the way
``tests/fullstack/test_native_ofdm.cc`` does, with the GIL released. With
the option off, the default build compiles exactly the sources it did
before. On grid47, the default executable's ``dragonradiocpp`` has no
``notebook`` submodule and ``DRAGONRADIO_NOTEBOOK:BOOL=OFF``.

Proof: ``notebooks/phase0/01_phy_inprocess``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This notebook runs headless on the ``dragonradio`` kernel. It is simulation:
NumPy AWGN, not real time. Results are in ``results/phase0/phy/``
(``phy_summary.csv``, ``delivery_evm.png``, ``constellation.png``,
``psd.png``). 2000 packets took 2.9 s and the notebook 15 s.

* Setup: both PHYs use the radio's defaults (M=48, CP 6, default
  subcarriers), and the same payload MCS (CRC32, RS8, no inner code, QPSK)
  with 500-byte payloads. Native has its v29 header. Liquid has
  CRC32/v29p78/BPSK headers and a 4-sample taper.
* SNR is the frame's mean sample power over the noise power in the full
  1 MHz band. Both PHYs occupy about 38 of 48 subcarriers, so the SNR per
  occupied subcarrier is about 1 dB higher.

.. list-table:: Delivery against SNR (100 packets per point, AWGN, simulation)
   :header-rows: 1

   * - SNR [dB]
     - 0
     - 2
     - 4
     - 6
     - 8
     - 10
     - 20
   * - native header
     - 0.35
     - 0.92
     - 1.00
     - 1.00
     - 1.00
     - 1.00
     - 1.00
   * - native payload
     - 0
     - 0
     - 0
     - 0
     - 1.00
     - 1.00
     - 1.00
   * - liquid header
     - 0.04
     - 0.54
     - 0.97
     - 1.00
     - 1.00
     - 1.00
     - 1.00
   * - liquid payload
     - 0
     - 0
     - 0
     - 0
     - 0.83
     - 1.00
     - 1.00

**EVM.** The PHYs report EVM under different definitions (checked in the
source):

* **Native** (``src/simulation/NativeFrame.hh``, ``decode_payload``) reports
  10·log10 of the mean squared distance from each equalized payload data
  symbol to its nearest constellation point.
* **Liquid** (``ofdmflexframesync.c``) reports 10·log10 of the mean squared
  modem EVM over the BPSK **header** symbols only.

Both are decision directed. The notebook adds a **reference EVM** computed
the same way for both, from the IQ:

#. cut the frame into OFDM symbols of M+CP samples, from its first sample;
#. take the FFT of each transmitted and received symbol;
#. fit one complex gain per symbol;
#. take error power over signal power on all occupied subcarriers.

That covers the preamble, pilots, header and payload. The reference EVM is
aided by the known data and the true timing, so it measures the received
signal, not each receiver's channel estimate. It matches the AWGN prediction
−(SNR + 10·log10(M/n_occupied)) within 0.28 dB for SNR ≥ 0 dB.

At 20 dB SNR:

.. list-table::
   :header-rows: 1

   * - PHY
     - Reported by the PHY
     - Reference
   * - native
     - −20.9 dB
     - −21.1 dB
   * - liquid
     - −20.1 dB
     - −21.3 dB

Liquid's header-only figure reads about 1.2 dB worse than its own signal.
The PHY-reported EVMs should not be compared across PHYs. The reference EVM
can be.

Fallback (d): ``scripts/phy-npz.py`` in a subprocess
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``scripts/phy-npz.py`` handles ``modulate`` and ``demodulate`` for
``native`` or ``liquid``, passing arrays through ``.npz`` files. It runs
under ``dragonradio-python`` and needs the ``DRAGONRADIO_NOTEBOOK``
executable.

``notebooks/phase0/02_phy_subprocess`` drives it from the ``drnb-python3``
kernel, which cannot import ``dragonradiocpp`` (the notebook asserts that).
The channel stays in NumPy. A call takes 0.17–0.26 s for 50 packets, mostly
process start. Delivery agrees with the in-process run: native payload 1.0
from 8 dB, liquid 0.8 at 8 dB and 1.0 at 12 dB. Results are in
``results/phase0/phy-subprocess/``. This is the path for a kernel that
cannot host DragonRadio, such as stock Colab.

.. _drgui:

drgui in a notebook
-------------------

**Logs.** ``grid/run-sim-fullstack.sh TREE OUT --scenario clean-arq
--duration 6 -- [--phy ofdm -c rs8] --log-iq --log-tx-records
--log-snapshots --snapshot-frequency 1.0 --snapshot-duration 0.1``.

* ``--log-iq`` turns on ``log_slots``, ``log_recv_symbols`` and
  ``log_sent_iq``.
* Both runs passed, 241/241 each way, and wrote 26–30 MB of HDF5 per node.
* On grid47: ``/tmp/drnb/sim/drgui-{native,liquid}/clean-arq/logs``.

**Notebook.** ``notebooks/phase0/03_drgui_ipympl`` runs on the
``dragonradio`` kernel with ``%matplotlib widget`` (headless: 27.5 s). Its
step table is ``results/phase0/drgui/drgui_steps.csv``, and its PNGs are in
the same directory.

* **Works unchanged:**

  * ``LogCollection`` loading;
  * ``LogViewer.rxView``, ``txView`` and ``snapshotView``: plotting, the
    Next button, the slider (``set_val`` moves to the packet), and Link to
    TX / Link to RX;
  * ``savefig``;
  * ``ConstellationPlot``, ``PSDPlot``, ``SpecgramPlot`` and ``EventPlot``
    (SEND, RECV, TXRECORD).
* **Broken: ``TrafficPlot``.** It fails with ``AttributeError:
  'CheckButtons' object has no attribute 'rectangles'``
  (``dragonradio/tools/plot/util.py:292``, ``addCheckboxWidget``). Matplotlib
  removed that attribute (deprecated in 3.7), and the image has 3.10, so the
  ``plot-traffic`` CLI fails too. The notebook's check expects this failure.
* **No native constellation.** The native PHY does not log received symbols
  (``RadioPacket.symbols`` is empty), so ReceiveView's constellation is empty
  for native. Liquid fills it with ``--log-iq``.
* **Needs a closer look: the received-packet IQ window.** In the liquid
  receive view (``liquid_rxview.png``), packet 5's window holds only noise,
  while a strong burst sits about 15 ms earlier. Native packet 5 is about
  22 % signal. I measured this over all of node 2's packets with a rough
  heuristic: the share of the extracted window above 10× the slot's median
  power.

  * Less than half the window above that threshold: 99 of 241 native packets
    and 217 of 241 liquid packets.
  * Window shorter than the frame, because it runs past the logged slot IQ:
    33 native and 96 liquid packets.

  ``Slots`` handles negative ``start_samples`` correctly. The logged receive
  buffers vary from 86 to 71 721 samples. I did not find the cause tonight.
  Evidence: ``results/phase0/drgui/slot-alignment.txt`` and
  ``~/dragonradio-nb/scripts/slot_align_probe2.py``.

**Compiled imports that log reading needs.** I probed this
(``~/dragonradio-nb/scripts/logread_probe.py``) on NBVENV's Python, which has
no ``dragonradiocpp``:

#. ``dragonradio.liquid``: ``CRCScheme``, ``FECScheme`` and
   ``ModulationScheme``, for the MCS categories and to parse the logged
   config.
#. ``dragonradio.signal.decompressIQData``: slot and snapshot IQ are stored
   compressed (``int8``). Sent-packet IQ is stored as ``complex64`` and needs
   nothing.
#. ``dragonradiocpp_tools_mgen``: imported at module load through
   ``tools.logging`` → ``tools.colosseum.logging`` → ``tools.mgen``. The
   pure-Python ``xdg`` is also needed and was missing from the image.

With pure-Python stand-ins for (1) and (2), and the mgen extension built,
``dragonradio.tools.logging`` and ``drgui`` import and load these logs on
plain Python.

**What a drgui refactor would need** (not done tonight):

* views that size their own figures (in a notebook they get matplotlib's
  6.4 × 4.8 default and are cramped; the notebook sets
  ``figure.figsize``);
* a fix for ``addCheckboxWidget`` on matplotlib ≥ 3.9;
* liquid enums loaded lazily, and a NumPy ``decompressIQData`` fallback;
* ``tools.logging`` that does not import ``tools.colosseum`` (and with it
  mgen) at import time;
* the split into draw functions and controllers, as the plan describes.

``mp.use('GTK3Agg')`` appears only in the CLI entry points, and the views'
``fig.show()`` is harmless under ipympl.

``live`` mode
-------------

``notebooks/phase0/04_live_sim`` runs on the ``drnb-python3`` kernel:

* It runs ``taskset -c PAIR nice -n 10 bash grid/run-sim-fullstack.sh TREE
  RUNDIR --scenario SCENARIO [-- radio args]`` in its own session, so a
  timeout kills the air relay and both radios. An ipywidgets progress bar
  shows progress.
* It parses ``summary.json`` and the receivers' ``nodeN.json``, and plots
  goodput (1 s bins from arrival times) and latency (p50, p95 and max).
* ``TOLERANT=True`` adds the Colab-probe settings.

``notebooks/phase0/run-live-batch.sh [PHY]`` runs it headless 5 times in a
row on cpus 4,36, then 4 copies at once on 8,40, 9,41, 10,42 and 11,43. A
sampler recorded every simulation process's affinity every 10 s: all had
their pair and ``ni`` 10.

``clean-arq`` with strict defaults, simulation. The scenario's checks
include "no late TX bursts" and "no late or dropped RX bursts" on both nodes.

.. list-table::
   :header-rows: 1

   * - PHY
     - Sequential (5)
     - Concurrent (4)
     - Late TX bursts
     - Late or dropped RX
     - Goodput each way
     - p50 latency
   * - native
     - 5 PASS
     - 4 PASS
     - 0
     - 0
     - 160.0–160.6 kb/s
     - 87.5–92.0 ms
   * - liquid
     - 5 PASS
     - 4 PASS
     - 0
     - 0
     - 160.0–160.5 kb/s
     - 89.6–94.5 ms

Each run takes 40.0–40.6 s; each notebook execution 44.0–45.0 s. Tables:
``results/phase0/live-batch/table-{native,liquid}.txt``. Per-run
``result.json``, PNGs and executed notebooks are in the same directory.

So one physical core (two hyperthreads) holds strict timing for a two-node
``clean-arq``: two radios, the air relay and the runner. **Caveat:** grid47
was otherwise idle, and 4 concurrent runs used 8 of 64 threads. This is not
a capacity test, and it does not revise the earlier figure (8 runs pass on a
32-thread node, 12 do not).

JupyterLab through an SSH tunnel
--------------------------------

Tested with ``~/dragonradio-nb/scripts/lab_test.sh`` (log
``results/phase0/jupyterlab-serving.log``):

* ``NBVENV/bin/jupyter-lab --no-browser --ip=127.0.0.1 --port=8890`` starts
  in about 1 s. ``ss`` shows it listening on 127.0.0.1:8890 only.
* ``/api/status``: 200 with the token, 403 without. ``/lab``: 200.
* ``/api/kernelspecs`` lists ``dragonradio``, ``drnb-python3``, ``python3``
  (``/usr/bin/python3``, the default) and the shared home's
  ``~/.local/share/jupyter/kernels`` entries (miniforge envs).
* One cell was executed through REST (``POST /api/kernels``) and the
  websocket (``execute_request``) on both kernels, in 0.7 s and 0.6 s
  including kernel start. The kernels were deleted (204) and the server
  stopped.
* Lab extensions present: ``jupyter-matplotlib`` 0.12.0 (ipympl) and
  ``jupyterlab-jupytext`` from NBVENV, and
  ``@jupyter-widgets/jupyterlab-manager`` 5.0.13 from the image. Rendering
  in a browser was not checked.
* The tunnel was tested end to end from gridgpu01 through ``dwslgrid``,
  which is the same path a laptop takes: ``/api/status`` and ``/lab``
  returned 200 through ``-L 127.0.0.1:18890:127.0.0.1:8890``. Then the
  tunnel was closed and the server stopped.

To use it, start JupyterLab on the node from gridgpu01. The last line
prints the URL with the token:

.. code-block:: sh

   ~/dragonradio-nb/nodessh.sh grid47 'DRAGONRADIO_TREE=/tmp/drnb/dragonradio setsid -f \
     /tmp/drnb/nbvenv/bin/jupyter-lab --no-browser --ip=127.0.0.1 --port=8890 --port-retries=0 \
     --ServerApp.root_dir=/tmp/drnb/dragonradio/notebooks > /tmp/drnb/lab.log 2>&1 < /dev/null; \
     sleep 4; grep -m1 -o "http://127.0.0.1:8890/lab?token=[0-9a-f]*" /tmp/drnb/lab.log'

From a laptop, open the tunnel, then browse to
``http://127.0.0.1:8888/lab?token=TOKEN``:

.. code-block:: sh

   ssh -N -L 8888:127.0.0.1:8890 \
       -o UserKnownHostsFile=~/.ssh/known_hosts.grid -o StrictHostKeyChecking=accept-new \
       -o ProxyCommand="ssh -W %h:%p krd26@dwslgrid.ece.drexel.edu" krd26@grid47

``-J krd26@dwslgrid.ece.drexel.edu`` in place of the ``ProxyCommand`` does
the same. A separate known-hosts file keeps grid host keys, which change
with each reservation, out of the main one.

Stop the server:

.. code-block:: sh

   ~/dragonradio-nb/nodessh.sh grid47 'pkill -u $USER -x jupyter-lab'

What failed
-----------

* The first ``node-build.sh`` run failed because ``extern/`` was missing on
  a fresh node tree (`Build and baseline`_).
* ``import dragonradiocpp`` on the image's Python (approach a).
* The native PHY cannot demodulate from Python without a new binding
  (``DRAGONRADIO_NOTEBOOK``).
* ``dragonradio.tools.logging`` cannot be imported with the image as it
  is: ``dragonradiocpp_tools_mgen`` and ``xdg`` are missing. The layer now
  provides both.
* ``TrafficPlot`` fails on matplotlib 3.10.
* drgui's receive view probably shows the wrong IQ for many received
  packets in these simulated logs. The cause is unknown.
* Native logs carry no received symbols, so the receive view has no native
  constellation.
* ``jupytext`` refused to write next to a ``.py`` on the NFS home ("modified
  while Jupytext was running"). Converting in node-local ``/tmp`` works.
* One cleanup slip. A ``pgrep -f`` meant to find the test tunnel matched my
  own shell. I then killed the tunnel ssh and the server by exact PID, and
  checked that nothing was left on either host.

Reproduce
---------

On gridgpu01, in a clone at this branch's HEAD:

.. code-block:: sh

   ~/dragonradio-nb/nodessh.sh grid47 'mkdir -p /tmp/drnb/dragonradio/extern'   # once per fresh node
   ~/dragonradio-nb/node-build.sh fullstack-tests
   ~/dragonradio-nb/nodessh.sh grid47 'cd /tmp/drnb/dragonradio/build && ctest -L fullstack'
   ~/dragonradio-nb/nodessh.sh grid47 'bash /tmp/drnb/dragonradio/grid/build-notebook-layer.sh /tmp/drnb/dragonradio'
   # drgui logs (notebook 03 reads these paths)
   ~/dragonradio-nb/nodessh.sh grid47 'cd /tmp/drnb && L="--log-iq --log-tx-records --log-snapshots --snapshot-frequency 1.0 --snapshot-duration 0.1";
     bash dragonradio/grid/run-sim-fullstack.sh /tmp/drnb/dragonradio /tmp/drnb/sim/drgui-native --scenario clean-arq --duration 6 -- $L;
     bash dragonradio/grid/run-sim-fullstack.sh /tmp/drnb/dragonradio /tmp/drnb/sim/drgui-liquid --scenario clean-arq --duration 6 -- --phy ofdm -c rs8 $L'
   # notebooks, headless (executed copies go to ~/dragonradio-nb/results/phase0)
   for nb in 01_phy_inprocess 02_phy_subprocess 03_drgui_ipympl 04_live_sim; do
     ~/dragonradio-nb/nodessh.sh grid47 "cd /tmp && DRAGONRADIO_TREE=/tmp/drnb/dragonradio \
       bash /tmp/drnb/dragonradio/notebooks/phase0/run-headless.sh /tmp/drnb/dragonradio/notebooks/phase0/$nb.py"
   done
   ~/dragonradio-nb/nodessh.sh grid47 'bash /tmp/drnb/dragonradio/notebooks/phase0/run-live-batch.sh native'

Each notebook is committed as a stripped ``.ipynb`` paired with a jupytext
percent ``.py``. Edit the ``.py``, then regenerate the ``.ipynb`` with
``jupytext --to ipynb`` and ``nbstripout`` in a node-local directory.

What Phase 1 should change in the plan
--------------------------------------

* **Settle how ``phy`` mode works.** Kernels run the ``dragonradio``
  executable under a ``*python`` name. No loadable module and no
  ``dragonradiocpp`` rebuild are needed. The notebook helpers (or something
  like them) belong in the default build: "Verify importing DragonRadio
  inside the kernel" is done, and what remains is the native demodulator's
  Python entry point. Decide with Geoff whether ``dragonradiocpp.notebook``
  should be on by default, or should become proper ``PacketDemodulator``
  bindings (``setCallback`` plus a NumPy ``demodulate``).
* **Make the layer the container.** ``build-notebook-layer.sh`` is what the
  ``dragonradio-nb`` image layer should run. Add to it:

  * the ``drnb`` launcher (per-user port, cpuset and ``limits.cpu``, nice 10,
    a token);
  * ``c.MultiKernelManager.default_kernel_name = "drnb-python3"``, since the
    default is now the image's ``/usr/bin/python3``;
  * ``JUPYTER_PATH`` and ``JUPYTER_DATA_DIR`` isolation, because the shared
    home's ``~/.local`` kernels show up in every user's list.
* **Fix log reading before notebooks 00 and 01.** Make ``tools.logging``
  import neither ``tools.colosseum`` nor mgen eagerly. Add the NumPy
  ``decompressIQData`` and the lazy enums (already in the plan). Fix
  ``addCheckboxWidget`` for matplotlib 3.10. Then investigate the
  receive-view IQ alignment on simulated logs. drgui's figures are a
  teaching centrepiece, so "figures match drgui exactly" needs drgui to be
  right first.
* **Have the native PHY log its received symbols** (``log_recv_symbols``), or
  the receive view's constellation stays empty for native.
* **Adopt the reference EVM** from ``01_phy_inprocess`` as the "one EVM
  definition" for ``compare`` (data-aided, per-symbol gain, all occupied
  subcarriers). It works for both PHYs from IQ alone, and it tracks theory
  within 0.3 dB in AWGN. A logged, receiver-side EVM can come later.
* **Re-measure capacity.** In Phase 0, one physical core per student run held
  strict timing for ``clean-arq``, 4 at a time on a 64-thread node, for both
  PHYs. The capacity number should come from a sweep at 8, 16 and 32
  concurrent pinned runs on grid47, with a heavier scenario (``saturation``
  or OFDMA). It should not be scaled from the 32-thread emulation figure.
* **Colab:** ``scripts/phy-npz.py`` is the ``phy``-mode path if the Colab
  bundle ships ``dragonradio`` (built with ``DRAGONRADIO_NOTEBOOK``). The
  ``drgui`` notebook needs the log-reading fixes above before it can run
  there.
