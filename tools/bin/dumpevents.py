#!/usr/bin/env python3
# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
import argparse
import datetime
import logging

import pandas as pd

from dragonradio.tools.logging import LogCollection
from dragonradio.tools.logging.command_line import Command

def pprSend(row):
    result = f"SEND: curhop={row.curhop:d}; nexthop={row.nexthop:d}; seq={row.seq:d}"
    if row.nretrans > 0:
        result += "; retransmitted"
    elif row.dropped == 'll_drop':
        result += f"; dropped by link-layer"
    elif row.dropped == 'queue_drop':
        result += f"; dropped by network queue"

    return result

def pprRecv(row):
    return f"RECV: curhop={row.curhop:d}; nexthop={row.nexthop:d}; seq={row.seq:d}"

class DumpEventsCommand(Command):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    def add_arguments(self, parser):
        parser.add_argument('--wall-time', action='store_true',
                            default=False,
                            help='show wall time instead of offset from start')
        parser.add_argument('--events', action='store_true',
                            default=False,
                            help='show events')
        parser.add_argument('--send', action='store_true',
                            default=False,
                            help='show sent packets')
        parser.add_argument('--recv', action='store_true',
                            default=False,
                            help='show received packets')

    def handle(self, parser, args):
        node_ids = sorted(self.logs.nodes)

        for node_id in reversed(node_ids):
            frames = []

            if args.events:
                frames.append(pd.DataFrame({ 'timestamp': self.logs[node_id].events.timestamp
                                           , 'event': self.logs[node_id].events.event}))

            if args.send:
                frames.append(pd.DataFrame({ 'timestamp': self.logs[node_id].send.timestamp
                                           , 'event': self.logs[node_id].send.apply(pprSend, axis=1)}))

            if args.recv:
                frames.append(pd.DataFrame({ 'timestamp': self.logs[node_id].recv.timestamp
                                           , 'event': self.logs[node_id].recv.apply(pprRecv, axis=1)}))

            df = pd.concat(frames)

            if not args.wall_time:
                df.timestamp += self.logs[node_id].delta

            df = df.sort_values(by='timestamp')

            for _, (t, event) in df.iterrows():
                print(f'{t:5.4f}: {event}')

if __name__ == '__main__':
    cmd = DumpEventsCommand()
    cmd.run('Dump DragonRadio events')
