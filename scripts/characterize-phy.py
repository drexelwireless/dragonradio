#!/usr/bin/env python3
"""Theory, channel quality and isolated software characterization (no hardware)."""
import argparse
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

# Set before NumPy import; preserve deliberate operator settings and record them.
for name in ('OPENBLAS_NUM_THREADS','OMP_NUM_THREADS','VECLIB_MAXIMUM_THREADS'):
    os.environ.setdefault(name,'1')
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'python/dragonradio'))
import numpy as np
from dragonradio.simulation.characterization import oracle_study, practical_study, asymmetric_study, software_worker, PROFILES
from dragonradio.simulation.ota import MODES, digest, save_json


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--frames',type=int,default=100)
    p.add_argument('--seeds',type=int,nargs='+',default=[2026,2027,2028])
    p.add_argument('--snrs',type=float,nargs='+',default=[0,4,8,12,16,20,30])
    p.add_argument('--profiles',nargs='+',choices=PROFILES,default=list(PROFILES))
    p.add_argument('--software-only',action='store_true')
    p.add_argument('--reference-stream',action='store_true',help='frozen pre-optimization stream implementation for software measurements')
    p.add_argument('--quality-only',action='store_true')
    p.add_argument('--worker',choices=MODES,help=argparse.SUPPRESS)
    p.add_argument('--software-frames',type=int,default=50)
    p.add_argument('--repetitions',type=int,default=5)
    a=p.parse_args()
    if not 1<=a.frames<=10000 or not 1<=a.software_frames<=500 or a.repetitions<1 or len(set(a.seeds))!=len(a.seeds) or any(s<0 for s in a.seeds):
        p.error('invalid sample counts or seeds')
    if a.software_only and a.quality_only: p.error('choose at most one restriction')
    if a.worker:
        save_json(a.output,software_worker(a.worker,a.software_frames,a.repetitions,a.reference_stream))
        return
    root=Path(__file__).resolve().parents[1]
    report=dict(schema=1,hardware_opened=False,platform=platform.platform(),python=platform.python_version(),
        frames_per_seed=a.frames,seeds=a.seeds,software_frames=a.software_frames,repetitions=a.repetitions,
        numpy=np.__version__,logical_cpu_count=os.cpu_count(),
        script_source_sha256={name:digest(root/name) for name in ('scripts/characterize-phy.py','scripts/compare-software.py','tests/reference_stream_v1.py')},
        build_manifests={name:json.loads((root/'build'/name/'manifest.json').read_text()) for name in ('phy','modem') if (root/'build'/name/'manifest.json').exists()},
        threads={k:os.environ[k] for k in ('OPENBLAS_NUM_THREADS','OMP_NUM_THREADS','VECLIB_MAXIMUM_THREADS')},
        sources={str(f.relative_to(root)):digest(f) for base in ('python/dragonradio/dragonradio/simulation','src/simulation')
                 for f in (root/base).rglob('*') if f.suffix in ('.py','.cc') and f.is_file()},
        libraries={str(f.relative_to(root)):digest(f) for base in ('build/phy','build/modem')
                   for f in (root/base).glob('libdragonradio*') if f.suffix in ('.so','.dylib')},
        oracle=[],practical=[],asymmetric=[],software=[])
    if not a.software_only:
        print('Running independent oracle BER calibration',flush=True)
        report['oracle']=oracle_study(a.frames,a.seeds)
        print('Running practical channel study',flush=True)
        report['practical']=practical_study(a.frames,a.seeds,a.snrs,a.profiles)
        print('Running asymmetric two-user/TDMA controls',flush=True)
        report['asymmetric']=asymmetric_study(a.frames,a.seeds)
        save_json(a.output,report)
    if not a.quality_only:
        for mode in MODES:
            print('Isolated software worker: '+mode,flush=True)
            path=a.output.parent/(a.output.stem+'-'+mode+'-worker.json')
            subprocess.run([sys.executable,__file__,'--worker',mode,'--output',str(path),
                            '--software-frames',str(a.software_frames),'--repetitions',str(a.repetitions)]+(['--reference-stream'] if a.reference_stream else []),check=True)
            report['software'].append(json.loads(path.read_text()))
    report['gates']=dict(oracle_theory_consistent=all(r['theory_consistent_6sigma'] for r in report['oracle']) if report['oracle'] else None,
        no_practical_false_accepts=all(u['false_accepts']==0 for r in report['practical']+report['asymmetric'] for u in r['users'].values()) if report['practical'] else None,
        streams_verified=all(c['all_payloads_verified'] for r in report['software'] for c in r['software']) if report['software'] else None)
    # Empty sections are explicitly labelled rather than counted as passed work.
    report['executed']=dict(theory=bool(report['oracle']),quality=bool(report['practical']),software=bool(report['software']))
    save_json(a.output,report)
    print(json.dumps(dict(output=str(a.output),gates=report['gates'],executed=report['executed']),indent=2))
    if any(value is False for value in report['gates'].values()): raise SystemExit(1)


if __name__=='__main__':
    main()
