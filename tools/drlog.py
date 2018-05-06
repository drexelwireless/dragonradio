#!/usr/bin/env python3
import h5py
import numpy as np
import time

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

class RecvPacket:
    def __init__(self, timestamp, start, end, hdr_valid, payload_valid, pkt_id, src, dest, evm, rssi, iqdata):
        self._timestamp = timestamp
        self._start = start
        self._end = end
        self._hdr_valid = hdr_valid
        self._payload_valid = payload_valid
        self._pkt_id = pkt_id
        self._src = src
        self._dest = dest
        self._evm = evm
        self._rssi = rssi
        self._iqdata = iqdata

    def __str__(self):
        return "Packet {} ({} -> {})".\
        format(self.pkt_id, self.src, self.dest)

    @property
    def timestamp(self):
        """Receive slot timestamp (in seconds since the logging node's start timestamp)"""
        return self._timestamp

    @property
    def start(self):
        """Packet start (in samples since the beginning of the receive slot)"""
        return self._start

    @property
    def end(self):
        """Packet end (in samples since the beginning of the receive slot)"""
        return self._end

    @property
    def hdr_valid(self):
        """Flag indicating whther or not the header is valid"""
        return self._hdr_valid

    @property
    def payload_valid(self):
        """Flag indicating whther or not the payload is valid"""
        return self._payload_valid

    @property
    def pkt_id(self):
        """Packet ID"""
        return self._pkt_id

    @property
    def src(self):
        """Source node ID"""
        return self._src

    @property
    def dest(self):
        """Destination node ID"""
        return self._dest

    @property
    def evm(self):
        """EVM (dB)"""
        return self._evm

    @property
    def rssi(self):
        """RSSI (dB)"""
        return self._rssi

    @property
    def data(self):
        """Demodulated IQ data"""
        return self._iqdata

class SendPacket:
    def __init__(self, timestamp, pkt_id, src, dest, iqdata):
        self._timestamp = timestamp
        self._pkt_id = pkt_id
        self._src = src
        self._dest = dest
        self._iqdata = iqdata

    @property
    def timestamp(self):
        """Packet timestamp (in seconds since the logging node's start timestamp)"""
        return self._timestamp

    @property
    def pkt_id(self):
        """Packet ID"""
        return self._pkt_id

    @property
    def src(self):
        """Source node ID"""
        return self._src

    @property
    def dest(self):
        """Destination node ID"""
        return self._dest

    @property
    def data(self):
        """Modulated IQ data"""
        return self._iqdata

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
        return time.localtime(self.log_attrs['start'])

    @property
    def tx_bandwidth(self):
        """TX bandwidth (in Hz)"""
        return self.log_attrs['tx_bandwidth']

    @property
    def rx_bandwidth(self):
        """RX bandwidth (in Hz)"""
        return self.log_attrs['rx_bandwidth']

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

class Log:
    def __init__(self):
        self._nodes = {}
        self._logs = {}
        self._recv = {}
        self._send = {}
        self._slots = {}

    def load(self, filename):
        with h5py.File(filename, 'r') as f:
            node = Node()
            for attr in f.attrs:
                node.log_attrs[attr] = f.attrs[attr]

            self._nodes[node.node_id] = node
            self._slots[node.node_id] = [Slot(*x) for x in f['slots']]
            self._recv[node.node_id] = [RecvPacket(*x) for x in f['recv']]
            self._send[node.node_id]= [SendPacket(*x) for x in f['send']]

            self._recv[node.node_id].sort(key=lambda x: x.pkt_id)

            return node

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

        Fs = node.rx_bandwidth
        recv = self.received[node.node_id]

        for pkt in recv:
            t1 = pkt.timestamp + t_start/Fs
            t2 = pkt.timestamp + t_end/Fs
            if t1 >= t_start and t1 < t_end:
                result.append(pkt)

        return result

    def findSlots(self, node, pkt):
        slots = self._slots[node.node_id]

        for i in range(0, len(slots)):
            if slots[i].timestamp == pkt.timestamp:
                ts = [slots[i].timestamp, slots[i+1].timestamp]
                data = np.concatenate((slots[i].data, slots[i+1].data))
                return (ts, data)

        return None

    def findReceivedPacketIndex(self, node, pkt_id):
        """
        Find the index of a packet in a node's list of received packets.

        Args:
            node: The node.
            pkt: The packet.

        Returns:
            The index or None.
        """
        recv = self.received[node.node_id]

        for i in range(0, len(recv)):
            if recv[i].pkt_id == pkt_id:
                return i
            elif recv[i].pkt_id > pkt_id:
                return None

        return None

    def findSentPacketIndex(self, node, pkt_id):
        """
        Find the index of a packet in a node's list of sent packets.

        Args:
            node: The node.
            pkt: The packet.

        Returns:
            The index or None.
        """
        send = self.sent[node.node_id]

        for i in range(0, len(send)):
            if send[i].pkt_id == pkt_id:
                return i
            elif send[i].pkt_id > pkt_id:
                return None

        return None

    @property
    def nodes(self):
        return self._nodes

    @property
    def received(self):
        return self._recv

    @property
    def sent(self):
        return self._send
