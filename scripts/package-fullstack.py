#!/usr/bin/env python3
"""Package the complete DragonRadio source tree and its pinned dependencies
for building the full stack on a radio host (P4 item 2.3).

scripts/package-grid.py packages only the PHY engine. This packages:

  dragonradio-fullstack-COMMIT/
    source/dragonradio.tar.gz    git archive of COMMIT (prefix dragonradio/) + COMMIT file
    vendor/liquid.tar.gz         pinned liquidDSP checkout with generated configure
    vendor/xsimd.tar.gz, firpm.tar.gz, libcorrect.tar.gz   sources at the submodule pins
    vendor/debs/*.deb            Ubuntu 24.04 amd64 development packages (optional)
    vendor/wheels/*.whl          Python packages at the workspace's versions (optional)
    setup.sh                     verify hashes, build a private workspace offline
    MANIFEST.json                SHA-256 of every file, pins, versions, limits

The bundle is sources and Ubuntu 24.04 x86_64 packages; nothing compiled is
copied between hosts. setup.sh runs grid/setup-fullstack-build.sh with
VENDOR=vendor (no network) and then grid/build-fullstack.sh. Opening a radio
is never part of it.

Usage:
  package-fullstack.py --workspace /tmp/dragonradio-fullstack [--commit HEAD]
      [--output-dir build] [--no-debs] [--no-wheels]
"""
import argparse
import hashlib
import io
import json
import os
import subprocess
import sys
import tarfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SUBMODULES = ['dependencies/liquid-dsp', 'dependencies/uhd', 'dependencies/pybind11', 'dependencies/libcorrect',
              'dependencies/xsimd', 'dependencies/firpm']


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, capture_output=True, text=True, **kw).stdout


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def add_bytes(tar, name, data, mode=None):
    mode = mode if mode is not None else (0o755 if name.endswith('/configure') else 0o644)
    info = tarfile.TarInfo(name)
    info.size = len(data)
    info.mode = mode
    info.mtime = int(time.time())
    tar.addfile(info, io.BytesIO(data))


def git_archive(repo, rev, prefix, out, extra=None):
    """git archive REV of REPO into OUT (.tar.gz) under PREFIX/, plus extra files."""
    raw = subprocess.run(['git', '-C', str(repo), 'archive', '--format=tar', f'--prefix={prefix}/', rev],
                         check=True, capture_output=True).stdout
    with tarfile.open(fileobj=io.BytesIO(raw)) as src, tarfile.open(out, 'w:gz') as dst:
        for m in src.getmembers():
            dst.addfile(m, src.extractfile(m) if m.isfile() else None)
        for name, data in (extra or {}).items():
            add_bytes(dst, f'{prefix}/{name}', data)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--workspace', required=True, help='build workspace from setup-fullstack-build.sh (vendored dependencies)')
    ap.add_argument('--commit', default='HEAD')
    ap.add_argument('--output-dir', default=str(ROOT / 'build'))
    ap.add_argument('--no-debs', action='store_true', help='omit Ubuntu packages (target installs them)')
    ap.add_argument('--no-wheels', action='store_true', help='omit Python wheels (target has network for pip)')
    args = ap.parse_args()
    ws = Path(args.workspace).resolve()
    commit = run(['git', '-C', str(ROOT), 'rev-parse', args.commit]).strip()
    short = commit[:8]
    dirty = bool(run(['git', '-C', str(ROOT), 'status', '--porcelain', '--untracked-files=no']).strip())
    name = f'dragonradio-fullstack-{short}'
    out = Path(args.output_dir) / name
    if out.exists():
        sys.exit(f'{out} exists')
    (out / 'source').mkdir(parents=True)
    (out / 'vendor').mkdir()

    # Source tree at COMMIT (committed content only; a dirty tree is recorded, not packaged).
    git_archive(ROOT, commit, 'dragonradio', out / 'source' / 'dragonradio.tar.gz',
                extra={'COMMIT': (commit + '\n').encode()})

    # Submodule pins from the commit's tree (gitlinks), and the workspace's checkouts.
    pins = {}
    for line in run(['git', '-C', str(ROOT), 'ls-tree', commit] + SUBMODULES).splitlines():
        mode, kind, sha, path = line.split(None, 3)
        if kind == 'commit':
            pins[path] = sha
    vendored = {}
    for dep, src in (('xsimd', 'src-xsimd'), ('firpm', 'src-firpm'), ('libcorrect', 'src-libcorrect')):
        rev = run(['git', '-C', str(ws / src), 'rev-parse', 'HEAD']).strip()
        pin = pins.get(f'dependencies/{dep}')
        if pin and pin != rev:
            sys.exit(f'{dep}: workspace checkout {rev} differs from the submodule pin {pin}')
        git_archive(ws / src, rev, dep, out / 'vendor' / f'{dep}.tar.gz')
        vendored[dep] = rev
    # liquidDSP: tracked files plus the bootstrap outputs configure needs.
    liquid = ws / 'liquid-src'
    rev = run(['git', '-C', str(liquid), 'rev-parse', 'HEAD']).strip()
    if pins.get('dependencies/liquid-dsp') and pins['dependencies/liquid-dsp'] != rev:
        sys.exit(f'liquid: workspace {rev} differs from the submodule pin {pins["dependencies/liquid-dsp"]}')
    generated = {g: (liquid / g).read_bytes() for g in ('configure', 'config.h.in', 'aclocal.m4') if (liquid / g).exists()}
    if 'configure' not in generated or 'config.h.in' not in generated:
        sys.exit('liquid checkout lacks bootstrap-generated configure/config.h.in')
    git_archive(liquid, rev, 'liquid', out / 'vendor' / 'liquid.tar.gz', extra=generated)
    vendored['liquid-dsp'] = rev

    debs = []
    if not args.no_debs:
        (out / 'vendor' / 'debs').mkdir()
        for deb in sorted((ws / 'packages').glob('*.deb')):
            os.link(deb, out / 'vendor' / 'debs' / deb.name) if deb.stat().st_dev == out.stat().st_dev \
                else (out / 'vendor' / 'debs' / deb.name).write_bytes(deb.read_bytes())
            debs.append(deb.name)
    freeze = run([str(ws / 'venv' / 'bin' / 'python'), '-m', 'pip', 'freeze']).split()
    wheels = []
    if not args.no_wheels:
        (out / 'vendor' / 'wheels').mkdir()
        req = out / 'vendor' / 'wheels' / 'requirements.txt'
        req.write_text('\n'.join(freeze) + '\n')
        subprocess.run([str(ws / 'venv' / 'bin' / 'python'), '-m', 'pip', 'download', '-q', '--only-binary=:all:',
                        '-d', str(out / 'vendor' / 'wheels'), '-r', str(req)], check=True)
        wheels = sorted(p.name for p in (out / 'vendor' / 'wheels').glob('*.whl'))

    setup = (ROOT / 'grid' / 'bundle-setup.sh').read_text()
    (out / 'setup.sh').write_text(setup)
    (out / 'setup.sh').chmod(0o755)

    build_manifest = (ws / 'build-manifest.txt').read_text().splitlines() if (ws / 'build-manifest.txt').exists() else []
    files = {str(p.relative_to(out)): sha256(p) for p in sorted(out.rglob('*')) if p.is_file()}
    manifest = {
        'bundle': name, 'created': time.strftime('%Y-%m-%dT%H:%M:%S%z'), 'host': os.uname().nodename,
        'commit': commit, 'working_tree_dirty_at_packaging': dirty,
        'submodule_pins': pins, 'vendored_sources': vendored,
        'pybind11': next((l.split('==')[1] for l in freeze if l.lower().startswith('pybind11==')), None),
        'python_packages': freeze, 'debs': debs, 'wheels': wheels,
        'workspace_build_manifest': build_manifest,
        'target': 'Ubuntu 24.04 x86_64 (debs and wheels); other images need their own packages',
        'uhd': next((l for l in build_manifest if l.startswith('uhd ')), 'uhd (unknown)'),
        'notes': [
            'Modernized toolchain: UHD 4.6, Boost 1.83, Python 3.12, pybind11 2.13.6. The historical '
            'DragonRadio image used UHD 3.9 and Python 3.8; see docs/fullstack-packaging.rst.',
            'Sources only: compile on the target. No radio is opened by setup.sh.',
            'The submodule pin for pybind11 (787d2c8) predates Python 3.11; pip pybind11 is used instead.',
            'The UHD submodule pin is recorded but not built: the workspace uses the distribution libuhd.'],
        'files': files,
        'hardware_opened': False,
    }
    (out / 'MANIFEST.json').write_text(json.dumps(manifest, indent=1) + '\n')
    archive = out.with_suffix('.tar')
    subprocess.run(['tar', '-C', str(out.parent), '-cf', str(archive), name], check=True)
    digest = sha256(archive)
    (archive.parent / (archive.name + '.sha256')).write_text(f'{digest}  {archive.name}\n')
    print(f'{archive}\nsha256 {digest}\n{len(files)} files, {len(debs)} debs, {len(wheels)} wheels; commit {commit}'
          + (' (working tree was dirty; committed content packaged)' if dirty else ''))


if __name__ == '__main__':
    raise SystemExit(main())
