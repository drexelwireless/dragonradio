#!/usr/bin/env python3
# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
import argparse
import datetime
from functools import wraps
import logging
import sys

import matplotlib as mp
import matplotlib.patches as patches
from matplotlib.widgets import Button, Slider
import matplotlib.pyplot as plt
from matplotlib.transforms import blended_transform_factory

import dragonradio.radio
from dragonradio.radio import decompressIQData
import dragonradio.tools.logging
from dragonradio.tools.plot.radio import ConstellationPlot, PAPRPlot, PSDPlot, SpecgramPlot, WaveformPlot

mp.use('GTK3Agg')

class LogView:
    def __init__(self, logs, viewer):
        self.logs = logs
        """A LogCollection"""

        self.viewer = viewer
        """Responsible LogViewer"""

class ReceiveView(LogView):
    """View of received packets"""
    def __init__(self, logs, viewer, node_id,
                 show_header_invalid=False,
                 nfft=256,
                 sigslop=0):
        super().__init__(logs, viewer)

        self.node_id = node_id
        self.show_header_invalid = show_header_invalid
        self.sigslop = sigslop

        self.pktidx = 0

        self.fig = plt.figure()
        self.specgram = SpecgramPlot(self.fig, self.fig.add_subplot(2,1,1), nfft=nfft)
        self.constellation = ConstellationPlot(self.fig, self.fig.add_subplot(2,4,5))
        self.waveform = WaveformPlot(self.fig, self.fig.add_subplot(2,4,6))
        self.psd = PSDPlot(self.fig, self.fig.add_subplot(2,4,7), nfft=nfft)
        self.papr = PAPRPlot(self.fig, self.fig.add_subplot(2,4,8))

        # Handle close event for figure
        self.fig.canvas.mpl_connect('close_event', self.on_close)

        # Add next and prev buttons. Coordinates are:
        #   posx, posy, width, height
        self.axprev = self.fig.add_axes([0.71, 0.02, 0.1, 0.03])
        self.bprev = Button(self.axprev, 'Previous')
        self.bprev.on_clicked(self.prev_packet)

        self.axnext = self.fig.add_axes([0.82, 0.02, 0.1, 0.03])
        self.bnext = Button(self.axnext, 'Next')
        self.bnext.on_clicked(self.next_packet)

        # Add sent packet button.
        self.axtx = self.fig.add_axes([0.6, 0.02, 0.1, 0.03])
        self.btx = Button(self.axtx, 'Link to TX')
        self.btx.on_clicked(self.link_to_tx)

        # Add packet position slider
        self.axpos = self.fig.add_axes([0.1, 0.02, 0.4, 0.03])
        self.spos = Slider(self.axpos, 'Packet Index',
                           0,
                           len(self.received(self.node_id))-1,
                           valfmt='%1.0f',
                           valinit=0,
                           valstep=1)
        self.spos.on_changed(self.update_slider)

    def received(self, node_id):
        recv = self.logs[node_id].recv
        if self.show_header_invalid:
            return recv
        else:
            return recv[recv.header_valid == True]

    def plot(self, idx):
        recv = self.received(self.node_id)

        if idx >= 0 and idx < len(recv):
            self.pktidx = idx
            self.pkt = recv.iloc[idx]
            self.spos.set_val(idx)

            slots = self.logs.findPacketSlots(self.pkt)
            if slots == None:
                logging.warning("Cannot find slots for packet at timestamp %f", self.pkt.timestamp)
                return

            sig = slots[self.pkt.start_samples-self.sigslop:self.pkt.end_samples+self.sigslop]

            if not self.pkt.header_valid:
                msg = 'INVALID HEADER'
            elif not self.pkt.payload_valid:
                msg = 'INVALID PAYLOAD'
            else:
                msg = ''

            self.fig.canvas.set_window_title('Node {} Received Packets'.format(self.node_id))
            self.fig.suptitle('Packet {} from node {} '
                              '(evm {:03.1f}dB, rssi {:03.1f}dB, fc {:03.1f}MHz) {}'.\
              format(self.pkt.seq, self.pkt.src, self.pkt.evm, self.pkt.rssi, self.pkt.fc/1e6, msg))

            t0 = slots.ts[0]

            self.specgram.plot(slots.fs, slots.iq_data, t0)

            # Mark all packets in the current specgram
            pkts = self.logs[self.node_id].findReceivedPackets(t0, t0+len(slots.iq_data)/slots.fs)
            if not self.show_header_invalid:
                pkts = pkts[pkts.header_valid == True]

            for (_, pkt) in pkts.iterrows():
                self.bracketPacket(pkt, t0, self.specgram.ax)

            # Mark all slots in the current specgram
            for t in slots.ts:
                self.markSlot(self.specgram.ax, t-t0)

            self.constellation.plot(self.pkt.symbols)
            self.waveform.plot(sig, sigslop=self.sigslop)
            self.psd.plot(slots.fs, sig)
            self.papr.plot(sig)

            self.fig.canvas.draw()

    def update_slider(self, val):
        idx = int(val)
        if idx != self.pktidx:
            self.plot(idx)

    def on_close(self, _event):
        if self.viewer:
            self.viewer.closeView(self)

    def next_packet(self, _event):
        self.plot(self.pktidx+1)

    def prev_packet(self, _event):
        self.plot(self.pktidx-1)

    def link_to_tx(self, event):
        if self.viewer:
            idx = self.logs[self.pkt.curhop].findSentPacketIndex(self.pkt.seq)
            if idx is None:
                logging.warning('Could not find transmitted packet')
                return

            view = self.viewer.txView(self.pkt.curhop)
            view.plot(idx)

    def markSlot(self, ax, t, **kwargs):
        ax.axvline(t, color='r')

    def bracketPacket(self, pkt, t0, ax):
        t_start = pkt.start - t0
        t_end = pkt.end - t0

        # If the packet we are marking is the current packet, its label appears
        # in red. Otherwise, its label appears in black.
        if pkt.seq == self.pkt.seq:
            weight = 'bold'
        else:
            weight = 'normal'

        if pkt.payload_valid:
            color = 'k'
        else:
            color = 'r'

        # See:
        #   https://stackoverflow.com/questions/30311809/is-it-possible-to-anchor-a-matplotlib-annotation-to-a-data-coordinate-in-the-x-a
        tform = blended_transform_factory(ax.transData, ax.transAxes)

        ax.annotate('',
                    xy=(t_start, 1),
                    xytext=(t_end, 1),
                    xycoords=tform,
                    arrowprops=dict(arrowstyle='<->',
                                    connectionstyle='bar, armA=20.0, armB=20.0, fraction=0.0',
                                    ec='k'))

        ax.text((t_start + t_end) / 2,
                1.1,
                str(pkt.seq),
                transform=tform,
                ha='center',
                va='bottom',
                weight=weight,
                rotation=45,
                color=color)

    def note(self, ax, msg, t, y, **kwargs):
        ax.annotate(msg,
                    xy=(t, 0),
                    xycoords='data',
                    xytext=(t, y),
                    textcoords='data',
                    arrowprops=dict(arrowstyle='->'),
                    **kwargs)

class SendView(LogView):
    """View of sent packets"""
    def __init__(self, logs, viewer, node_id, nfft=256):
        super().__init__(logs, viewer)

        self.node_id = node_id

        self.pktidx = 0

        self.fig = plt.figure()
        self.constellation = ConstellationPlot(self.fig, self.fig.add_subplot(2,2,1))
        self.waveform = WaveformPlot(self.fig, self.fig.add_subplot(2,2,2))
        self.psd = PSDPlot(self.fig, self.fig.add_subplot(2,2,3), nfft=nfft)
        self.papr = PAPRPlot(self.fig, self.fig.add_subplot(2,2,4))

        # Handle close event for figure
        self.fig.canvas.mpl_connect('close_event', self.on_close)

        # Add next and prev buttons. Coordinates are:
        #   posx, posy, width, height
        self.axprev = self.fig.add_axes([0.71, 0.02, 0.1, 0.03])
        self.bprev = Button(self.axprev, 'Previous')
        self.bprev.on_clicked(self.prev_packet)

        self.axnext = self.fig.add_axes([0.82, 0.02, 0.1, 0.03])
        self.bnext = Button(self.axnext, 'Next')
        self.bnext.on_clicked(self.next_packet)

        # Add received packet button.
        self.axrx = self.fig.add_axes([0.6, 0.02, 0.1, 0.03])
        self.brx = Button(self.axrx, 'Link to RX')
        self.brx.on_clicked(self.link_to_rx)

        # Add packet position slider
        self.axpos = self.fig.add_axes([0.1, 0.02, 0.4, 0.03])
        self.spos = Slider(self.axpos, 'Packet Index',
                           0, len(self.logs[self.node_id].send),
                           valfmt='%1.0f',
                           valinit=0,
                           valstep=1)
        self.spos.on_changed(self.update_slider)

    def plot(self, idx):
        send = self.logs[self.node_id].send

        if idx >= 0 and idx < len(send):
            self.pktidx = idx
            self.pkt = send.iloc[idx]
            self.spos.set_val(idx)

            self.fig.canvas.set_window_title('Node {} Sent Packets'.format(self.node_id))
            self.fig.suptitle('Packet {} to node {}'.format(self.pkt.seq, self.pkt.dest))

            self.constellation.plot(self.pkt.iq_data)
            self.waveform.plot(self.pkt.iq_data)
            self.psd.plot(self.pkt.bw, self.pkt.iq_data)
            self.papr.plot(self.pkt.iq_data)

            self.fig.canvas.draw()

    def update_slider(self, val):
        idx = int(val)
        if idx != self.pktidx:
            self.plot(idx)

    def on_close(self, _event):
        if self.viewer:
            self.viewer.closeView(self)

    def next_packet(self, _event):
        self.plot(self.pktidx+1)

    def prev_packet(self, _event):
        self.plot(self.pktidx-1)

    def link_to_rx(self, event):
        if self.viewer:
            idx = self.logs[self.pkt.nexthop].findReceivedPacketIndex(self.pkt.seq)
            if idx is None:
                logging.warning('Could not find received packet')
                return

            view = self.viewer.rxView(self.pkt.nexthop)
            view.plot(idx)

class SnapshotView(LogView):
    """View of snapshots"""
    def __init__(self, logs, viewer, node_id, nfft=256):
        super().__init__(logs, viewer)

        self.node_id = node_id

        self.snapshotidx = 0

        self.fig = plt.figure()
        self.specgram = SpecgramPlot(self.fig, self.fig.add_subplot(2,1,1), nfft=nfft)
        self.psd = PSDPlot(self.fig, self.fig.add_subplot(2,1,2), nfft=nfft)

        # Add next and prev buttons. Coordinates are:
        #   posx, posy, width, height
        self.axprev = self.fig.add_axes([0.71, 0.02, 0.1, 0.03])
        self.bprev = Button(self.axprev, 'Previous')
        self.bprev.on_clicked(self.prev_snapshot)

        self.axnext = self.fig.add_axes([0.82, 0.02, 0.1, 0.03])
        self.bnext = Button(self.axnext, 'Next')
        self.bnext.on_clicked(self.next_snapshot)

        # Add packet position slider
        self.axpos = self.fig.add_axes([0.1, 0.02, 0.4, 0.03])
        self.spos = Slider(self.axpos, 'Snapshot Index', 0, len(self.snapshots)-1, valfmt='%1.0f', valinit=0, valstep=1)
        self.spos.on_changed(self.update_slider)

    @property
    def snapshots(self):
        return self.logs[self.node_id].snapshots

    def plot(self, idx):
        if idx >= 0 and idx < len(self.snapshots):
            self.snapshotidx = idx

            snapshot = self.snapshots.iloc[idx]
            self.spos.set_val(idx)

            sig = decompressIQData(snapshot.iq_data)

            self.fig.canvas.set_window_title('Snapshot at {}'.format(str(snapshot.timestamp)))

            self.specgram.plot(snapshot.fs, sig, snapshot.timestamp)
            self.psd.plot(snapshot.fs, sig, title=None)

            # Plot self-transmissions
            df = self.logs[self.node_id].selftx
            selftx = df[df.timestamp == snapshot.timestamp]
            fs = snapshot.fs

            for _, e in selftx.iterrows():
                start = e.start/fs
                end = e.end/fs
                f_bot = e.fc-0.5*e.fs
                f_height = e.fs

                if e.is_local:
                    color = 'b'
                else:
                    color = 'r'

                rect = patches.Rectangle((start, f_bot),
                                         end-start,
                                         f_height,
                                         linewidth=0.4,
                                         edgecolor=color,
                                         facecolor=color,
                                         alpha=0.3)
                self.specgram.ax.add_patch(rect)

            self.fig.canvas.draw()

    def update_slider(self, val):
        idx = int(val)
        if idx != self.snapshotidx:
            self.plot(idx)

    def next_snapshot(self, _event):
        self.plot(self.snapshotidx+1)

    def prev_snapshot(self, _event):
        self.plot(self.snapshotidx-1)

def cached_fig(f):
    """Cache figures"""
    @wraps(f)
    def wrapper(self, param, *args, **kwargs):
        fname = f.__name__
        if fname not in self.figures:
            self.figures[fname] = {}

        if param not in self.figures[fname]:
            self.figures[fname][param] = f(self, param, *args, **kwargs)

        return self.figures[fname][param]

    return wrapper

class LogViewer:
    def __init__(self, logs):
        self.logs = logs
        """Collection of DragonRadio logs"""

        self.figures = {}
        """Cached figures"""

    def closeView(self, fig):
        for key in self.figures:
            if fig.node_id in self.figures[key] and self.figures[key][fig.node_id] == fig:
                del self.figures[key]
                return

    @cached_fig
    def rxView(self, node_id, nfft=256, show_header_invalid=False, sigslop=0):
        view = ReceiveView(self.logs, self, node_id,
                           nfft=nfft,
                           show_header_invalid=show_header_invalid,
                           sigslop=sigslop)
        view.fig.show()
        return view

    @cached_fig
    def txView(self, node_id, nfft=256):
        view = SendView(self.logs, self, node_id, nfft=nfft)
        view.fig.show()
        return view

    @cached_fig
    def snapshotView(self, node_id, nfft=256):
        view = SnapshotView(self.logs, self, node_id, nfft=nfft)
        view.fig.show()
        return view

def main():
    parser = argparse.ArgumentParser(description='Show received packets.')
    parser.add_argument('-d', '--debug', action='store_const', const=logging.DEBUG,
                        dest='loglevel',
                        default=logging.WARNING,
                        help='print debugging information')
    parser.add_argument('-v', '--verbose', action='store_const', const=logging.INFO,
                        dest='loglevel',
                        help='be verbose')
    parser.add_argument('--start-time', type=float,
                        default=None,
                        metavar='SEC',
                        help='set start time in seconds since the epoch')
    parser.add_argument('--scenarios',
                        type=str,
                        default=None,
                        metavar='DIR',
                        help='specify directory for scenario files')
    parser.add_argument('--srn-logs', type=str,
                        default=None,
                        metavar='DIR',
                        help='directory where node logs are located')

    parser.add_argument('--tx', action='append', type=int, default=[],
                        dest='tx',
                        metavar='NODE',
                        help='view TX log for given node')
    parser.add_argument('--rx', action='append', type=int, default=[],
                        dest='rx',
                        metavar='NODE',
                        help='view RX log for given node')
    parser.add_argument('--snapshots', action='append', type=int, default=[],
                        dest='snapshots',
                        metavar='NODE',
                        help='view snapshot log for given node')
    parser.add_argument('--nfft', action='store', type=int, default=256,
                        dest='nfft',
                        metavar='N',
                        help='set number of FFT points')
    parser.add_argument('--sigslop', action='store', type=int, default=0,
                        metavar='N',
                        help='set number of extra samples to plot surrounding received packet')
    parser.add_argument('--show-invalid-headers', action='store_true', default=False,
                        dest='show_invalid_headers',
                        help='show invalid headers when displaying RX log')
    parser.add_argument('paths', nargs='*')

    # Parse arguments
    try:
        args = parser.parse_args()
    except SystemExit as ex:
        return ex.code

    # Set up logging
    logging.basicConfig(format='%(asctime)s:%(name)s:%(levelname)s:%(message)s',
                        level=args.loglevel)

    # Determine start time
    if args.start_time:
        start = datetime.datetime.fromtimestamp(args.start_time, UTC)
    else:
        start = None

    # Set path to scenarios
    if args.scenarios is not None:
        dragonradio.tools.colosseum.scoring.scenarios_path = args.scenarios

    # Load logs
    logs = dragonradio.tools.logging.LogCollection(start_time=start)
    logs.load(args.paths)

    viewer = LogViewer(logs)

    for node_id in args.tx:
        if node_id not in logs.nodes:
            print("Cannot find node {}.".format(args.node_id), file=sys.stderr)
        else:
            view = viewer.txView(node_id, nfft=args.nfft)
            view.plot(0)

    for node_id in args.rx:
        if node_id not in logs.nodes:
            print("Cannot find node {}.".format(args.node_id), file=sys.stderr)
        else:
            view = viewer.rxView(node_id,
                                 nfft=args.nfft,
                                 show_header_invalid=args.show_invalid_headers, sigslop=args.sigslop)
            view.plot(0)

    for node_id in args.snapshots:
        if node_id not in logs.nodes:
            print("Cannot find node {}.".format(args.node_id), file=sys.stderr)
        else:
            view = viewer.snapshotView(node_id, nfft=args.nfft)
            view.plot(0)

    plt.show()

if __name__ == '__main__':
    main()
