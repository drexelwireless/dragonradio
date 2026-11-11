#!/usr/bin/env python3
# Copyright 2018-2023 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
from functools import wraps
import logging
import sys
from typing import Any, Dict

import matplotlib as mp
from matplotlib.figure import Figure
from matplotlib.widgets import Button, Slider
import matplotlib.pyplot as plt

from dragonradio.signal import decompressIQData
from dragonradio.tools.logging import LogCollection, Packet
from dragonradio.tools.logging.command_line import Command, add_annotate_args
from .plot import ConstellationPlot, PAPRPlot, PSDPlot, SpecgramPlot, WaveformPlot

def pprSentPacket(pkt: Packet):
    """Pretty print a sent packet"""
    return f"seq={pkt.seq:}\n" \
           f"curhop={pkt.curhop:}\n" \
           f"nexthop={pkt.nexthop:}\n" \
           f"size={pkt.size:}\n" \
           f"data_len={pkt.data_len:}\n" \
           f"ms={pkt.ms:}"

def pprReceivedPacket(logs: LogCollection, pkt: Packet):
    """Pretty print a received packet"""
    pkt_sent = logs[pkt.curhop].findSentPacket(pkt)
    if pkt_sent is not None:
        t_sent = pkt_sent.aligned_timestamp
        t_recv = pkt.aligned_timestamp
        delta = t_recv - t_sent
        delta_s = f"{delta:g}"
    else:
        delta_s = ''

    return f"seq={pkt.seq:}\n" \
           f"curhop={pkt.curhop:}\n" \
           f"nexthop={pkt.nexthop:}\n" \
           f"size={pkt.size:}\n" \
           f"data_len={pkt.data_len:}\n" \
           f"ms={pkt.ms:}\n" \
           f"evm={pkt.evm:2.0f}\n" \
           f"rssi={pkt.rssi:2.0f}\n" \
           f"clock delta={delta_s:}"

class LogView:
    logs: LogCollection
    """A LogCollection"""

    viewer: 'LogViewer'
    """Responsible LogViewer"""

    node_id: int
    "ID of node whose log is being viewed"

    fig: Figure
    """Figure representing view"""

    def __init__(self, logs: LogCollection, viewer: 'LogViewer', node_id: int):
        self.logs = logs
        self.viewer = viewer
        self.node_id = node_id

    def set_window_title(self, title: str):
        """Set window title.

        Args:
            title (str): Window title.
        """
        if self.fig.canvas.manager is not None:
            self.fig.canvas.manager.set_window_title(title)

class ReceiveView(LogView):
    """View of received packets"""
    plot_recv: bool = True
    """Should received packets be plotted?"""

    plot_sent: bool = True
    """Should sent packets be plotted?"""

    plot_rx_slot_boundaries: bool = False
    """Should received slot boundaries be plotted?"""

    plot_slot_boundaries: bool = False
    """Should slot boundaries be plotted?"""

    plot_tx_records: bool = True
    """Should TX records be plotted?"""

    plot_events: bool = True
    """Should MAC/USRP events be plotted?"""

    def __init__(self,
                 logs: LogCollection,
                 viewer: 'LogViewer',
                 node_id: int,
                 show_header_invalid: bool=False,
                 nfft: int=256,
                 sigslop: int =0,
                 annotate: bool=False,
                 sticky: bool=False):
        super().__init__(logs, viewer, node_id)

        self.show_header_invalid = show_header_invalid
        self.sigslop = sigslop

        self.pktidx = 0

        self.fig = plt.figure()
        self.specgram = SpecgramPlot(self.fig, self.fig.add_subplot(2,1,1),
                                     nfft=nfft,
                                     annotate=annotate,
                                     sticky=sticky)
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

        self.specgram.pick_artist = self.pick_artist

    def received(self, node_id: int):
        recv = self.logs[node_id].recv
        if self.show_header_invalid:
            return recv
        else:
            return recv[recv.header_valid == True]

    def plot(self, idx: int):
        recv = self.received(self.node_id)

        if idx >= 0 and idx < len(recv):
            self.pktidx = idx
            self.pkt = recv.iloc[idx]
            self.spos.set_val(idx)

            slots = self.logs.findPacketSlots(self.pkt, self.node_id)
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

            self.set_window_title('Node {} Received Packets'.format(self.node_id))
            self.fig.suptitle('Packet {} from node {} '
                              '(evm {:03.1f}dB, rssi {:03.1f}dB, fc {:03.1f}MHz) {}'.\
              format(self.pkt.seq, self.pkt.src, self.pkt.evm, self.pkt.rssi, self.pkt.fc/1e6, msg))

            if not slots.empty:
                t0 = slots.t_start

                self.specgram.plot(slots.fs, slots.iq_data, t0)

                # Plot received packet voxels
                if self.plot_recv:
                    pkts = self.logs[self.node_id].findReceivedPackets(slots.t_start, slots.t_end)
                    if not self.show_header_invalid:
                        pkts = pkts[pkts.header_valid == True]

                    for idx, pkt in pkts.iterrows():
                        # Plot voxel occupied by packet
                        if 'chan_bw' in pkt:
                            pkt.idx = idx

                            if pkt.seq == self.pkt.seq and pkt.fc == self.pkt.fc:
                                alpha = 0.9
                            else:
                                alpha = 0.3

                            if pkt.payload_valid:
                                color = 'k'
                            else:
                                color = 'r'

                            self.specgram.plotPacketVoxel(pkt, t0,
                                                          linewidth=1,
                                                          edgecolor=color,
                                                          facecolor='none',
                                                          alpha=alpha,
                                                          ppr=lambda pkt: pprReceivedPacket(self.logs, pkt))

                        # Display packet bracket at top of plot
                        if pkt.seq == self.pkt.seq:
                            weight = 'bold'
                        else:
                            weight = 'normal'

                        self.specgram.bracketPacket(pkt, t0, weight=weight)

                # Plot sent packet voxels
                if self.plot_sent:
                    pkts = self.logs[self.node_id].findSentPackets(slots.t_start, slots.t_end)
                    for idx, pkt in pkts.iterrows():
                        pkt.idx = idx

                        self.specgram.plotPacketVoxel(pkt, t0,
                                                      linewidth=1,
                                                      edgecolor='k',
                                                      facecolor='none',
                                                      alpha=0.3,
                                                      ppr=pprSentPacket)

                # Mark all received slots in the current specgram
                if self.plot_rx_slot_boundaries:
                    for _, slot in slots.slots.iterrows():
                        self.specgram.markSlot(slot.start-t0)

                # Mark all slots in the current specgram
                if self.plot_slot_boundaries:
                    offset = self.logs.start.timestamp()
                    slot_size = self.logs[self.node_id].config['slot_size']
                    t_slot = slots.t_start + slot_size - (slots.t_start + offset) % slot_size

                    while t_slot < slots.t_end:
                        self.specgram.markSlot(t_slot-t0, color='b')
                        t_slot += slot_size

                # Display TX records
                if self.plot_tx_records:
                    for node_id in self.logs.nodes:
                        if node_id == self.node_id:
                            continue

                        tx_records = self.logs[node_id].findTXRecords(slots.t_start, slots.t_end)
                        for _, tx in tx_records.iterrows():
                            self.specgram.plotTXRecord(t0, tx)

                # Plot MAC events
                if self.plot_events:
                    for node_id in self.logs.nodes:
                        if node_id == self.node_id:
                            continue

                        self.specgram.plotEvents(self.logs[node_id],
                                                 'USRP',
                                                 t0,
                                                 slots.t_start,
                                                 slots.t_end)

                        self.specgram.plotEvents(self.logs[node_id],
                                                 'MAC',
                                                 t0,
                                                 slots.t_start,
                                                 slots.t_end)

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
            idx = self.logs[self.pkt.curhop].findSentPacketIndex(self.pkt)
            if idx is None:
                logging.warning('Could not find transmitted packet')
                return

            view = self.viewer.txView(self.pkt.curhop)
            view.plot(idx)

    def pick_artist(self, artist):
        if hasattr(artist, 'df'):
            item = artist.df

            if hasattr(item, 'idx') and item.nexthop == self.node_id:
                self.plot(item.idx)

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
    def __init__(self,
                 logs: LogCollection,
                 viewer: 'LogViewer',
                 node_id: int,
                 nfft: int=256):
        super().__init__(logs, viewer, node_id)

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

            self.set_window_title('Node {} Sent Packets'.format(self.node_id))
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
            idx = self.logs[self.pkt.nexthop].findReceivedPacketIndex(self.pkt)
            if idx is None:
                logging.warning('Could not find received packet')
                return

            view = self.viewer.rxView(self.pkt.nexthop)
            view.plot(idx)

class SnapshotView(LogView):
    """View of snapshots"""
    def __init__(self,
                 logs: LogCollection,
                 viewer: 'LogViewer',
                 node_id: int,
                 nfft: int=256):
        super().__init__(logs, viewer, node_id)

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

            self.set_window_title('Snapshot at {}'.format(str(snapshot.timestamp)))
            self.specgram.plot(snapshot.fs, sig, snapshot.timestamp)
            self.psd.plot(snapshot.fs, sig, title=None)

            # Plot self-transmissions
            df = self.logs[self.node_id].selftx
            selftx = df[df.timestamp == snapshot.timestamp]
            fs = snapshot.fs

            for _, e in selftx.iterrows():
                if e.is_local:
                    color = 'b'
                else:
                    color = 'r'

                self.specgram.plotVoxel(e.start/fs, e.end/fs, e.fc, e.fs,
                                        linewidth=0.4,
                                        edgecolor=color,
                                        facecolor='none',
                                        alpha=0.8)

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
    logs: LogCollection
    """Collection of DragonRadio logs"""

    figures: Dict[str, Dict[Any, Figure]]
    """Cached figures"""

    def __init__(self, logs):
        self.logs = logs
        self.figures = {}

    def closeView(self, fig):
        for key in self.figures:
            if fig.node_id in self.figures[key] and self.figures[key][fig.node_id] == fig:
                del self.figures[key]
                return

    # show=False leaves showing the figure to the caller. In a notebook
    # (ipympl), a figure shown before it is first drawn can stay blank until
    # something redraws it, so draw first (view.plot(0)) and then display
    # view.fig.canvas.

    @cached_fig
    def rxView(self, node_id: int, nfft: int=256, show_header_invalid: bool=False, sigslop: int=0,
               show: bool=True, **kwargs):
        view = ReceiveView(self.logs, self, node_id,
                           nfft=nfft,
                           show_header_invalid=show_header_invalid,
                           sigslop=sigslop,
                           **kwargs)
        if show:
            view.fig.show()
        return view

    @cached_fig
    def txView(self, node_id: int, nfft: int=256, show: bool=True):
        view = SendView(self.logs, self, node_id, nfft=nfft)
        if show:
            view.fig.show()
        return view

    @cached_fig
    def snapshotView(self, node_id: int, nfft: int=256, show: bool=True):
        view = SnapshotView(self.logs, self, node_id, nfft=nfft)
        if show:
            view.fig.show()
        return view

class DrGUICommand(Command):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    def add_arguments(self, parser):
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

        add_annotate_args(parser)

    def handle(self, parser, args):
        mp.use('GTK3Agg')

        viewer = LogViewer(self.logs)

        for node_id in args.tx:
            if node_id not in self.logs.nodes:
                print("Cannot find node {}.".format(node_id), file=sys.stderr)
            else:
                view = viewer.txView(node_id, nfft=args.nfft)
                view.plot(0)

        for node_id in args.rx:
            if node_id not in self.logs.nodes:
                print("Cannot find node {}.".format(node_id), file=sys.stderr)
            else:
                view = viewer.rxView(node_id,
                                     nfft=args.nfft,
                                     show_header_invalid=args.show_invalid_headers,
                                     sigslop=args.sigslop,
                                     annotate=args.annotate,
                                     sticky=args.sticky)
                view.plot(0)

        for node_id in args.snapshots:
            if node_id not in self.logs.nodes:
                print("Cannot find node {}.".format(args.node_id), file=sys.stderr)
            else:
                view = viewer.snapshotView(node_id, nfft=args.nfft)
                view.plot(0)

        plt.show()

def drgui():
    mp.use('GTK3Agg')

    cmd = DrGUICommand()
    cmd.run('DragonRadio GUI viewer')
