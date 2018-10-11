#!/usr/bin/env python3
import argparse
import matplotlib as mp
from matplotlib.text import OffsetFrom
from matplotlib.widgets import Button, Slider
import matplotlib.pyplot as plt
import numpy as np
import scipy.signal as signal
import sys

import drlog

# See:
#   http://stanford.edu/~raejoon/blog/2017/05/16/python-recipes-for-cdfs.html
#   https://stackoverflow.com/questions/24575869/read-file-and-plot-cdf-in-python
#   https://stackoverflow.com/questions/3209362/how-to-plot-empirical-cdf-in-matplotlib-in-python/11692365#11692365
def plot_ccdf(ax, data):
    sorted = np.sort(data)
    yvals = np.arange(1, len(sorted)+1)/float(len(sorted))
    #yvals = np.arange(len(sorted))/float(len(sorted)-1)
    ax.plot(sorted, 1-yvals)
    return sorted

# See:
#   https://stackoverflow.com/questions/11551049/matplotlib-plot-zooming-with-scroll-wheel
def zoom_factory(fig, ax, base_scale = 2.0):
    def zoom_fun(event):
        if event.inaxes == ax:
            # get the current x and y limits
            cur_xlim = ax.get_xlim()
            cur_ylim = ax.get_ylim()
            xdata = event.xdata # get event x location
            ydata = event.ydata # get event y location
            if event.button == 'up':
                # deal with zoom in
                scale_factor = 1/base_scale
            elif event.button == 'down':
                # deal with zoom out
                scale_factor = base_scale
            else:
                # deal with something that should never happen
                scale_factor = 1

            # set new limits
            ax.set_xlim([xdata - (xdata-cur_xlim[0]) / scale_factor,
                         xdata + (cur_xlim[1]-xdata) / scale_factor])
            ax.set_ylim([ydata - (ydata-cur_ylim[0]) / scale_factor,
                         ydata + (cur_ylim[1]-ydata) / scale_factor])
            fig.canvas.draw() # force re-draw

    return zoom_fun

class SpecgramPlot:
    def __init__(self, fig, ax, nfft=256, scale=1e3, cmap=plt.get_cmap('viridis')):
        self.fig = fig
        self.ax = ax
        self.scale = scale # kHz
        self.nfft = nfft
        self.cmap = cmap
        self.cb = None

    def plot(self, Fs, w, t0):
        xticks = mp.ticker.FuncFormatter(lambda x, pos: '{0:g}'.format(x+t0))
        yticks = mp.ticker.FuncFormatter(lambda x, pos: '{0:g}'.format(x/self.scale))

        self.ax.clear()
        if self.cb:
            self.cb.remove()
        pxx, freq, t, cax = self.ax.specgram(w, NFFT=self.nfft, Fs=Fs, cmap=self.cmap)
        self.cb = self.fig.colorbar(cax, ax=self.ax)
        self.cb.set_label('Intensity (dB)')
        self.ax.set_aspect('auto')
        self.ax.set_xlabel('Time (sec)')
        self.ax.set_ylabel('Frequency (kHz)')
        self.ax.xaxis.set_major_formatter(xticks)
        self.ax.yaxis.set_major_formatter(yticks)
        #self.ax.axis('tight')

class ConstellationPlot:
    def __init__(self, fig, ax):
        self.fig = fig
        self.ax = ax

    def plot(self, data):
        self.ax.clear()
        self.ax.scatter(np.real(data), np.imag(data))
        self.ax.set_title('Constellation')
        self.ax.set_xlabel('I')
        self.ax.set_ylabel('Q')
        #self.constellation.axis('tight')

class WaveformPlot:
    def __init__(self, fig, ax):
        self.fig = fig
        self.ax = ax

    def plot(self, sig):
        self.ax.clear()
        self.ax.plot(np.real(sig))
        self.ax.plot(np.imag(sig))
        self.ax.set_title('Waveform')
        self.ax.set_xlabel('Time (samples)')
        #self.ax.axis('tight')

        self.fig.canvas.mpl_connect('scroll_event', zoom_factory(self.fig, self.ax, base_scale=2.0))

class PSDPlot:
    def __init__(self, fig, ax, nfft=256, scale=1e3):
        self.fig = fig
        self.ax = ax
        self.scale = scale # kHz
        self.nfft = nfft

    def plot(self, Fs, sig):
        xticks = mp.ticker.FuncFormatter(lambda x, pos: '{0:g}'.format(x/self.scale))

        self.ax.clear()
        self.ax.psd(sig, NFFT=self.nfft, Fs=Fs)
        self.ax.set_title('PSD')
        self.ax.set_xlabel('Frequency (kHz)')
        self.ax.xaxis.set_major_formatter(xticks)
        #self.ax.axis('tight')

class PAPRPlot:
    def __init__(self, fig, ax):
        self.fig = fig
        self.ax = ax

    # See:
    #   https://www.dsprelated.com/showcode/238.php
    #   https://www.dsprelated.com/showarticle/962.php
    def plot(self, sig):
        P = sig.real**2 + sig.imag**2
        Pratio = P/np.mean(P)
        PdB = 10*np.log10(Pratio)

        self.ax.clear()
        plot_ccdf(self.ax, PdB)
        self.ax.set_title('CCDF of PAPR')
        self.ax.set_xlabel('$PAPR_0$ (dB)')
        self.ax.set_xlim(xmin=-5)
        self.ax.set_ylabel('$Pr(PAPR \geq PAPR_0)$')
        self.ax.set_yscale('log')
        #self.ax.grid(True)
        #self.ax.axis('tight')

        self.fig.canvas.mpl_connect('scroll_event', zoom_factory(self.fig, self.ax, base_scale=2.0))

class ReceivePlot:
    def __init__(self, log, node, show_header_invalid=False):
        self.log = log
        self.node = node
        self.Fs = self.node.rx_bandwidth
        self.show_header_invalid = show_header_invalid

        self.pktidx = 0

        self.fig = plt.figure()
        self.specgram = SpecgramPlot(self.fig, self.fig.add_subplot(2,1,1))
        self.constellation = ConstellationPlot(self.fig, self.fig.add_subplot(2,4,5))
        self.waveform = WaveformPlot(self.fig, self.fig.add_subplot(2,4,6))
        self.psd = PSDPlot(self.fig, self.fig.add_subplot(2,4,7))
        self.papr = PAPRPlot(self.fig, self.fig.add_subplot(2,4,8))

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
        self.spos = Slider(self.axpos, 'Packet Index', 0, len(self.received(self.node.node_id))-1, valfmt='%1.0f', valinit=0, valstep=1)
        self.spos.on_changed(self.update_slider)

    def received(self, node_id):
        recv = self.log.received[node_id]
        if self.show_header_invalid:
            return recv
        else:
            return recv[recv.header_valid == True]

    def plot(self, idx):
        recv = self.received(self.node.node_id)

        if idx >= 0 and idx < len(recv):
            self.pktidx = idx
            self.pkt = recv.iloc[idx]
            self.spos.set_val(idx)

            (self.ts, self.w) = self.log.findSlots(self.node, self.pkt)

            self.sig = self.w[self.pkt.start_samples:self.pkt.end_samples]

            if not self.pkt.header_valid:
                msg = 'INVALID HEADER'
            elif not self.pkt.payload_valid:
                msg = 'INVALID PAYLOAD'
            else:
                msg = ''

            self.fig.canvas.set_window_title('Node {} Received Packets'.format(self.node.node_id))
            self.fig.suptitle('Packet {} from node {} (evm {:03.1f}dB, rssi {:03.1f}dB, fc {:03.1f}MHz) {}'.format(self.pkt.seq, self.pkt.src, self.pkt.evm, self.pkt.rssi, self.pkt.fc/1e6, msg))

            t0 = self.ts[0]

            self.specgram.plot(self.Fs, self.w, t0)

            # Mark all packets in the current specgram
            #self.markPacket(self.pkt, self.specgram.ax)
            pkts = self.log.findReceivedPackets(self.node, self.ts[0], self.ts[0]+len(self.w)/self.Fs)
            if not self.show_header_invalid:
                pkts = pkts[pkts.header_valid == True]

            for (_, pkt) in pkts.iterrows():
                self.bracketPacket(pkt, self.specgram.ax)

            # Mark all slots in the current specgram
            for t in self.ts:
                self.markSlot(self.specgram.ax, t-t0)

            self.constellation.plot(self.pkt.iq_data)
            self.waveform.plot(self.sig)
            self.psd.plot(self.Fs, self.sig)
            self.papr.plot(self.sig)

            self.fig.canvas.draw()

    def update_slider(self, val):
        idx = int(val)
        if idx != self.pktidx:
            self.plot(idx)

    def next_packet(self, event):
        self.plot(self.pktidx+1)

    def prev_packet(self, event):
        self.plot(self.pktidx-1)

    def link_to_tx(self, event):
        global viewer

        node = self.log.nodes[self.pkt.src]
        if not node:
            return

        idx = self.log.findSentPacketIndex(node, self.pkt.seq)

        fig = viewer.txFig(node)
        fig.plot(idx)

    def markSlot(self, ax, t, **kwargs):
        ax.axvline(t, color='r')

    def bracketPacket(self, pkt, ax):
        t_start = pkt.start - pkt.timestamp
        t_end = pkt.end - pkt.timestamp

        (ymin, ymax) = ax.get_ylim()

        # If the packet we are marking is the current packet, its label appears
        # in red. Otherwise, its label appears in black.
        if pkt.seq == self.pkt.seq:
            color = 'r'
        else:
            color = 'k'

        ax.annotate('',
                    xy=(t_start, ymax),
                    xytext=(t_end, ymax),
                    xycoords='data',
                    arrowprops=dict(arrowstyle='<->', connectionstyle='bar, fraction=0.5', ec='k'))

        ax.text((t_start + t_end) / 2, ymax + 0.1*(ymax - ymin), str(pkt.seq),
                ha='center',
                va='bottom',
                weight='bold',
                color=color)

    def markPacket(self, pkt, ax):
        # XXX hard-coded constants for y position of labels. Fix this or get rid
        # of this function...
        self.note(ax, 'packet start', pkt.start/self.Fs, 1e6,
                  horizontalalignment='right',
                  verticalalignment='bottom')
        self.note(ax, 'packet end', pkt.end/self.Fs, -1e6,
                  horizontalalignment='left',
                  verticalalignment='bottom')

    def note(self, ax, msg, t, y, **kwargs):
        ax.annotate(msg,
                    xy=(t, 0),
                    xycoords='data',
                    xytext=(t, y),
                    textcoords='data',
                    arrowprops=dict(arrowstyle='->'),
                    **kwargs)

class SendPlot:
    def __init__(self, log, node):
        self.log = log
        self.node = node
        self.Fs = self.node.tx_bandwidth

        self.pktidx = 0

        self.fig = plt.figure()
        self.constellation = ConstellationPlot(self.fig, self.fig.add_subplot(2,2,1))
        self.waveform = WaveformPlot(self.fig, self.fig.add_subplot(2,2,2))
        self.psd = PSDPlot(self.fig, self.fig.add_subplot(2,2,3))
        self.papr = PAPRPlot(self.fig, self.fig.add_subplot(2,2,4))

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
        self.spos = Slider(self.axpos, 'Packet Index', 0, len(self.log.sent[self.node.node_id]), valfmt='%1.0f', valinit=0, valstep=1)
        self.spos.on_changed(self.update_slider)

    def plot(self, idx):
        send = self.log.sent[self.node.node_id]

        if idx >= 0 and idx < len(send):
            self.pktidx = idx
            self.pkt = send.iloc[idx]
            self.spos.set_val(idx)

            self.fig.canvas.set_window_title('Node {} Sent Packets'.format(self.node.node_id))
            self.fig.suptitle('Packet {} to node {}'.format(self.pkt.seq, self.pkt.dest))

            self.constellation.plot(self.pkt.iq_data)
            self.waveform.plot(self.pkt.iq_data)
            self.psd.plot(self.Fs, self.pkt.iq_data)
            self.papr.plot(self.pkt.iq_data)

            self.fig.canvas.draw()

    def update_slider(self, val):
        idx = int(val)
        if idx != self.pktidx:
            self.plot(idx)

    def next_packet(self, event):
        self.plot(self.pktidx+1)

    def prev_packet(self, event):
        self.plot(self.pktidx-1)

    def link_to_rx(self, event):
        global viewer

        node = self.log.nodes[self.pkt.dest]
        if not node:
            return

        idx = self.log.findReceivedPacketIndex(node, self.pkt.seq)

        fig = viewer.rxFig(node)
        fig.plot(idx)

class LogViewer:
    def __init__(self, log):
        self.log = log
        self.rxFigs = {}
        self.txFigs = {}

    def rxFig(self, node):
        if node.node_id in self.rxFigs:
            return self.rxFigs[node.node_id]
        else:
            fig = ReceivePlot(self.log, node)
            self.rxFigs[node.node_id] = fig
            fig.fig.show()
            return fig

    def txFig(self, node):
        if node.node_id in self.txFigs:
            return self.txFigs[node.node_id]
        else:
            fig = SendPlot(self.log, node)
            self.txFigs[node.node_id] = fig
            fig.fig.show()
            return fig

def main():
    global viewer

    parser = argparse.ArgumentParser(description='Show received packets.')
    parser.add_argument('-d', '--debug', action='store_true',
                        help='debug')
    parser.add_argument('--tx', action='append', type=int, default=[], dest='tx',
                        help='view TX log for given node')
    parser.add_argument('--rx', action='append', type=int, default=[], dest='rx',
                        help='view RX log for given node')
    parser.add_argument('paths', nargs='*')
    args = parser.parse_args()

    log = drlog.Log()
    viewer = LogViewer(log)

    for path in args.paths:
        log.load(path)

    for node_id in args.tx:
        node = log.nodes[node_id]
        if not node:
            print("Cannot find node {}.".format(args.node_id), file=sys.stderr)
        else:
            tx = viewer.txFig(node)
            tx.plot(0)

    for node_id in args.rx:
        node = log.nodes[node_id]
        if not node:
            print("Cannot find node {}.".format(args.node_id), file=sys.stderr)
        else:
            rx = viewer.rxFig(node)
            rx.plot(0)

    plt.show()

if __name__ == '__main__':
    main()
