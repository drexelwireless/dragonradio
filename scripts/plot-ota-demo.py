#!/usr/bin/env python3
"""Scientific simulation figure for the exact waveforms in an OTA campaign."""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'python/dragonradio'))
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from dragonradio.simulation.ota import MODES, load_wave


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--campaign', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    manifest = json.loads((a.campaign/'manifest.json').read_text())
    summary = {r['mode']:r for r in json.loads((a.campaign/'simulation.json').read_text())}
    colors = {1:'#2563eb', 2:'#ea580c'}
    fig, axes = plt.subplots(2, 2, figsize=(12, 7), constrained_layout=True)
    ax = axes[0,0]
    for j, mode in enumerate(MODES):
        records = manifest['waveforms'][mode]['records'][:6]
        start = records[0]['start']
        for r in records:
            y = j + (-.17 if r['user'] == 1 else .17)
            ax.broken_barh([((r['start']-start)/1000, r['count']/1000)], (y-.12,.24), facecolors=colors[r['user']])
    ax.set(yticks=range(3), yticklabels=['Legacy OFDM (TDMA)', 'Native OFDM (TDMA)', 'Native OFDMA'],
           xlabel='Samples from first packet (thousands)', title='Independent user packets: sequential vs concurrent')
    ax.invert_yaxis()
    from matplotlib.patches import Patch
    ax.legend(handles=[Patch(color=c,label=f'User {u}') for u,c in colors.items()], loc='lower right')
    _, wave = load_wave(a.campaign, 'native-ofdma')
    record = manifest['waveforms']['native-ofdma']['records'][0]
    block = wave[record['start']:record['start']+record['count']].reshape(-1,144)[:,16:]
    power = np.mean(np.abs(np.fft.fftshift(np.fft.fft(block[2:], axis=1), axes=1))**2, axis=0)
    relative = 10*np.log10(np.maximum(power,1e-15)/max(power))
    freq = np.fft.fftshift(np.fft.fftfreq(128,1/manifest['sample_rate']))/1000
    ax = axes[0,1]
    ax.plot(freq,relative,color='#334155',lw=1.2)
    swap = manifest['allocation_swap']
    for user, left, right in ((2 if swap else 1,-52,-5),(1 if swap else 2,5,52)):
        ax.axvspan(left*manifest['sample_rate']/128/1000,right*manifest['sample_rate']/128/1000,
                   alpha=.15,color=colors[user],label=f'User {user}')
    ax.set(xlabel='Frequency relative to center (kHz)', ylabel='Power relative to peak (dB)',
           ylim=(-65,5), title='Composite OFDMA FFT bins (before RF chain)')
    ax.legend()
    ax = axes[1,0]
    ax.bar(range(3),[summary[m]['aggregate_goodput_bps']/1e6 for m in MODES], color=['#64748b','#0d9488','#7c3aed'])
    ax.set(xticks=range(3), xticklabels=['Legacy OFDM','Native OFDM','Native OFDMA'],
           ylabel='Verified aggregate payload (Mbit/s)', title='Simulation goodput; includes traffic gaps')
    ax = axes[1,1]
    for user in (1,2):
        ax.bar(np.arange(3)+(user-1.5)*.3,[summary[m]['users'][str(user)]['pdr'] for m in MODES],
               width=.3,color=colors[user],label=f'User {user}')
    ax.set(xticks=range(3),xticklabels=['Legacy OFDM','Native OFDM','Native OFDMA'],
           ylim=(0,1.08),ylabel='Verified unique packet fraction',title='Separate simulated channels, per receiver')
    ax.legend(loc='lower right')
    fig.suptitle('DragonRadio OTA campaign rehearsal — SIMULATION, no RF measurements',fontsize=14)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    fig.savefig(a.output,dpi=160)
    print(a.output)


if __name__ == '__main__':
    main()
