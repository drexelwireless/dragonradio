"""Modulate or demodulate packets with the native or liquid OFDM PHY, passing
arrays through .npz files: the subprocess fallback for phy-mode notebooks
whose kernel cannot import dragonradiocpp.

Needs the dragonradio executable built with -DDRAGONRADIO_NOTEBOOK=ON (the
native PHY's demodulator has no other Python entry point). Run it as
    NBVENV/bin/dragonradio-python scripts/phy-npz.py modulate --phy native IN OUT

modulate:   IN has 'payloads' (uint8, packets x bytes) and optional 'seqs';
            OUT gets 'iq' (complex64, all frames back to back) and 'offsets'
            (frame k is iq[offsets[k]:offsets[k+1]]).
demodulate: IN has 'iq' and optional 'offsets' (each slice is demodulated
            separately); OUT gets one row per decoded header: 'buffer',
            'seq', 'payload_valid', 'evm', 'rssi', 'cfo', 'start', 'end', and
            'data' (uint8, rows zero-padded to the longest) with 'data_len'.
"""
import argparse
import sys

import numpy as np

import dragonradio.radio as radio
import dragonradio.liquid as liquid
from dragonradio.liquid import MCS, CRCScheme, FECScheme, ModulationScheme
from dragonradiocpp import notebook


def mkPHY(name, M=48, cp_len=6, taper_len=4):
    """The PHYs with the radio's defaults; payload CRC32, RS8, QPSK."""
    payload_mcs = MCS(CRCScheme.crc32, FECScheme.rs8, FECScheme.none, ModulationScheme.qpsk)
    if name == 'native':
        return radio.NativeOFDM([(payload_mcs, radio.AutoGain())], M, cp_len, None, 4096, 4, True, 'v29')
    header_mcs = MCS(CRCScheme.crc32, FECScheme.none, FECScheme.v29p78, ModulationScheme.bpsk)
    return liquid.OFDM(header_mcs, [(payload_mcs, radio.AutoGain())], True, True, M, cp_len, taper_len, None)


def modulate(phy, inp, output):
    payloads = inp['payloads']
    seqs = inp['seqs'] if 'seqs' in inp else np.arange(len(payloads))
    frames = [notebook.modulate(phy, p.tobytes(), seq=int(s) % 65536) for p, s in zip(payloads, seqs)]
    offsets = np.concatenate([[0], np.cumsum([len(f) for f in frames])])
    np.savez(output, iq=np.concatenate(frames).astype(np.complex64), offsets=offsets)


def demodulate(phy, inp, output, node_id):
    iq = inp['iq']
    offsets = inp['offsets'] if 'offsets' in inp else np.array([0, len(iq)])
    rows = []
    for b in range(len(offsets) - 1):
        for d in notebook.demodulate(phy, iq[offsets[b]:offsets[b + 1]], node_id=node_id):
            rows.append((b, d))
    data_len = np.array([len(d['data']) for _, d in rows], np.int64)
    data = np.zeros((len(rows), data_len.max(initial=0)), np.uint8)
    for i, (_, d) in enumerate(rows):
        data[i, :data_len[i]] = np.frombuffer(d['data'], np.uint8)

    def col(key, dtype):
        return np.array([d[key] for _, d in rows], dtype)

    np.savez(output, buffer=np.array([b for b, _ in rows], np.int64), seq=col('seq', np.int64),
             payload_valid=col('payload_valid', bool), evm=col('evm', np.float32),
             rssi=col('rssi', np.float32), cfo=col('cfo', np.float32),
             start=col('start', np.int64), end=col('end', np.int64), data=data, data_len=data_len)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('op', choices=['modulate', 'demodulate'])
    parser.add_argument('--phy', choices=['native', 'liquid'], required=True)
    parser.add_argument('--node-id', type=int, default=2, help='receiving node (demodulate)')
    parser.add_argument('input')
    parser.add_argument('output')
    args = parser.parse_args()

    phy = mkPHY(args.phy)
    inp = np.load(args.input)
    if args.op == 'modulate':
        modulate(phy, inp, args.output)
    else:
        demodulate(phy, inp, args.output, args.node_id)
    return 0


if __name__ == '__main__':
    sys.exit(main())
