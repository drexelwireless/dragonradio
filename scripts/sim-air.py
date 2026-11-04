#!/usr/bin/env python3
"""Air relay for full-stack simulation (phase P2).

Every simulated node (``--sim-air SOCKET``) connects here. Each timestamped TX
burst a node sends is relayed unchanged to every other node; receivers apply
their own link model and noise. The relay records, per burst, the transmitting
node, its air time and how far ahead of that air time it arrived, which is the
ground truth for TX timing. It never alters or drops samples except when a
receiver's socket backlog exceeds --max-backlog (counted and reported).
"""
import argparse
import asyncio
import json
import os
import signal
import struct
import time

HEADER = struct.Struct('<IHBBqddIII')   # must match sim::AirHeader (44 bytes)
MAGIC = 0x41535244
VERSION = 2
HELLO = 1
BURST = 2


class Air:
    def __init__(self, max_backlog, burst_log):
        self.max_backlog = max_backlog
        self.clients = {}
        self.nodes = {}
        self.burst_log = burst_log

    def node_stats(self, node):
        return self.nodes.setdefault(node, {'bursts': 0, 'samples': 0, 'backlog_drops': 0,
                                            'lead_min': None, 'lead_sum': 0.0, 'late': 0})

    async def handle(self, reader, writer):
        node = None
        try:
            while True:
                hdr = await reader.readexactly(HEADER.size)
                magic, version, kind, src, start, fs, fc, n, burst, group = HEADER.unpack(hdr)
                if magic != MAGIC or version != VERSION:
                    break
                payload = await reader.readexactly(8*n) if n else b''
                if kind == HELLO:
                    node = src
                    self.clients[writer] = src
                    self.node_stats(src)
                    continue
                if kind != BURST:
                    continue
                now = time.monotonic_ns()*1e-9
                lead = start/fs - now
                st = self.node_stats(src)
                st['bursts'] += 1
                st['samples'] += n
                st['lead_sum'] += lead
                st['lead_min'] = lead if st['lead_min'] is None else min(st['lead_min'], lead)
                if lead < 0:
                    st['late'] += 1
                if self.burst_log is not None:
                    self.burst_log.write(f'{src},{burst},{group},{start},{n},{fs:.17g},{fc:.17g},{lead:.6f}\n')
                for w, dst in list(self.clients.items()):
                    if w is writer:
                        continue
                    if w.transport.get_write_buffer_size() > self.max_backlog:
                        self.node_stats(dst)['backlog_drops'] += 1
                        continue
                    w.write(hdr)
                    w.write(payload)
        except (asyncio.IncompleteReadError, ConnectionResetError, BrokenPipeError):
            pass
        finally:
            self.clients.pop(writer, None)
            writer.close()

    def summary(self):
        out = {}
        for node, st in sorted(self.nodes.items()):
            d = dict(st)
            d['lead_mean'] = st['lead_sum']/st['bursts'] if st['bursts'] else None
            del d['lead_sum']
            out[str(node)] = d
        return out


async def main(args):
    if os.path.exists(args.socket):
        os.unlink(args.socket)
    burst_log = open(args.burst_log, 'w') if args.burst_log else None
    if burst_log:
        burst_log.write('node,burst,group,start,nsamples,fs,fc,lead\n')
    air = Air(args.max_backlog, burst_log)
    server = await asyncio.start_unix_server(air.handle, path=args.socket)
    stop = asyncio.Event()
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, stop.set)
    print(f'air relay listening on {args.socket}', flush=True)
    async with server:
        await stop.wait()
    if burst_log:
        burst_log.close()
    if args.stats:
        with open(args.stats, 'w') as f:
            json.dump(air.summary(), f, indent=1)
    os.unlink(args.socket)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('socket', help='Unix socket path to listen on')
    parser.add_argument('--stats', help='write per-node relay statistics (JSON) on exit')
    parser.add_argument('--burst-log', help='write one CSV line per relayed burst')
    parser.add_argument('--max-backlog', type=int, default=256 << 20,
                        help='per-receiver socket backlog (bytes) before bursts are dropped')
    asyncio.run(main(parser.parse_args()))
