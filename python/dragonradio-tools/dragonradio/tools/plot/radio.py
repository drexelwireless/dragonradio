# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
"""Plotting utilities for DragonRadio"""
from enum import Enum
import math
import re
from typing import List, Optional

import matplotlib as mp
from matplotlib.figure import Figure
import matplotlib.pyplot as plt
from matplotlib.ticker import MaxNLocator
import numpy as np

from dragonradio.tools.logging import LogCollection, COLOR_CAT
from dragonradio.tools.colosseum import ReservationLog, Scorer
from dragonradio.tools.plot.colosseum import plotStages
from dragonradio.tools.plot.util import AnnotatedPlot
from dragonradio.tools.plot.util import plotCCDF, zoomFactory, addCheckboxWidget

def pprEvent(e):
    """Pretty print an event"""
    return e.event

def pprSentPacket(pkt):
    """Pretty print a sent packet"""
    return "seq={seq}\ncurhop={curhop}\nnexthop={nexthop}\nsize={size}\ndata_len={data_len}\nmcsidx={mcsidx}\nflow={flow}\nmgen_seqno={mgen_seqno}".\
        format(seq=pkt.seq,
               curhop=pkt.curhop,
               nexthop=pkt.nexthop,
               size=pkt.size,
               data_len=pkt.data_len,
               mcsidx=pkt.mcsidx,
               flow=pkt.mgen_flow_uid,
               mgen_seqno=pkt.mgen_seqno)

def pprReceivedPacket(pkt):
    """Pretty print a received packet"""
    return "seq={seq}\ncurhop={curhop}\nnexthop={nexthop}\nsize={size}\ndata_len={data_len}\nflow={flow}\nmgen_seqno={mgen_seqno}".\
        format(seq=pkt.seq,
               curhop=pkt.curhop,
               nexthop=pkt.nexthop,
               size=pkt.size,
               data_len=pkt.data_len,
               flow=pkt.mgen_flow_uid,
               mgen_seqno=pkt.mgen_seqno)

def pprSACK(sack):
    """Pretty print a selective ACK"""
    return "seq={seq:.0f}\nnode={node:.0f}".\
        format(seq=sack.seq,
               node=sack.node)

class SpecgramPlot:
    """A spectrogram plot"""
    def __init__(self, fig, ax, nfft=256, noverlap=128, scale=1e3, cmap=plt.get_cmap('viridis')):
        if noverlap >= nfft:
            noverlap = nfft/2

        self.fig = fig
        self.ax = ax
        self.scale = scale # kHz
        self.nfft = nfft
        self.noverlap = noverlap
        self.cmap = cmap
        self.cb = None

    def plot(self, Fs, w, t0):
        xticks = mp.ticker.FuncFormatter(lambda x, pos: '{0:g}'.format(x+t0))
        yticks = mp.ticker.FuncFormatter(lambda x, pos: '{0:g}'.format(x/self.scale))

        self.ax.clear()
        if self.cb:
            self.cb.remove()

        _pxx, _freq, _t, cax = self.ax.specgram(w, NFFT=self.nfft, noverlap=self.noverlap, Fs=Fs, cmap=self.cmap)

        self.cb = self.fig.colorbar(cax, ax=self.ax)
        self.cb.set_label('Intensity (dB)')
        self.ax.set_aspect('auto')
        self.ax.set_xlabel('Time (sec)')
        self.ax.set_ylabel('Frequency (kHz)')
        self.ax.set_ylim(-Fs/2, Fs/2)
        self.ax.xaxis.set_major_formatter(xticks)
        self.ax.yaxis.set_major_formatter(yticks)
        #self.ax.axis('tight')

class ConstellationPlot:
    """A constellation plot"""
    def __init__(self, fig, ax):
        self.fig = fig
        self.ax = ax

    def plot(self, data, title='Constellation'):
        self.ax.clear()
        self.ax.scatter(np.real(data), np.imag(data))
        if title:
            self.ax.set_title(title)
        self.ax.set_xlabel('I')
        self.ax.set_ylabel('Q')
        #self.constellation.axis('tight')

class WaveformPlot:
    """A waveform plot"""
    def __init__(self, fig, ax):
        self.fig = fig
        self.ax = ax

    def plot(self, sig, title='Waveform', sigslop=0):
        self.ax.clear()
        self.ax.plot(np.real(sig))
        self.ax.plot(np.imag(sig))
        if title:
            self.ax.set_title(title)
        self.ax.set_xlabel('Time (samples)')
        #self.ax.axis('tight')

        if sigslop:
            self.ax.axvline(sigslop, color='r')
            self.ax.axvline(len(sig)-sigslop, color='r')

        self.fig.canvas.mpl_connect('scroll_event', zoomFactory(self.fig, self.ax, base_scale=2.0))

class PSDPlot:
    """A PSD plot"""
    def __init__(self, fig, ax, nfft=256, scale=1e3):
        self.fig = fig
        self.ax = ax
        self.scale = scale # kHz
        self.nfft = nfft

    def plot(self, Fs, sig, title='PSD'):
        xticks = mp.ticker.FuncFormatter(lambda x, pos: '{0:g}'.format(x/self.scale))

        self.ax.clear()
        self.ax.psd(sig, NFFT=self.nfft, Fs=Fs)
        if title:
            self.ax.set_title(title)
        self.ax.set_xlabel('Frequency (kHz)')
        self.ax.xaxis.set_major_formatter(xticks)
        #self.ax.axis('tight')

class PAPRPlot:
    """A PAPR plot"""
    def __init__(self, fig, ax):
        self.fig = fig
        self.ax = ax

    # See:
    #   https://www.dsprelated.com/showcode/238.php
    #   https://www.dsprelated.com/showarticle/962.php
    def plot(self, sig, title='CCDF of PAPR'):
        P = sig.real**2 + sig.imag**2
        Pratio = P/np.mean(P)
        with np.errstate(divide='ignore'):
            PdB = 10*np.log10(Pratio)

        self.ax.clear()
        plotCCDF(self.ax, PdB)
        self.ax.set_title(title)
        self.ax.set_xlabel('$PAPR_0$ (dB)')
        self.ax.set_xlim(left=-5)
        self.ax.set_ylabel('$Pr(PAPR \geq PAPR_0)$')
        self.ax.set_yscale('log')
        #self.ax.grid(True)
        #self.ax.axis('tight')

        self.fig.canvas.mpl_connect('scroll_event', zoomFactory(self.fig, self.ax, base_scale=2.0))

class ReservationPlot(AnnotatedPlot):
    def __init__(self, fig, *args, logs: LogCollection=None, **kwargs):
        super().__init__(fig, *args, **kwargs)

        self.logs: LogCollection = logs
        """Logs to plot"""

    def plotStages(self, ax=None):
        # If logs are taken from a Colosseum reservation, plot stage markers
        if self.logs.reservation is not None:
            scorer = Scorer(self.logs.reservation)
            if ax is None:
                ax = self.ax
            plotStages(ax, scorer)

class RadioMetricPlot(ReservationPlot):
    RECV_METRICS = frozenset(['cfo', 'evm', 'rssi', 'mcsidx', 'ms', 'interarrival', 'demod_latency', 'rx_latency'])

    MS_METRICS = frozenset(['ms', 'sent_ms'])

    METRIC_YLABELS = { 'cfo': 'CFO (Hz)'
                     , 'evm': 'EVM (dB)'
                     , 'rssi': 'RSSI (dB)'
                     , 'mcsidx': 'MCS Index'
                     , 'ms': 'Modulation Scheme'
                     , 'interarrival': 'Interarrival Time (sec)'

                     , 'demod_latency': 'Demodulation Latency (sec)'
                     , 'rx_latency': 'Packet RX Latency (sec)'

                     , 'sent_mcsidx': 'MCS Index'
                     , 'sent_ms': 'Modulation Scheme'
                     , 'interdeparture': 'Interdeparture Time (sec)'

                     , 'tuntap_latency': 'tun/tap read Latency (sec)'
                     , 'enqueue_latency': 'Packet Enqueue Latency (sec)'
                     , 'dequeue_latency': 'Packet Dequeue Latency (sec)'
                     , 'queue_latency': 'Packet Queue Latency (sec)'
                     , 'llc_latency': 'LLC Latency (sec)'
                     , 'synth_latency': 'Synthesizer Latency (sec)'
                     , 'mod_latency': 'Modulation Latency (sec)'
                     , 'tx_latency': 'Packet TX Latency (sec)'
                     }

    def __init__(self, fig, ax, logs,
                 metric=None,
                 latency=None,
                 nodes=None,
                 checkboxes=True,
                 only_invalid_packets=False,
                 include_invalid_packets=False,
                 filt=lambda x : x):
        super().__init__(fig, ax, logs=logs)

        self.metric = metric
        """The metric to plot"""

        self.addAnnotation(ax)

        yticks = None

        if nodes is None:
            nodes = logs.nodes

        for node_id in sorted(nodes):
            # Determine DataFrame containing metric and annotation printer
            if metric in self.RECV_METRICS:
                df = logs[node_id].recv
                ppr = pprReceivedPacket
            else:
                df = logs[node_id].send
                ppr = pprSentPacket

            # Restrict packets
            if only_invalid_packets:
                df = df.loc[(df.header_valid == 1) & (df.payload_valid == 0)]
            elif include_invalid_packets:
                df = df.loc[(df.header_valid == 1) & (df.payload_valid == 1)]

            # Apply DataFrame filter
            df = filt(df)

            # Use time delta to align node's timestamps with collective start of
            # log.
            x = df.timestamp + logs[node_id].delta

            # Determine y axis
            if latency is not None:
                metrics = latency.split(',')
                if len(metrics) != 2:
                    raise Exception('Latency must specify two metrics, but got %s' % latency)

                metric_from, metric_to = metrics

                y = df[metric_to] - df[metric_from]

                def strip_latency(s):
                    return re.sub('_latency', '', s)

                ylabel = f"Latency from {strip_latency(metric_from):} to {strip_latency(metric_to):}"
            else:
                if metric == 'interarrival' or metric == 'interdeparture':
                    df.set_index(['timestamp'], inplace=True)
                    df.sort_index(inplace=True)
                    df.reset_index(inplace=True)

                    y = df.timestamp.diff()
                elif metric in self.MS_METRICS:
                    y = df.ms.cat.codes
                elif metric == 'sent_mcsidx':
                    y = df['mcsidx']
                else:
                    y = df[metric]

                # Determine y label
                ylabel = self.METRIC_YLABELS[metric]

            # If this is a modulation scheme metric, set ticks appropriately
            if metric in self.MS_METRICS:
                cats = df.ms.cat.categories
                yticks = (range(0, len(cats)), list(cats))

            # Plot data for this node
            line, = ax.plot(x, y,
                            'o',
                            alpha=0.3,
                            markersize=2,
                            label=str(node_id))
            line.df = df
            line.ppr = ppr
            self.addLine(ax, line)

        if metric in self.RECV_METRICS:
            self.fig.suptitle('Received Packet Metric')
        else:
            self.fig.suptitle('Sent Packet Metric')

        if checkboxes:
            self.fig.subplots_adjust(right=0.83)
            addCheckboxWidget(self.fig,
                            self.lines[ax],
                            ax=ax,
                            match_legend=True,
                            height_factor=0.15)

        ax.set_xlabel('Time (sec)')
        ax.set_ylabel(ylabel)

        if yticks is not None:
            lo, hi = ax.get_ylim()
            lo = math.ceil(lo)
            hi = math.ceil(hi)

            ticks, labels = yticks
            ax.set_yticks(ticks[lo:hi])
            ax.set_yticklabels(labels[lo:hi])

        if not checkboxes:
            ax.legend(handles=self.lines[ax], loc='upper right')

        # Plot stage markers
        self.plotStages()

        # Attach hover event handler
        self.fig.canvas.mpl_connect("motion_notify_event", self.hover)

class EventPlot(ReservationPlot):
    def __init__(self, logs, fig=None, ax=None):
        if fig is None:
            fig = plt.figure(figsize=(14,4))

        if ax is None:
            ax = fig.add_subplot(1,1,1)

        super().__init__(fig, ax, logs=logs)

        self.series = []
        """Event series to plot"""

        self.cats = []
        """Categories"""

    def addEventCategory(self, node_id, k, ppr, filt=None):
        if k == 'SEND':
            df = self.logs[node_id].event_cats.send
        elif k == 'RECV':
            df = self.logs[node_id].event_cats.recv
        else:
            df = self.logs[node_id].event_cats.events
            df = df[df.category==k]

        # Filter by arbitrary predicate
        if filt != None:
            df = filt(df)

        for c in COLOR_CAT.categories:
            idx = df.color == c

            self.series.append((node_id, k,
                                df[idx].timestamp+ self.logs[node_id].delta,
                                len(self.cats),
                                c,
                                df[idx],
                                ppr))

        self.cats.append((node_id, k))

    def plot(self):
        title = 'Event Timeline Plot'
        self.ax.set_title(title)
        self.fig.canvas.set_window_title(title)

        self.ax.set_ylim((-1, len(self.cats)))
        self.ax.set_yticks(range(len(self.cats)))
        self.ax.set_yticklabels(["Node {}: {}".format(s[0], s[1]) for s in self.cats])

        self.addAnnotation(self.ax)

        for (_node_id, _k, x, y, c, df, ppr) in self.series:
            line, = self.ax.plot(x, np.full(len(x), y),
                                 'o',
                                 markerfacecolor=c,
                                 markeredgecolor=c,
                                 alpha=0.85,
                                 markersize=3)

            line.df = df
            line.ppr = ppr
            self.addLine(self.ax, line)

        # Plot stage markers
        self.plotStages()

        self.fig.canvas.mpl_connect("motion_notify_event", self.hover)

class TrafficPlot(ReservationPlot):
    def __init__(self,
                 fig: Figure,
                 logs: LogCollection,
                 src: Optional[int],
                 dest: Optional[int],
                 y: str='seq',
                 filt=lambda x : x,
                 by_flow: bool=True,
                 mac_errors: bool=False,
                 sack: bool=False):
        super().__init__(fig, logs=logs, sticky=True)

        self.filt = filt
        """DataFrame filter"""

        # Set title
        if src is not None and dest is not None:
            title = 'Traffic {} $\\to$ {}'.format(src, dest)
        elif src is not None:
            title = 'From {}'.format(src)
        elif dest is not None:
            title = 'To {}'.format(dest)
        else:
            raise ValueError("Must specify at least one of src and dest")

        # Set title
        self.fig.suptitle(title)
        self.fig.canvas.set_window_title(title)

        if src is not None and dest is not None:
            sent_ax = self.fig.add_subplot(2,1,1)
            recv_ax = self.fig.add_subplot(2,1,2, sharex=sent_ax)
        elif src is not None:
            sent_ax = self.fig.add_subplot(1,1,1)
        elif dest is not None:
            recv_ax = self.fig.add_subplot(1,1,1)

        if src is not None:
            self.plotSentTraffic(sent_ax, src, dest, y, by_flow=by_flow)

            if mac_errors:
                self.plotMACErrors(sent_ax, src, r'^(MAC: (NO SLOT|MISSED))')

            if sack:
                self.plotSACK(sent_ax, src, dest, 'sack', alpha=0.1)

                delta = self.logs[src].delta

                df = self.logs[src].arq_events
                df = df[df.node == dest]

                self.plotTraffic(sent_ax, df[df.type == 'nak'], 'seq', delta, pprSACK,
                                 label='NAK', color='y')

                self.plotTraffic(sent_ax, df[df.type == 'retrans_nak'], 'seq', delta, pprSACK,
                                 label='NAK (retrans)', color='y')

                self.plotTraffic(sent_ax, df[df.type == 'snak'], 'seq', delta, pprSACK,
                                 label='NAK (selective)', color='y')

                self.plotTraffic(sent_ax, df[df.type == 'ack_timeout'], 'seq', delta, pprSACK,
                                 label='ACK timeout', color='y')

            # Plot stage markers
            self.plotStages(ax=sent_ax)

        if dest is not None:
            self.plotRecvTraffic(recv_ax, src, dest, y, by_flow=by_flow)

            if mac_errors:
                self.plotMACErrors(recv_ax, dest, r'^(MAC: (attempting))')

            if sack:
                self.plotSACK(recv_ax, dest, src, 'send_sack', alpha=0.1)

            # Plot stage markers
            self.plotStages(ax=recv_ax)

        # Tighten layout
        self.fig.tight_layout()

        # Add checkbox widgets
        self.fig.tight_layout()
        self.fig.subplots_adjust(right=0.83)
        if src is not None:
            addCheckboxWidget(self.fig, self.lines[sent_ax], ax=sent_ax, match_legend=True)
        if dest is not None:
            addCheckboxWidget(self.fig, self.lines[recv_ax], ax=recv_ax, match_legend=True)

        # Attach hover event handler
        self.fig.canvas.mpl_connect("motion_notify_event", self.hover)

    def plotMACErrors(self, ax, node_id, pat):
        delta = self.logs[node_id].delta

        r = re.compile(pat)
        mac_events = self.logs.events[node_id]
        mac_events = mac_events[mac_events.event.str.match(r)]

        (t1, t2) = ax.get_xlim()

        for (_, row) in mac_events.iterrows():
            if row.t >= t1 and row.t <= t2:
                ax.axvline(x=row.timestamp + delta, color='r')

    Y_LABELS = { 'seq':        'Sequence number'
               , 'mgen_seqno': 'MGEN sequence number'
               , 'mcsidx':     'MCS index'}

    def plotTraffic(self, ax, df, y, delta, ppr, color='k', alpha=0.85, label=None):
        line, = ax.plot(df.timestamp + delta, df[y],
                        'o',
                        markersize=2,
                        color=color,
                        alpha=alpha,
                        label=label)

        line.df = df
        line.ppr = ppr
        self.addLine(ax, line)

    def plotSentTraffic(self, ax, src, dest, y, by_flow=True):
        self.addAnnotation(ax)

        delta = self.logs[src].delta

        send_df = self.logs[src].send

        # Restrict to packets to destination
        if dest is not None:
            send_df = send_df[send_df.dest == dest]

        # Apply DataFrame filter
        send_df = self.filt(send_df)

        # Plot sent packets and then dropped packets. This makes the dropped
        # packets stand out on the plot.
        def plotSend(df, color='k', alpha=0.85, label=None):
            self.plotTraffic(ax, df, y, delta, pprSentPacket, color, alpha, label)

        if by_flow:
            flows = send_df.mgen_flow_uid[send_df.mgen_flow_uid != 0].unique()
            flows.sort()

            for flow in flows:
                plotSend(send_df[send_df.mgen_flow_uid == flow],
                         color=None,
                         label=str(flow))
        else:
            plotSend(send_df[(send_df.dropped == 'transmitted') & (send_df.nretrans == 0)],
                    label='delivered')
            plotSend(send_df[(send_df.dropped == 'transmitted') & (send_df.nretrans != 0)],
                    color='b',
                    label='redelivered')
            plotSend(send_df[send_df.dropped == 'll_drop'],
                    color='r',
                    label='link-layer drop')
            plotSend(send_df[send_df.dropped == 'queue_drop'],
                    color='0.5',
                    alpha=0.1,
                    label='queue drop')

        ax.yaxis.set_major_locator(MaxNLocator(integer=True))

        ax.set_title('Sent Packets (Node {})'.format(src))

        ax.set_xlabel('Time (sec)')

        ax.yaxis.set_major_locator(MaxNLocator(integer=True))
        ax.set_ylabel(self.Y_LABELS[y])

    def plotRecvTraffic(self, ax, src, dest, y, by_flow=False):
        self.addAnnotation(ax)

        delta = self.logs[dest].delta

        recv_df = self.logs[dest].recv

        # Restrict to packets from source
        if src is not None:
            recv_df = recv_df[recv_df.src == src]

        # Apply DataFrame filter
        recv_df = self.filt(recv_df)

        # Plot valid packets and then packets with an invalid payload. This
        # makes the packets with an invalid payload stand out on the plot.
        def plotRecv(df, color='k', alpha=0.85, label=None):
            self.plotTraffic(ax, df, y, delta, pprReceivedPacket, color, alpha, label)

        if by_flow:
            flows = recv_df.mgen_flow_uid[recv_df.mgen_flow_uid != 0].unique()
            flows.sort()

            for flow in flows:
                plotRecv(recv_df[recv_df.mgen_flow_uid == flow],
                         color=None,
                         label=str(flow))
        else:
            plotRecv(recv_df[(recv_df.header_valid == 1) & (recv_df.payload_valid == 1)],
                     color='k',
                     label='valid')
            plotRecv(recv_df[(recv_df.header_valid == 1) & (recv_df.payload_valid != 1)],
                     color='r',
                     label='invalid payload')

        ax.set_title('Received Packets (Node {})'.format(dest))

        ax.set_xlabel('Time (sec)')

        ax.yaxis.set_major_locator(MaxNLocator(integer=True))
        ax.set_ylabel(self.Y_LABELS[y])

    def plotSACK(self, ax, node, dest, col='sack', alpha=0.85):
        delta = self.logs[node].delta

        df = self.logs[node].sacks(col)

        self.plotTraffic(ax, df[(df.sack == 0) & (df.node == dest)], 'seq', delta, pprSACK, label='SNAK', color='r', alpha=alpha)

    def plot(self):
        self.fig.canvas.draw()
