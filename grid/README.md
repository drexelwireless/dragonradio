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
