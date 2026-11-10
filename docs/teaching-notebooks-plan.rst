Teaching and Evaluation Notebooks: Technical Plan
=================================================

This plan expands work package 3 (*Notebook foundation*) and the *Simulation
notebooks* section of :doc:`development-plan-mac-sensing`. It covers notebooks
that run inside the DragonRadio container and serve three groups:

* **Students.** Each notebook teaches one part of the radio. It links to
  theory, includes diagrams, runs the real radio code, and shows simulation and
  over-the-air results.
* **Developers.** Notebooks compare native and liquid PHYs and measure what a
  new feature is worth, with a fixed method.
* **Regression.** Every notebook can run headless as a test.

Starting point
--------------

Already in place:

* **Jupyter.** The class image (``dragonradio-class-gr310-20261027-v2``) has
  Jupyter Notebook, ipywidgets, matplotlib and h5py, but not JupyterLab.
* **No simulator in the image.** The image's DragonRadio (radiowars2024
  ``5cf3e7d``) has no simulator. The port branch ``codex/native-radiowars2024``
  has the simulated device, ``scripts/sim-fullstack.py`` with about 30 scenarios
  (ARQ, queues, clock offset, OFDMA, TAP/TCP), the native PHYs, and ``drgui``.
* **drgui.** ``dragonradio.tools.plot.radio`` has ``drgui`` (receive, send and
  snapshot views built on matplotlib widgets) and reusable plot classes:
  ``SpecgramPlot``, ``ConstellationPlot``, ``PSDPlot``, ``PAPRPlot``,
  ``EventPlot``, ``TrafficPlot`` and ``RadioMetricPlot``.
* **Log readers.** ``dragonradio.tools.logging`` reads logs with ``Log`` and
  ``LogCollection`` (HDF5 to pandas). It imports compiled modules
  (``dragonradio.radio.decompressIQData`` and the liquid enums).
* **Capacity.** On a 32-thread node, 8 live runs at once pass the strict timing
  checks; 12 do not (``grid/concurrency-probe.sh``). Colab cannot hold strict
  timing (``notebooks/colab``).
* **Existing material.** ``sdr-lab-class`` has companion notebooks to follow
  for style (week06 DragonRadio LLC, week08 OFDM).
* **Over-the-air data to reuse.** ota02 (native against liquid, OFDMA against
  TDMA), step2 (paired retransmissions), diag05 (grid57 fractional delay), and
  the OFDMA slot-fill fix (163 to 214 kb/s).

Architecture
------------

Four execution levels
~~~~~~~~~~~~~~~~~~~~~

Every notebook declares which levels it supports. A single ``MODE`` parameter
at the top picks one.

.. list-table::
   :header-rows: 1
   :widths: 12 38 25 25

   * - Level
     - What runs
     - Time
     - Where
   * - ``load``
     - Precomputed data packs (simulation sweeps, OTA campaigns)
     - Seconds
     - Anywhere, including Colab
   * - ``phy``
     - Native or liquid modulator and demodulator called in the kernel, with
       NumPy channels. Not real time.
     - Seconds
     - Container; Colab with the bundle
   * - ``live``
     - ``sim-fullstack.py`` scenario in a pinned subprocess, in real time
     - 30–60 s per run
     - Grid container only
   * - ``ota``
     - Grid campaign through ``ota-pipeline.sh`` under a reservation
     - Minutes to hours
     - Instructor or developer only, within the RF approvals

Sweeps always use ``load``. Students run one representative point ``live``,
then check it against the precomputed sweep. That keeps 30 students within the
8 live runs a node can support.

Container image
~~~~~~~~~~~~~~~

Build a notebook layer, ``dragonradio-nb``, on the class image instead of
changing the image itself:

* DragonRadio rebuilt from the port branch (simulator and native PHYs) with
  ``grid/build-in-class-image.sh``.
* Added packages: ``jupyterlab``, ``ipympl`` (drgui's widgets inside notebooks),
  ``papermill`` and ``nbclient`` (headless runs), ``jupytext``,
  ``nbstripout``, and ``myst-nb`` for docs. Pin the versions.
* A ``drnb`` launcher that starts JupyterLab on a per-user port. It also sets
  ``limits.cpu`` and a cpuset so each user gets two dedicated hyperthreads, and
  runs radios at nice 10 with the Colab-probe settings as defaults.
* Access through an SSH tunnel via ``dwslgrid``. Move to JupyterHub with a
  DockerSpawner/LXD spawner only if a pilot shows the tunnel is too much for
  students.

For Colab, extend the existing ``drsim.tar.gz`` bundle. Support ``load`` and
``phy`` only, plus ``live`` with tolerant settings, labelled as a demo.

Shared library: ``dragonradio.notebook``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Add a small, tested package next to ``dragonradio.tools`` so notebooks stay
short and hold teaching content, not plumbing.

.. list-table::
   :header-rows: 1
   :widths: 18 82

   * - Module
     - Responsibility
   * - ``run``
     - ``run_scenario(name, phy=..., **overrides)`` wraps ``sim-fullstack.py``.
       It pins CPUs, shows an ipywidgets progress bar and enforces a timeout.
       It caches by a hash of (scenario, arguments, git SHA, image ID) and
       returns a ``RunResult`` with the summary, per-node logs and pass/fail.
   * - ``data``
     - Data-pack manifest (SHA-256, git SHA, image, nodes, date, RF settings)
       with fetch, verify and load. Loaders return pandas frames from the
       existing ``Log``/``LogCollection`` and the summary JSON.
   * - ``views``
     - Notebook versions of the drgui views (see below), plus slot timelines,
       ARQ sequence diagrams, queue depth and goodput plots.
   * - ``theory``
     - Analytic references to overlay: BER/SER over AWGN for each modulation,
       pure and slotted ALOHA, TDMA and OFDMA efficiency with guard and
       overhead, Shannon capacity, and M/M/1 queue delay.
   * - ``compare``
     - Paired native-against-liquid and feature A/B analysis (method below).
   * - ``diagram``
     - The radio's block diagram as SVG, with a per-notebook highlight on the
       block being taught, linked to the source files.

**Porting drgui.** Split each drgui view (``ReceiveView``, ``SendView``,
``SnapshotView``) into a pure function that draws onto given axes, and a thin
controller. The CLI keeps its matplotlib Slider/Button controller. Notebooks
get an ipywidgets controller (packet slider, node selector, send/receive
link). Both use the same plot classes, so figures match drgui exactly and
improvements reach both. As a stopgap, ``%matplotlib widget`` with ipympl runs
the current drgui almost unchanged.

**Reading logs without the compiled module.** For ``load`` mode and Colab,
reading a log must not need ``dragonradiocpp``. Two parts:

* Add a pure-NumPy ``decompressIQData`` fallback.
* Load the liquid enums lazily.

Store data-pack IQ already decompressed when its size is reasonable.

Notebook template
~~~~~~~~~~~~~~~~~

Every notebook has the same sections, so students know where to look and the
headless runner knows what to check:

#. **Objectives**, and which levels the notebook supports.
#. **Theory.** Short derivations with links to the course text, standards
   (802.11a OFDM, Abramson ALOHA) and the matching ``docs/*.rst`` page.
#. **Where this lives in DragonRadio.** The highlighted block diagram, with
   links to source (for example ``src/phy/``, ``src/mac/TDMA``,
   ``src/net/Queue``).
#. **Predict.** Students write down what they expect before running anything.
#. **Run** at the chosen ``MODE``.
#. **Look.** drgui-style figures: constellation, PSD, spectrogram, slots, ARQ.
#. **Compare** with theory, then simulation with over the air.
#. **Exercises**, with solutions in a separate instructor branch.
#. **For developers** (optional): native against liquid, open issues, and
   links to the evaluation notebook.

Hidden cells tagged ``check`` compare key numbers with tolerances, for example
"simulated goodput within 5% of the TDMA bound". These cells are what make a
notebook a test.

Use ``jupytext`` to pair each notebook with a ``.py`` percent-format file, so
review diffs are readable. ``nbstripout`` keeps outputs out of git.

Curriculum
----------

The order follows the radio from bottom to top. Each notebook relies only on
scenarios and data that exist today, except where a work package is named.

.. list-table::
   :header-rows: 1
   :widths: 30 40 30

   * - Notebook
     - Content and figures
     - Sources
   * - 00 Tour
     - Container, architecture, first run, drgui tour
     - ``clean-arq``
   * - 01 OFDM PHY
     - Subcarriers, CP, pilots, PAPR; constellation and PSD; EVM against SNR
       for native and liquid
     - ``phy`` mode, ``characterize-phy.py``
   * - 02 Impairments
     - CFO, timing, multipath, phase noise. Case studies: the grid57
       fractional-delay failure and its fix (2-sample FFT window backoff); the
       Wiener phase-noise gap between native and liquid
     - ``phy`` mode, diag05 and 2026-11-08 OTA packs; package 2 for live
       multipath
   * - 03 FEC and framing
     - Code rate, header/payload split, frame length against slot
     - ``native-fec.rst``, ``phy`` mode
   * - 04 TDMA and timing
     - Slots, guards, send lead, late bursts, clock offset and PPS sync
     - ``clock-offset-*``, ota02 late-burst data
   * - 05 ARQ
     - Windows, retransmission, ACK loss, erasures
     - ``clean-arq``/``noarq``, ``erasure-*``, ``ack-loss``, step2 pack
   * - 06 Queues and AQM
     - Tail drop, push-out, RED; delay against load
     - ``*-saturation``, ``red-tuned``
   * - 07 Transport over radio
     - TCP over a lossy link with and without ARQ
     - ``tap-tcp-*``
   * - 08 OFDMA
     - Strong and weak users, unequal demand, slot fill as a feature-value
       case study (163 to 214 kb/s)
     - ``ofdma-*``, ota02, diag05
   * - 09 Simulation against OTA
     - What the simulator does and does not model; the stall bug (radio clock
       read under a lock) as a systems lesson
     - ota02, Colab probe

Gated by the development plan: ALOHA/FDMA (package 4), spectrum sensing
(package 5, offline on recorded IQ first), CSMA (package 6) and DSA (package 7).

Developer evaluation notebooks
------------------------------

A separate track, ``notebooks/eval/``, uses the same library and is
parameterised with papermill, so one notebook produces a report for each
feature:

* **Native against liquid dashboard.** Built from campaign data packs:
  delivery, latency percentiles, retransmissions, late bursts, and goodput by
  cell and PHY.
* **Feature value report.** Covers the hypothesis, metric, simulation
  scenarios, OTA cells and block design. The method reuses step2's paired
  analysis: paired trials within a block, effect size with a bootstrap
  confidence interval, and block-to-block noise reported next to the effect.
  Past features that can serve as examples: OFDMA slot fill, phase tracking,
  push-out and the stall fix.
* **One EVM definition.** Liquid's logged EVM is measured on BPSK header
  symbols and native's on payload, so the two cannot be compared today. Add one
  EVM computed the same way for both, either in ``compare`` from logged IQ
  against a known reference or as a field both PHYs log. Until then, the
  dashboards show only delivery, latency and retransmissions across PHYs.

Rendered reports go into the docs as HTML, so a decision about a feature points
to a report that can be run again.

Data management
---------------

* ``simulation-results/`` stays gitignored. Selected results become data packs:
  a versioned tarball plus ``manifest.json``.
* Data packs are stored on grid storage for the container, mirrored to Drive
  or a GitHub release for Colab, and fetched and cached by ``data``.
* Keep the packs small. Store summaries and metrics in full, but IQ snapshots
  only for the packets a notebook actually shows.
* A ``make datapacks`` target rebuilds packs from campaign directories, so
  each figure can be traced back to its run.

Testing and documentation
-------------------------

* ``make notebooks-test`` runs every notebook with ``MODE=load`` through
  papermill, then checks the ``check`` cells. It is fast and runs anywhere.
* A nightly grid job runs ``MODE=live`` on one pinned node. It catches
  simulator or API drift between the port branch and the notebooks.
* Sphinx renders executed ``load``-mode notebooks with ``myst-nb``. The theory
  in ``docs/*.rst`` and the notebooks link to each other.

Phases
------

.. list-table::
   :header-rows: 1
   :widths: 22 53 25

   * - Phase
     - Work
     - Effort
   * - 0 Feasibility
     - Decide which branch hosts the notebooks. Build ``dragonradio-nb``.
       Verify importing DragonRadio inside the kernel for ``phy`` mode. Run
       drgui under ipympl.
     - 1 week
   * - 1 Foundation
     - ``dragonradio.notebook`` (``run``, ``data``, ``views``, ``theory``),
       template, log reading without the compiled module, data-pack format,
       ``notebooks-test``; notebooks 00 and 01
     - 2–3 weeks
   * - 2 Core curriculum
     - Notebooks 02–09
     - 3–5 days each
   * - 3 Evaluation track
     - ``compare``, the common EVM, dashboard and feature report; docs
       rendering
     - 2 weeks
   * - 4 Pilot
     - A few students per node; measure capacity under real use; decide on
       JupyterHub; Colab subset
     - 1 term

Decisions and risks
-------------------

* **Where the notebooks live (decided 2026-11-09).** The notebooks and
  ``dragonradio.notebook`` live on ``codex/native-radiowars2024``, not
  ``codex/ofdma-simulation``. The class image runs radiowars2024, which has a
  different Python layout (``python/dragonradio/src``, ``dragonradiocpp``).
* **The in-kernel PHY import is unverified.** If it fails, ``phy`` mode falls
  back to ``scripts/python-phy-radio.py`` run as a subprocess.
* **Real-time capacity.** Live runs need dedicated CPU, and the 8-run figure on
  a 32-thread node comes from emulation. Enforce it with cpusets and rely on
  ``load`` mode for sweeps.
* **Hardware.** Several X310s hang, and native trials may trigger some of the
  hangs. Teaching should rely on recorded OTA packs, with live OTA as an extra.
* **Drift.** Notebooks pin a git SHA in their data-pack manifest, and the
  nightly live run flags changes in API or behaviour.
