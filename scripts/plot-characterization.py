#!/usr/bin/env python3
"""Plot calibrated BER, practical PER and paired software measurements."""
import argparse
import json
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--study',type=Path,required=True)
    p.add_argument('--software-comparison',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    study=json.loads(a.study.read_text())
    software=json.loads(a.software_comparison.read_text())
    names={'legacy-ofdm':'Legacy OFDM','native-ofdm':'Native OFDM','native-ofdma':'Native OFDMA'}
    colors={'legacy-ofdm':'#64748b','native-ofdm':'#0d9488','native-ofdma':'#7c3aed'}
    fig,axes=plt.subplots(2,2,figsize=(12,7.5),constrained_layout=True)
    ax=axes[0,0]
    rows=[r for r in study['oracle'] if r['mode']=='native-ofdm']
    ax.semilogy([r['ebno_db'] for r in rows],[r['theoretical_ber'] for r in rows],color='black',label='Coherent Gray QPSK theory')
    for mode,marker in (('native-ofdm','o'),('native-ofdma','x')):
        rows=[r for r in study['oracle'] if r['mode']==mode]
        ax.semilogy([r['ebno_db'] for r in rows],[r['ber'] if r['ber'] else r['ber_wilson95'][1] for r in rows],
            marker=marker,linestyle='none',color=colors[mode],label=names[mode]+' oracle')
    ax.set(xlabel='Data-bit Eb/N0 (dB)',ylabel='Bit error rate',title='Known timing/channel: FFT/noise calibration')
    ax.legend(fontsize=8)
    ax.grid(alpha=.2,which='both')
    ax=axes[0,1]
    for mode in names:
        rows=[r for r in study['practical'] if r['profile']=='awgn' and r['payload_bytes']==256 and r['mode']==mode]
        ax.plot([r['reference_snr_db'] for r in rows],[r['users']['1']['per'] for r in rows],marker='o',color=colors[mode],label=names[mode]+(' user 1' if mode=='native-ofdma' else ''))
    ax.set(xlabel='TX-reference time-sample SNR (dB)',ylabel='Packet error rate',ylim=(-.03,1.03),title='Practical receivers: 256-byte payload, uncoded')
    ax.legend(fontsize=8)
    ax.grid(alpha=.2)
    ax=axes[1,0]
    for i,mode in enumerate(names):
        row=next(r for r in study['asymmetric'] if r['mode']==mode and r['link_snr_db']==[30,10])
        for user,c in ((1,'#2563eb'),(2,'#ea580c')):
            ax.bar(i+(user-1.5)*.32,row['users'][str(user)]['payload_goodput_bps']/1e3,width=.32,color=c,label=f'User {user}: '+('30 dB' if user==1 else '10 dB') if i==0 else None)
    ax.set(xticks=range(3),xticklabels=list(names.values()),ylabel='Verified user payload (kbit/s)',title='Strong/weak links: fixed noise, equal total TX RMS')
    ax.legend(fontsize=8)
    ax=axes[1,1]
    xpos=0
    labels=[]
    for row in software['results']:
        if row['mode']=='legacy-ofdm': continue
        for index,title in ((2,'4k chunks'),(4,'whole capture')):
            c=row['cells'][index]
            ax.bar(xpos-.16,c['before_median_us']/1000,width=.32,color='#94a3b8',label='Before' if xpos==0 else None)
            ax.bar(xpos+.16,c['after_median_us']/1000,width=.32,color='#0d9488',label='After' if xpos==0 else None)
            labels.append(names[row['mode']]+'\n'+title)
            xpos+=1
    ax.set(xticks=range(4),xticklabels=labels,ylabel='Two-receiver replay wall time (ms)',title='Median of paired blocks: remove repeated buffer copies')
    ax.legend(fontsize=8)
    fig.suptitle('DragonRadio simulation characterization — no RF or full-stack claims',fontsize=14)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    fig.savefig(a.output,dpi=160)
    print(a.output)


if __name__=='__main__': main()
