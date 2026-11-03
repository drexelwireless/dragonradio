#!/usr/bin/env python3
"""Alternate isolated baseline/cursor-stream measurements without editing source."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import statistics


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--blocks',type=int,default=3)
    p.add_argument('--frames',type=int,default=100)
    p.add_argument('--repetitions',type=int,default=7)
    a=p.parse_args()
    if a.blocks<3 or not 1<=a.frames<=500 or a.repetitions<3: p.error('need >=3 blocks/repetitions and frames 1..500')
    a.output.parent.mkdir(parents=True,exist_ok=True)
    reports={'before':[],'after':[]}
    orders=[]
    root=Path(__file__).resolve().parents[1]
    for block in range(a.blocks):
        order=('before','after') if block%2==0 else ('after','before')
        orders.append(order)
        for version in order:
            path=a.output.parent/f'{a.output.stem}-{version}-{block}.json'
            command=[sys.executable,str(root/'scripts/characterize-phy.py'),'--software-only','--software-frames',str(a.frames),
                     '--repetitions',str(a.repetitions),'--output',str(path)]
            if version=='before': command+=['--reference-stream']
            subprocess.run(command,check=True)
            reports[version].append(json.loads(path.read_text()))
    summary=[]
    for i,mode in enumerate(('legacy-ofdm','native-ofdm','native-ofdma')):
        cells=[]
        for k in range(5):
            before=[r['software'][i]['software'][k]['wall']['p50_us'] for r in reports['before']]
            after=[r['software'][i]['software'][k]['wall']['p50_us'] for r in reports['after']]
            for b,c in zip(reports['before'],reports['after']):
                if b['software'][i]['input_sha256']!=c['software'][i]['input_sha256']:
                    raise RuntimeError('unmatched replay input')
            cells.append(dict(chunk_samples=reports['after'][0]['software'][i]['software'][k]['chunk_samples'],
                before_median_us=statistics.median(before),after_median_us=statistics.median(after),
                paired_median_speedup=statistics.median(b/c for b,c in zip(before,after)),
                paired_speedups=[b/c for b,c in zip(before,after)]))
        summary.append(dict(mode=mode,cells=cells))
    a.output.write_text(json.dumps(dict(blocks=a.blocks,repetitions_per_block=a.repetitions,frames_per_user=a.frames,
        order=orders,results=summary,input_identical=True,
        caveat='median paired block speedup; few blocks, not a claim of universal dominance'),indent=2)+'\n')
    print(a.output)


if __name__=='__main__': main()
