# Copyright 2026 Drexel University
"""drgui for Jupyter: drgui's views driven by notebook (ipywidgets) controls.

drgui's views draw their controls (slider, Previous/Next, Link to TX/RX) as
matplotlib widgets inside the figure. Those only respond when the figure's
canvas passes mouse events back to the kernel. :class:`DrGUI` draws the same
panels with the same plot classes, so every quantity matches drgui, but its
controls are ordinary notebook widgets above the figure:

* the view (receive, send, snapshot) and the node;
* the packet or snapshot index: slider, Previous/Next, Play, jump to a
  sequence number;
* which received packets to step through (valid headers, all, failed payloads);
* the spectrogram overlays, FFT size, extra samples around a packet, and a
  time zoom into the spectrogram;
* Link to TX / Link to RX, which follow a packet to the other node.

A table beside the controls lists the selected packet's fields.

Usage, in a notebook::

    from dragonradio.tools.logging import LogCollection
    from dragonradio.tools.plot.radio.notebook import DrGUI

    logs = LogCollection()
    logs.load(paths)
    gui = DrGUI(logs)
    gui.show()          # or just leave `gui` as the cell's last line

From code: ``gui.view = 'send'``, ``gui.node = 1``, ``gui.index = 5``.

Rendering. With ``%matplotlib widget`` (ipympl) the figure is the live ipympl
canvas: the toolbar can pan and zoom, and hovering a packet in the spectrogram
shows its fields. With any other backend (``%matplotlib inline``, Colab, a
static export) each update redraws the figure as an image, and every control
still works. Pass ``renderer='inline'`` to force images.
"""
import asyncio
import html
import logging
from typing import Optional

import matplotlib
import matplotlib.pyplot as plt
import numpy as np

import ipywidgets as W
from IPython.display import clear_output, display

from dragonradio.tools.logging import LogCollection
from .drgui import ReceiveView, SendView, SnapshotView

__all__ = ['DrGUI']

VIEWS = ('receive', 'send', 'snapshot')

RX_FILTERS = {
    'valid headers (drgui default)': 'valid',
    'all, including invalid headers': 'all',
    'failed payloads only': 'bad-payload',
}

RX_OVERLAYS = {
    'plot_recv': 'received packets',
    'plot_sent': 'own transmissions',
    'plot_tx_records': "other nodes' TX records",
    'plot_events': 'MAC/USRP events',
    'plot_rx_slot_boundaries': 'received slot edges',
    'plot_slot_boundaries': 'TDMA slot grid',
}


def _received(logs: LogCollection, node_id: int, rx_filter: str):
    recv = logs[node_id].recv
    if rx_filter == 'all':
        return recv
    if rx_filter == 'bad-payload':
        return recv[(recv.header_valid == True) & (recv.payload_valid == False)]
    return recv[recv.header_valid == True]


class _ReceiveView(ReceiveView):
    """drgui's receive view, stepping through a chosen subset of packets."""
    rx_filter = 'valid'

    def received(self, node_id: int):
        return _received(self.logs, node_id, self.rx_filter)


def _fmt(v) -> str:
    if isinstance(v, (bool, np.bool_)):
        return 'yes' if v else 'NO'
    if isinstance(v, (float, np.floating)):
        if not np.isfinite(v):
            return str(v)
        if abs(v) >= 1e5:
            return f'{v:.6g}'
        return f'{v:.4g}'
    return html.escape(str(v))


def _table(title: str, rows) -> str:
    body = ''.join(f'<tr><td style="padding:0 8px 0 0;color:#666">{html.escape(k)}</td>'
                   f'<td style="text-align:right">{v}</td></tr>' for k, v in rows)
    return (f'<div style="font-size:12px;line-height:1.35"><b>{html.escape(title)}</b>'
            f'<table style="border-collapse:collapse">{body}</table></div>')


class DrGUI:
    """drgui's receive, send and snapshot views with notebook controls."""

    def __init__(self, logs: LogCollection, view: str = 'receive', node: Optional[int] = None,
                 index: int = 0, nfft: int = 256, figsize=(9, 6.5), renderer: str = 'auto'):
        if view not in VIEWS:
            raise ValueError(f'view must be one of {VIEWS}')
        self.logs = logs
        self.figsize = figsize
        if renderer == 'auto':
            # `%matplotlib widget` reports 'widget' (matplotlib >= 3.9) or
            # 'module://ipympl.backend_nbagg' (older).
            backend = matplotlib.get_backend().lower()
            renderer = 'widget' if 'ipympl' in backend or backend == 'widget' else 'inline'
        if renderer not in ('widget', 'inline'):
            raise ValueError("renderer must be 'auto', 'widget' or 'inline'")
        self.renderer = renderer
        self._views = {}          # (view, node, nfft) -> drgui view
        self._busy = False        # True while controls are set from code
        self._shown = False

        # --- controls ---------------------------------------------------
        lay = W.Layout
        self.w_view = W.ToggleButtons(options=[('Receive', 'receive'), ('Send', 'send'),
                                               ('Snapshot', 'snapshot')],
                                      value=view, style={'button_width': '84px'})
        self.w_node = W.Dropdown(description='Node', layout=lay(width='130px'),
                                 style={'description_width': '40px'})
        self.w_index = W.IntSlider(min=0, max=0, description='Packet', continuous_update=False,
                                   layout=lay(width='420px'), style={'description_width': '60px'})
        self.w_play = W.Play(interval=1200, step=1, min=0, max=0, layout=lay(width='90px'),
                             tooltip='Step through automatically')
        W.jslink((self.w_play, 'value'), (self.w_index, 'value'))
        self.w_prev = W.Button(description='◀ Previous', layout=lay(width='100px'))
        self.w_next = W.Button(description='Next ▶', layout=lay(width='100px'))
        self.w_count = W.HTML()
        self.w_seq = W.IntText(description='Seq', layout=lay(width='130px'),
                               style={'description_width': '30px'})
        self.w_goseq = W.Button(description='Go', layout=lay(width='50px'))
        self.w_link = W.Button(description='Link to TX', layout=lay(width='110px'),
                               tooltip='Show this packet at the node that sent or received it')
        self.w_filter = W.Dropdown(options=RX_FILTERS, value='valid', description='Packets',
                                   layout=lay(width='330px'), style={'description_width': '60px'})
        self.w_nfft = W.Dropdown(options=[64, 128, 256, 512, 1024], value=nfft, description='FFT',
                                 layout=lay(width='120px'), style={'description_width': '35px'})
        self.w_sigslop = W.IntSlider(min=0, max=400, step=10, value=0, continuous_update=False,
                                     description='Extra samples', layout=lay(width='300px'),
                                     style={'description_width': '95px'},
                                     tooltip='Samples shown either side of the packet (drgui --sigslop)')
        self.w_zoom = W.FloatRangeSlider(min=0.0, max=1.0, step=0.01, value=(0.0, 1.0),
                                         continuous_update=False, readout_format='.0%',
                                         description='Time zoom', layout=lay(width='420px'),
                                         style={'description_width': '75px'})
        self.w_overlays = {k: W.Checkbox(value=getattr(ReceiveView, k), description=v, indent=False,
                                         layout=lay(width='190px'))
                           for k, v in RX_OVERLAYS.items()}
        self.w_info = W.HTML(layout=lay(min_width='230px'))
        self.w_status = W.HTML()
        self.w_out = W.Output()

        self.w_prev.on_click(lambda _: self._step(-1))
        self.w_next.on_click(lambda _: self._step(+1))
        self.w_goseq.on_click(lambda _: self._goto_seq())
        self.w_link.on_click(lambda _: self._link())
        self.w_view.observe(self._on_view, 'value')
        self.w_node.observe(self._on_node, 'value')
        self.w_index.observe(self._on_index, 'value')
        self.w_filter.observe(self._on_filter, 'value')
        for w in (self.w_nfft, self.w_sigslop, self.w_zoom, *self.w_overlays.values()):
            w.observe(lambda _: self._redraw(), 'value')

        self._rx_row = W.HBox([self.w_filter, self.w_sigslop])
        self._overlay_box = W.GridBox(list(self.w_overlays.values()),
                                      layout=lay(grid_template_columns='repeat(3, 195px)'))
        nav = W.HBox([self.w_play, self.w_prev, self.w_index, self.w_next, self.w_count])
        top = W.HBox([self.w_view, self.w_node, self.w_nfft, self.w_seq, self.w_goseq, self.w_link])
        self._controls = W.VBox([top, nav, self._rx_row, self._overlay_box, self.w_zoom])
        self._figbox = W.VBox()
        self.ui = W.VBox([W.HBox([self._controls, self.w_info]), self.w_status, self._figbox])

        self._busy = True
        self._sync_view(node=node)
        self._set_index(index)
        self._busy = False

    # --- public properties -------------------------------------------------
    @property
    def view(self) -> str:
        return self.w_view.value

    @view.setter
    def view(self, v: str):
        self.w_view.value = v

    @property
    def node(self) -> int:
        return self.w_node.value

    @node.setter
    def node(self, n: int):
        self.w_node.value = n

    @property
    def index(self) -> int:
        return self.w_index.value

    @index.setter
    def index(self, i: int):
        self.w_index.value = i

    @property
    def current(self):
        """The drgui view currently shown (ReceiveView, SendView or SnapshotView)."""
        return self._get_view()

    def show(self):
        """Display the controls and figure, then draw."""
        if self._items() is not None and len(self._items()):
            self._place_figure(self._get_view())   # in the page before its first image
        display(self.ui)
        self._shown = True
        self._redraw()

    def _ipython_display_(self):
        self.show()

    # --- data ----------------------------------------------------------------
    def _nodes_for(self, view: str):
        nodes = []
        for n in sorted(self.logs.nodes):
            log = self.logs[n]
            try:
                count = {'receive': lambda: len(log.recv), 'send': lambda: len(log.send),
                         'snapshot': lambda: len(log.snapshots)}[view]()
            except Exception:  # dataset not logged
                count = 0
            if count:
                nodes.append(n)
        return nodes

    def _items(self):
        """The frame the index steps through, for the current view and node."""
        if self.node is None:
            return None
        log = self.logs[self.node]
        if self.view == 'receive':
            return _received(self.logs, self.node, self.w_filter.value)
        if self.view == 'send':
            return log.send
        return log.snapshots

    def _get_view(self):
        key = (self.view, self.node, self.w_nfft.value)
        if key not in self._views:
            with plt.ioff():
                if self.view == 'receive':
                    v = _ReceiveView(self.logs, None, self.node, nfft=self.w_nfft.value, controls=False)
                elif self.view == 'send':
                    v = SendView(self.logs, None, self.node, nfft=self.w_nfft.value, controls=False)
                else:
                    v = SnapshotView(self.logs, None, self.node, nfft=self.w_nfft.value, controls=False)
            v.fig.set_size_inches(*self.figsize)
            v.fig.subplots_adjust(left=0.09, right=0.97, bottom=0.08, top=0.88, hspace=0.45, wspace=0.45)
            self._views[key] = v
        return self._views[key]

    # --- control logic -------------------------------------------------------
    def _sync_view(self, node=None):
        """Set node choices, labels and visible rows for the current view."""
        view = self.view
        nodes = self._nodes_for(view)
        keep = node if node in nodes else (self.w_node.value if self.w_node.value in nodes else None)
        if keep is None and nodes:
            # drgui's usual choices: receive at the node that received most,
            # send at the node that sent most.
            keep = nodes[-1] if view != 'send' else nodes[0]
        self.w_node.options = nodes
        self.w_node.value = keep
        self.w_index.description = 'Snapshot' if view == 'snapshot' else 'Packet'
        self._rx_row.layout.display = '' if view == 'receive' else 'none'
        self._overlay_box.layout.display = '' if view == 'receive' else 'none'
        self.w_zoom.layout.display = 'none' if view == 'send' else ''
        self.w_link.layout.display = 'none' if view == 'snapshot' else ''
        self.w_link.description = 'Link to TX' if view == 'receive' else 'Link to RX'
        self.w_seq.layout.display = self.w_goseq.layout.display = 'none' if view == 'snapshot' else ''
        self._sync_range()

    def _sync_range(self):
        items = self._items()
        n = 0 if items is None else len(items)
        top = max(n - 1, 0)
        self.w_index.max = top
        self.w_play.max = top
        self.w_index.disabled = self.w_play.disabled = n == 0
        if self.w_index.value > top:
            self.w_index.value = top

    def _set_index(self, i: int):
        items = self._items()
        n = 0 if items is None else len(items)
        self.w_index.value = int(min(max(i, 0), max(n - 1, 0)))

    def _on_view(self, change):
        if self._busy:      # set from code, which syncs and redraws itself
            return
        self._busy = True
        self.w_zoom.value = (0.0, 1.0)
        self._sync_view()
        start = 0
        if self.view == 'snapshot':
            # Snapshots start before traffic does; open one from mid-run.
            start = len(self._items()) // 2
        self._set_index(start)
        self._busy = False
        self._redraw()

    def _on_node(self, change):
        if self._busy:
            return
        self._busy = True
        self._sync_range()
        self._set_index(0)
        self._busy = False
        self._redraw()

    def _on_filter(self, change):
        if self._busy:
            return
        self._busy = True
        self._sync_range()
        self._set_index(0)
        self._busy = False
        self._redraw()

    def _on_index(self, change):
        if not self._busy:
            self._redraw()

    def _step(self, d: int):
        self._set_index(self.index + d)

    def _goto_seq(self):
        items = self._items()
        if items is None or not len(items):
            return
        hits = np.flatnonzero(items.seq.to_numpy() == self.w_seq.value)
        if len(hits) == 0:
            self.w_status.value = f'<span style="color:#b00">No packet with seq {self.w_seq.value} in this list.</span>'
            return
        after = hits[hits > self.index]   # repeated seq: go to the next occurrence
        self.index = int(after[0] if len(after) else hits[0])

    def _link(self):
        v = self._get_view()
        pkt = getattr(v, 'pkt', None)
        if pkt is None:
            return
        if self.view == 'receive':
            node, label = pkt.curhop, None
            if node in self.logs.nodes:
                label = self.logs[node].findSentPacketIndex(pkt)
            if label is None:
                self.w_status.value = '<span style="color:#b00">Could not find the transmitted packet.</span>'
                return
            self._busy = True
            self.w_view.value = 'send'
            self._sync_view(node=node)
            self._set_index(self.logs[node].send.index.get_loc(label))
        else:
            node, label = pkt.nexthop, None
            if node in self.logs.nodes:
                label = self.logs[node].findReceivedPacketIndex(pkt)
            if label is None:
                self.w_status.value = '<span style="color:#b00">This packet was not received (or its receiver has no log).</span>'
                return
            self._busy = True
            self.w_view.value = 'receive'
            self._sync_view(node=node)
            items = self._items()
            if label not in items.index:
                self.w_filter.value = 'all'
                self._sync_range()
                items = self._items()
            self._set_index(items.index.get_loc(label))
        self._busy = False
        self._redraw()

    # --- drawing -------------------------------------------------------------
    def _place_figure(self, v):
        if self.renderer == 'widget':
            # Removing an ipympl canvas from a container does not reliably
            # remove it from the page, so every canvas stays a child and only
            # the current one is visible.
            canvas = v.fig.canvas
            canvas.header_visible = False
            if canvas not in self._figbox.children:
                self._figbox.children = (*self._figbox.children, canvas)
                # The browser creates the canvas's view asynchronously, and an
                # image sent before it exists is lost (the figure stays blank).
                # Draw again once the view is likely there.
                try:
                    loop = asyncio.get_event_loop()
                    for delay in (0.5, 2.0):
                        loop.call_later(delay, canvas.draw_idle)
                except RuntimeError:   # no event loop (not in a kernel)
                    pass
            for c in self._figbox.children:
                c.layout.display = '' if c is canvas else 'none'
        elif list(self._figbox.children) != [self.w_out]:
            self._figbox.children = [self.w_out]

    def _redraw(self):
        if not self._shown or self._busy:
            return
        items = self._items()
        n = 0 if items is None else len(items)
        what = 'snapshots' if self.view == 'snapshot' else 'packets'
        self.w_count.value = f'&nbsp;{self.index + 1 if n else 0} of {n} {what}'
        if n == 0:
            self.w_status.value = f'No {what} for this node and filter.'
            self.w_info.value = ''
            return
        self.w_status.value = ''
        v = self._get_view()
        self._place_figure(v)

        if self.view == 'receive':
            for k, w in self.w_overlays.items():
                setattr(v, k, w.value)
            v.rx_filter = self.w_filter.value
            v.sigslop = self.w_sigslop.value

        try:
            with plt.ioff():
                v.plot(self.index)
                if self.view == 'snapshot':   # drgui puts this only in the window title
                    s = v.snapshots.iloc[self.index]
                    v.fig.suptitle(f'Snapshot {self.index} at node {self.node} (t = {s.timestamp:.3f} s)')
                self._apply_zoom(v)
        except Exception as e:  # keep the controls usable
            logging.exception('drgui view failed')
            self.w_status.value = f'<span style="color:#b00">Drawing failed: {html.escape(repr(e))}</span>'
        else:
            if self.view == 'receive':
                slots = self.logs.findPacketSlots(v.pkt, self.node)
                if slots is None or slots.empty:
                    self.w_status.value = ('<span style="color:#b00">No received IQ (slots) logged for '
                                           'this packet, so the panels still show the previous one.</span>')
        self.w_info.value = self._info(v)

        if self.renderer == 'widget':
            v.fig.canvas.draw_idle()
        else:
            with self.w_out:
                clear_output(wait=True)
                display(v.fig)

    def _apply_zoom(self, v):
        """Zoom the spectrogram's time axis to a fraction of what was drawn."""
        lo, hi = self.w_zoom.value
        if self.view == 'send' or (lo, hi) == (0.0, 1.0):
            return
        ax = v.specgram.ax
        x0, x1 = ax.get_xlim()     # the full window: plot() cleared and redrew the axis
        ax.set_xlim(x0 + lo * (x1 - x0), x0 + hi * (x1 - x0))

    def _info(self, v) -> str:
        if self.view == 'snapshot':
            s = v.snapshots.iloc[self.index]
            fields = [('timestamp', _fmt(s.timestamp)), ('sample rate', _fmt(s.fs))]
            if 'fc' in s.index and np.isfinite(s.fc):
                fields.append(('centre freq', _fmt(s.fc)))
            try:
                selftx = self.logs[self.node].selftx
                fields.append(('own bursts', str(int((selftx.timestamp == s.timestamp).sum()))))
            except Exception:
                pass
            return _table(f'Snapshot {self.index} at node {self.node}', fields)
        pkt = getattr(v, 'pkt', None)
        if pkt is None:
            return ''
        keys = ['seq', 'src', 'dest', 'curhop', 'nexthop', 'header_valid', 'payload_valid',
                'evm', 'rssi', 'cfo', 'fc', 'bw', 'chan_bw', 'mcsidx', 'ms', 'size', 'data_len',
                'timestamp']
        rows = [(k, _fmt(pkt[k])) for k in keys if k in pkt.index]
        if self.view == 'receive' and pkt.curhop in self.logs.nodes:
            sent = self.logs[pkt.curhop].findSentPacket(pkt)
            if sent is not None:
                dt = pkt.aligned_timestamp - sent.aligned_timestamp
                rows.append(('clock delta', f'{dt * 1e3:.2f} ms'))   # drgui's: receive minus send time
        title = (f'Received at node {self.node}' if self.view == 'receive'
                 else f'Sent by node {self.node}')
        return _table(title, rows)
