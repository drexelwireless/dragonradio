#!/usr/bin/env python3
import h5py
import pandas as pd
import numpy as np
import time

import dragonradio

class Slot:
    def __init__(self, timestamp, iqdata):
        self._timestamp = timestamp
        self._iqdata = iqdata

    @property
    def timestamp(self):
        return self._timestamp

    @property
    def data(self):
        return self._iqdata

LIQUID_CRC = pd.Series([ 'unknown'
                       , 'none'
                       , 'checksum'
                       , 'crc8'
                       , 'crc16'
                       , 'crc24'
                       , 'crc32' ])

LIQUID_FEC = pd.Series([ 'unknown'
                       , 'none'
                       , 'rep3'
                       , 'rep5'
                       , 'h74'
                       , 'h84'
                       , 'h128'
                       , 'g2412'
                       , 'secded2216'
                       , 'secded3932'
                       , 'secded7264'
                       , 'v27'
                       , 'v29'
                       , 'v39'
                       , 'v615'
                       , 'v27p23'
                       , 'v27p34'
                       , 'v27p45'
                       , 'v27p56'
                       , 'v27p67'
                       , 'v27p78'
                       , 'v29p23'
                       , 'v29p34'
                       , 'v29p45'
                       , 'v29p56'
                       , 'v29p67'
                       , 'v29p78'
                       , 'rs8' ])

LIQUID_MS = pd.Series([ 'unknown'

                        # phase-shift keying
                      , 'psk2'
                      , 'psk4'
                      , 'psk8'
                      , 'psk16'
                      , 'psk32'
                      , 'psk64'
                      , 'psk128'
                      , 'psk256'

                        # differential phase-shift keying
                      , 'dpsk2'
                      , 'dpsk4'
                      , 'dpsk8'
                      , 'dpsk16'
                      , 'dpsk32'
                      , 'dpsk64'
                      , 'dpsk128'
                      , 'dpsk256'

                      # amplitude-shift keying
                      , 'ask2'
                      , 'ask4'
                      , 'ask8'
                      , 'ask16'
                      , 'ask32'
                      , 'ask64'
                      , 'ask128'
                      , 'ask256'

                        # quadrature amplitude-shift keying
                      , 'qam4'
                      , 'qam8'
                      , 'qam16'
                      , 'qam32'
                      , 'qam64'
                      , 'qam128'
                      , 'qam256'

                        # amplitude/phase-shift keying
                      , 'apsk4'
                      , 'apsk8'
                      , 'apsk16'
                      , 'apsk32'
                      , 'apsk64'
                      , 'apsk128'
                      , 'apsk256'

                        # specific modem types
                      , 'bpsk'
                      , 'qpsk'
                      , 'ook'
                      , 'sqam32'
                      , 'sqam128'
                      , 'V29'
                      , 'arb16opt'
                      , 'arb32opt'
                      , 'arb64opt'
                      , 'arb128opt'
                      , 'arb256opt'
                      , 'arb64vt'

                        # arbitrary modem type
                      , 'arb' ])

class Node:
    def __init__(self):
        self.log_attrs = {}

    @property
    def node_id(self):
        """The node's ID"""
        return self.log_attrs['node_id']

    @property
    def start(self):
        """Time at which logging began (in seconds since the Epoch)"""
        return self.log_attrs['start']

    @property
    def crc_scheme(self):
        """Liquid DSP CRC scheme"""
        return self.log_attrs['crc_scheme'].decode()

    @property
    def fec0(self):
        """Liquid DSP inner forward error correction"""
        return self.log_attrs['fec0'].decode()

    @property
    def fec1(self):
        """Liquid DSP outer forward error correction"""
        return self.log_attrs['fec1'].decode()

    @property
    def modulation_scheme(self):
        """Liquid DSP modulation scheme"""
        return self.log_attrs['modulation_scheme'].decode()

def loadDataSet(ds):
    """Load an h5py data set into a pandas DataFrame"""
    data = np.empty(len(ds), dtype=ds.dtype)
    if len(ds) != 0:
        ds.read_direct(data)
    return pd.DataFrame(data)

class Slots:
    def __init__(self, ts, sig, bw):
        self.ts = ts
        self.sig = sig
        self.bw = bw

class Log:
    def __init__(self):
        self._nodes = {}
        self._logs = {}
        self._recv = {}
        self._send = {}
        self._slots = {}
        self._snapshots = {}
        self._selftx = {}
        self._events = {}

    def load(self, filename):
        with h5py.File(filename, 'r') as f:
            node = Node()
            for attr in f.attrs:
                node.log_attrs[attr] = f.attrs[attr]

            self._nodes[node.node_id] = node

            # Load IQ data for slots
            df = loadDataSet(f['slots'])
            df['start'] = df.timestamp
            df['end'] = df.timestamp + df.iq_data.apply(len) / df.bw

            self._slots[node.node_id] = df

            # Load snapshots
            df = loadDataSet(f['snapshots'])
            #df['iq_data'] = df.iq_data.apply(dragonradio.decompressFLAC)
            df['start'] = df.timestamp
            #df['end'] = df.timestamp + df.iq_data.apply(len) / df.fs

            self._snapshots[node.node_id] = df

            # Load snapshot packets
            df = loadDataSet(f['selftx'])

            self._selftx[node.node_id] = df

            # Load received packets
            df = loadDataSet(f['recv'])
            df.crc = LIQUID_CRC.get(df.crc, 'unknown').values
            df.fec0 = LIQUID_FEC.get(df.fec0, 'unknown').values
            df.fec1 = LIQUID_FEC.get(df.fec1, 'unknown').values
            df.ms = LIQUID_MS.get(df.ms, 'unknown').values
            df['start'] = df.timestamp + df.start_samples/df.bw
            df['end'] = df.timestamp + df.end_samples/df.bw

            self._recv[node.node_id] = df

            # Load sent packets
            df = loadDataSet(f['send'])
            df['start'] = df.timestamp
            df['end'] = df.timestamp + df.iq_data.str.len()/df.bw

            self._send[node.node_id] = df

            # Load events
            df = loadDataSet(f['event'])
            df.event = df.event.str.decode('utf-8')

            self._events[node.node_id] = df

            return node

    def getReceivedPacketIQData(self, node, pkt):
        """
        Get the IQ data corresponding to a received packet

        Args:
            node: The node.
            pkt: The packet.

        Returns:
            The packet's IQ data.
        """
        # Find the packet's slot
        slots = self.findSlots(node, pkt)
        if slots == None:
            return None

        # Extract the received IQ data corresponding to the packet. We add
        # 1ms worth of samples to account for slight inaccuracy on the part of
        # the packet start/end calculation
        slop = int(slots.bw*0.001)

        return w[pkt.start_samples-slop:pkt.end_samples+slop]

    def findReceivedPackets(self, node, t_start, t_end):
        """
        Find all packets received by a node in a given time period.

        Args:
            node: The node.
            t_start: The start of the time period.
            t_end: The end of the time period.

        Returns:
            A list of packets.
        """
        result = []

        recv = self.received[node.node_id]

        return recv[((recv.start >= t_start) & (recv.start < t_end)) | ((recv.end >= t_start) & (recv.end < t_end))]

    def findSlot(self, node, t):
        """
        Find a node's receive slot corresponding to a given time.

        Args:
            node: The node.
            t: A time in the slot.

        Returns:
            Either a slot or None.
        """
        slots = self._slots[node.node_id]

        return slots[(slots.start <= t) & (t < slots.end)]

    def findSlots(self, node, pkt):
        """
        Find the time slots during which a packet ocurred.

        Args:
            node: The node.
            pkt: A packet.

        Returns:
            A pair consisting of a list of slot timestamps and slot IQ data.
        """
        slots = self._slots[node.node_id]

        idx = slots['timestamp'] == pkt.timestamp

        if not idx.any():
            return None

        i = slots.index[idx].tolist()[0]

        slot1 = slots.loc[idx].iloc[0]
        slot2 = slots.iloc[i+1]
        ts = [slot1.timestamp, slot2.timestamp]
        data = np.concatenate((slot1.iq_data, slot2.iq_data))

        return Slots(ts, data, slot1.bw)

    def findReceivedPacketIndex(self, node, seq):
        """
        Find the index of a packet in a node's list of received packets.

        Args:
            node: The node.
            pkt: The packet.

        Returns:
            The index or None.
        """
        recv = self.received[node.node_id]

        idx = recv.seq == seq

        if idx.any():
            return recv.index[idx].tolist()[0]
        else:
            return None

    def findSentPacketIndex(self, node, seq):
        """
        Find the index of a packet in a node's list of sent packets.

        Args:
            node: The node.
            pkt: The packet.

        Returns:
            The index or None.
        """
        send = self.sent[node.node_id]

        idx = send.seq == seq

        if idx.any():
            return send.index[idx].tolist()[0]
        else:
            return None

    def findReceivedPacketsAt(self, node, t1, t2):
        """
        Find packets received by a node within given time.

        Args:
            node: The node.
            t1: Beginning of time interval.
            t2: End of time interval.

        Returns:
            A data frame of packets.
        """
        recv = self.received[node.node_id]

        idx = ((t1 >= recv.start) & (t1 < recv.end)) | \
              ((t2 >= recv.start) & (t2 < recv.end)) | \
              ((t1 < recv.start) & (t2 > recv.end))

        return recv[idx]

    def findSentPacketsAt(self, node, t1, t2):
        """
        Find packets send by a node within given time.

        Args:
            node: The node.
            t1: Beginning of time interval.
            t2: End of time interval.

        Returns:
            A data frame of packets.
        """
        send = self.sent[node.node_id]

        idx = ((t1 >= send.start) & (t1 < send.end)) | \
              ((t2 >= send.start) & (t2 < send.end)) | \
              ((t1 < send.start) & (t2 > send.end))

        return send[idx]

    @property
    def nodes(self):
        return self._nodes

    @property
    def snapshots(self):
        return self._snapshots

    @property
    def selftx(self):
        return self._selftx

    @property
    def received(self):
        return self._recv

    @property
    def sent(self):
        return self._send

    @property
    def events(self):
        return self._events
