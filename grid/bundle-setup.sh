#!/bin/bash
# Build the full DragonRadio stack from a bundle made by
# scripts/package-fullstack.py, without network access, into a new private
# workspace (no sudo; nothing installed system-wide; no radio opened).
# Usage: setup.sh WORKSPACE
#   Verifies every file against MANIFEST.json, runs
#   grid/setup-fullstack-build.sh with the bundle's vendored sources, Ubuntu
#   24.04 packages and Python wheels, then grid/build-fullstack.sh (which builds
#   the radio and runs tests/fullstack).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
ws=${1:?usage: setup.sh WORKSPACE}
python3 - "$here" <<'PY'
import hashlib, json, sys
from pathlib import Path
root = Path(sys.argv[1])
m = json.loads((root / 'MANIFEST.json').read_text())
bad = [f for f, h in m['files'].items() if hashlib.sha256((root / f).read_bytes()).hexdigest() != h]
if bad:
    sys.exit(f'hash mismatch: {bad[:5]}')
print(f"bundle {m['bundle']}: {len(m['files'])} files verified; commit {m['commit']}")
PY
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
tar -xzf "$here/source/dragonradio.tar.gz" -C "$scratch" dragonradio/grid/setup-fullstack-build.sh
VENDOR="$here/vendor" bash "$scratch/dragonradio/grid/setup-fullstack-build.sh" \
  "$here/source/dragonradio.tar.gz" "$here/vendor/liquid.tar.gz" "$ws"
cp "$here/MANIFEST.json" "$ws/bundle-manifest.json"
bash "$ws/dragonradio/grid/build-fullstack.sh" "$ws" dragonradio
