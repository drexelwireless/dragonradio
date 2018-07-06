#!/usr/bin/env python3
import argparse
import matplotlib as mp
import matplotlib.pyplot as plt
import numpy as np
import scipy.signal as signal
import re
import sys
import time

import drlog

EVENTS = [ [r'^AMC: Moving up modulation scheme', 'AMC', 'g']
         , [r'^AMC: Moving down modulation scheme', 'AMC', 'r']
         , [r'^ARQ: recv from', 'ARQ', 'g']
         , [r'^ARQ: send to', 'ARQ', 'k']
         , [r'^PHY: invalid header', 'PHY', 'r']
         , [r'^PHY: invalid payload', 'PHY', 'y']
         ]

for i in range(0, len(EVENTS)):
    EVENTS[i][0] = re.compile(EVENTS[i][0])

class EventLog(object):
    def __init__(self):
        self.log = drlog.Log()
        self.data = {}
        self.series = []

    @property
    def start(self):
        return min([self.log.nodes[n].start for n in self.log.nodes])

    def loadLog(self, path):
        self.log.load(path)
        for node_id in self.log.nodes:
            if node_id not in self.data:
                self.data[node_id] = {}

    def parseEvents(self, node):
        delta = node.start - self.start

        events = self.log.events[node.node_id]
        events['t'] = events.timestamp + delta
        events['category'] = ''
        events['color'] = ''

        # Parse events
        for (r, k, c) in EVENTS:
            idx = events.event.str.match(r)
            events.category.loc[idx] = k
            events.color.loc[idx] = c

        def ppr(e):
            return e.event

        for k in set([k for (r, k, c) in EVENTS]):
            self.data[node.node_id][k] = (events[events.category == k], ppr)

    def parseSent(self, node):
        delta = node.start - self.start

        sent = self.log.sent[node.node_id]
        sent['t'] = sent.timestamp + delta
        sent['color'] = 'k'

        def ppr(pkt):
            return "Packet(seq={seq}, curhop={curhop}, nexthop={nexthop}, size={size})".\
                format(seq=pkt.seq, pkt=pkt.curhop, curhop=pkt.curhop, nexthop=pkt.nexthop, \
                       size=pkt.size)

        self.data[node.node_id]['sent'] = (sent, ppr)

    def parseReceived(self, node):
        delta = node.start - self.start

        recv = self.log.received[node.node_id]
        recv['t'] = recv.start + delta

        def f(p):
            if not p.header_valid:
                return 'r'
            elif not p.payload_valid:
                return 'y'
            else:
                return 'k'

        recv['color'] = recv.apply(f, axis=1)

        def ppr(pkt):
            return "Packet(seq={seq}, curhop={curhop}, nexthop={nexthop}, ms={ms}, fec0={fec0}, fec1={fec1}, size={size})".\
                format(seq=pkt.seq, pkt=pkt.curhop, curhop=pkt.curhop, nexthop=pkt.nexthop, \
                       ms=pkt.ms, fec0=pkt.fec0, fec1=pkt.fec1, size=pkt.size)

        self.data[node.node_id]['recv'] = (recv, ppr)

    def addSeriesCategory(self, node, k):
        node_id = node.node_id

        (df, ppr) = self.data[node_id][k]

        self.series.append((node.node_id, k, df.t, df.color, df, ppr))

    def plot(self):
        self.fig = plt.figure(figsize=(14,4))
        plt.title('Timeline Plot')
        plt.ylim((-1,len(self.series)))
        plt.yticks(range(len(self.series)), ["Node {}: {}".format(s[0], s[1]) for s in self.series])
        self.ax, = self.fig.axes

        self.lines = []

        for y in range(0, len(self.series)):
            (id, k, x, c, desc, ppr) = self.series[y]

            line = plt.scatter(x, [y]*len(x), color=c.values, alpha=0.85, s=10, label="Node {}: {}".format(id, k))
            line.desc = desc
            line.ppr = ppr
            self.lines.append(line)

        self.annot = self.ax.annotate('',
                                      xy=(4.42,0),
                                      xycoords='data',
                                      xytext=(20,20),
                                      textcoords='offset pixels',
                                      bbox=dict(boxstyle='round', fc='w'),
                                      arrowprops=dict(arrowstyle='->'))
        self.annot.set_visible(False)

        self.fig.canvas.mpl_connect("motion_notify_event", self.hover)

        plt.show()

    def hover(self, event):
        if event.inaxes == self.ax:
            for line in self.lines:
                cont, ind = line.contains(event)
                if cont:
                    self.updateAnnotation(line, ind)
                    self.annot.set_visible(True)
                    self.fig.canvas.draw_idle()
                    return

        if self.annot.get_visible():
            self.annot.set_visible(False)
            self.fig.canvas.draw_idle()

    def updateAnnotation(self, line, ind):
        pos = line.get_offsets()[ind['ind'][0]]
        self.annot.xy = pos
        self.annot.set_text(line.ppr(line.desc.iloc[ind['ind'][0]]))

def main():
    parser = argparse.ArgumentParser(description='Display DragonRadio events.')
    parser.add_argument('-d', '--debug', action='store_true',
                        help='debug')
    parser.add_argument('--send', action='store_true',
                        default=False,
                        help='show sent packets')
    parser.add_argument('--recv', action='store_true',
                        default=False,
                        help='show received packets')
    parser.add_argument('--phy', action='store_true',
                        default=False,
                        help='show PHY events')
    parser.add_argument('--amc', action='store_true',
                        default=False,
                        help='show AMC events')
    parser.add_argument('--arq', action='store_true',
                        default=False,
                        help='show ARQ events')
    parser.add_argument('paths', nargs='*')
    args = parser.parse_args()

    e = EventLog()

    for path in args.paths:
        e.loadLog(path)

    for node_id in e.log.nodes:
        node = e.log.nodes[node_id]
        e.parseEvents(node)

        if args.send:
            e.parseSent(node)

        if args.recv:
            e.parseReceived(node)

    node_ids = sorted(e.log.nodes)

    for node_id in reversed(node_ids):
        node = e.log.nodes[node_id]

        if args.phy:
            e.addSeriesCategory(node, 'PHY')

        if args.arq:
            e.addSeriesCategory(node, 'ARQ')

        if args.amc:
            e.addSeriesCategory(node, 'AMC')

        if args.send:
            e.addSeriesCategory(node, 'sent')

        if args.recv:
            e.addSeriesCategory(node, 'recv')

    e.plot()

if __name__ == '__main__':
    main()
