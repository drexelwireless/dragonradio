# SDR grid PHY tests

Start with [the complete procedure](../docs/ota-grid.rst). The runner compares
legacy liquidDSP OFDM, native OFDM and simultaneous native OFDMA. It preserves
production OFDM and does not integrate the production MAC/ARQ or default FEC.

- `validate-simulation.sh`: CPU-only build and regression gate on a provisioned Linux image.
- `sdr-config.example.json`: per-radio configuration; fill address, approved frequency, gain and antenna.
- `trial-plan.example.json`: controller plan for three **already reserved** nodes.
- `run-trial.py`: dry plan by default; `--execute` includes RF transmission and requires this experiment's approval.

After both backends are built:

```sh
python scripts/ota-phy.py prepare --campaign results/trial01 --frames 100
python scripts/ota-phy.py simulate --campaign results/trial01
python grid/run-trial.py --plan trial-plan.json --mode native-ofdma
```

Repeat each mode in randomized paired blocks; capture first, analyze afterward.
Keep the manifest and all captures/results for a trial together. Use `prepare
--swap` in a separate campaign for the subband-remapping demonstration. The
controller never allocates/stops reservations or flashes radios; the operator's
outer procedure must own cleanup.

`validate-linux-local.sh SOURCE.tar.gz LIQUID.tar.gz` is an optional CPU-only
Ubuntu x86_64 validation helper. It creates a private `/tmp` workspace, downloads
and unpacks FFTW Debian packages without installing them, creates a venv and
builds both engines. The second archive must contain a standalone Git checkout
at liquidDSP revision `d39f1c0f738c2adca23dec9caa63e7c5bdcebfcb`, with
bootstrap-generated `configure` and `config.h.in`. A Git worktree's `.git` pointer
is not portable. The normal `build-modem.py --fetch` checkout after a successful
local build is suitable; archive its `liquid-src/` directory, excluding compiled
objects/libraries. Keep dependency versions and hashes with the results. This
helper requires network access but neither sudo nor an SDR.

## Native PHY revision benchmark (CPU only)

`run-native-revision.sh SOURCE.tar.gz LIQUID.tar.gz RESULTS_DIR [CORE]` builds
native v2, the frozen v1 engine and the pinned liquid bridge in a private
`/tmp` workspace, runs the regression suite and the liquid CFO triage, then
`scripts/bench-native-revision.py quality` (parallel) and `software` (pinned to
CORE). Run it under tmux so it survives the controlling laptop disconnecting:

```sh
git archive --format=tar.gz --prefix=dragonradio/ -o src.tar.gz HEAD
tmux new-session -d -s drbench "bash run-native-revision.sh src.tar.gz liquid.tar.gz ~/dragonradio-bench/RUN 20 > ~/dragonradio-bench/RUN.log 2>&1"
```

`RESULTS_DIR/STATUS` records progress; `DONE` or `FAILED` marks the end. The
liquid archive must contain the pinned checkout with bootstrap-generated
`configure` **and** `config.h.in` (an archive with only `configure` fails at
`config.status`). See `docs/native-revision.rst` for the study design.

## Full-stack build workspace (no sudo)

`setup-fullstack-build.sh SOURCE.tar.gz LIQUID.tar.gz WORKSPACE` unpacks the
Ubuntu 24.04 development packages the production radio needs into a private
workspace, builds the pinned liquidDSP, xsimd and firpm, and installs the
Python package dependencies into a venv. `build-fullstack.sh WORKSPACE`
builds the `dragonradio` executable and runs `tests/fullstack`. This is a
modernized toolchain (UHD 4.6, Python 3.12); see `docs/native-fullstack.rst`.
No radio is opened by either script.

## Full-stack simulation (P2, CPU only)

`run-sim-fullstack.sh WORKSPACE OUTDIR [--scenario NAME ...]` runs
`scripts/sim-fullstack.py` in a workspace built as above: one `dragonradio`
process per node running the production radio (native OFDM PHY, channelizer,
synthesizer, ARQ controller, queue, TDMA MAC) over a simulated device
(`SimUSRP`) connected to `scripts/sim-air.py`, with synthetic UDP traffic in
place of the tap interface. It needs no privileges and opens no radio. Runs are
wall-clock paced; see `docs/fullstack-simulation.rst`.
