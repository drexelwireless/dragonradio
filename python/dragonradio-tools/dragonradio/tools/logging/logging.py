# Copyright 2018-2021 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
"""Support for working with DragonRadio log files"""
import ast
import datetime
from functools import cached_property
import logging
import os
import re
from typing import List

import numpy as np
import pandas as pd
from pandas.api.types import CategoricalDtype

import h5py

from dragonradio.liquid import CRCScheme, FECScheme, ModulationScheme
from dragonradio.radio import decompressIQData
from dragonradio.tools.colosseum.logging import ReservationLog

logger = logging.getLogger(__name__)

def enumCategories(cls):
    """Return a CategoricalDtype for a pybind11 enum"""
    return CategoricalDtype(categories=cls.__members__.keys())

class Slots:
    """Slots containing IQ data for a received packet"""
    def __init__(self, pkt, slots, offset):
        self._pkt = pkt
        """Received packet"""

        self._slots = slots
        """Slots containing received IQ data"""

        self.ts = list(slots.timestamp)
        """Timestamps of slots in the set"""

        self.iq_data = np.concatenate([decompressIQData(sig) for sig in slots.iq_data])
        """Combined IQ data of slots in the set"""

        self.offset = offset
        """Offset of sample 0 of packet"""

        self.fs = slots.iloc[0].fs
        """Sample rate of IQ data"""

        if 'fc' in slots:
            self.fc = slots.iloc[0].fc
            """Sample center frequency"""

    def __getitem__(self, idx):
        if isinstance(idx, slice):
            return self.iq_data[self.offset+idx.start:self.offset+idx.stop:idx.step]
        else:
            return self.iq_data[self.offset+idx]

DROPPED: List[str] = ['transmitted', 'll_drop', 'queue_drop', 'phy_drop']
"""Category names for packet 'dropped' status"""

DROPPED_CAT:CategoricalDtype = CategoricalDtype(range(0, len(DROPPED)))
"""Categories for packet 'dropped' status"""

ARQ_EVENT_TYPE: List[str] = [ 'send_nak', 'send_sack', 'nak', 'retrans_nak'
                            , 'sack', 'snak', 'ack_timeout'
                            ]
"""Category names for ARQ event types"""

ARQ_EVENT_TYPE_CAT:CategoricalDtype = CategoricalDtype(range(0, len(ARQ_EVENT_TYPE)))
"""Categories for ARQ event types"""

EVENTS = [(re.compile(r), k, c) for (r, k, c) in
            [ [r'^AMC: Moved up modulation scheme', 'AMC', 'g']
            , [r'^AMC: Moved down modulation scheme', 'AMC', 'r']
            , [r'^AMC: Environment', 'AMC', 'k']
            , [r'^AMC: Ping', 'AMC', 'c']
            , [r'^AMC: Pong', 'AMC', 'm']
            , [r'^ARQ: send set unack', 'ARQ', 'k']
            , [r'^ARQ: set unack', 'ARQ', 'k']
            , [r'^ARQ: recv OUTSIDE WINDOW', 'ARQ', 'y']
            , [r'^ARQ: Node now reachable', 'ARQ', 'g']
            , [r'^ARQ: Node unreachable', 'ARQ', 'r']
            , [r'^PHY: invalid payload', 'PHY', 'r']
            , [r'^TIMESYNC:', 'TIMESYNC', 'k']
            , [r'^USRP:', 'USRP', 'k']
            , [r'^USRP: (RX|TX) error:', 'USRP', 'r']
            , [r'^QUEUE:', 'QUEUE', 'k']
            , [r'^NET:', 'NET', 'k']
            , [r'^MAC:', 'MAC', 'r']
            , [r'^TUNTAP:', 'TUNTAP', 'k']
            ]]

EVENT_CAT: CategoricalDtype = CategoricalDtype(['UNKNOWN', 'AMC', 'ARQ', 'MAC', 'PHY', 'NET', 'QUEUE', 'SYSTEM', 'TIMESYNC', 'USRP', 'TUNTAP'])
"""Categories for events"""

COLOR_CAT: CategoricalDtype = CategoricalDtype(['b', 'g', 'r', 'c', 'm', 'y', 'k', 'w'])
"""Categories for events"""

class Events:
    """Categorized events"""
    def __init__(self, log):
        self.log = log

    @cached_property
    def events(self):
        df = self.log.events.copy(deep=False)

        df['category'] = EVENT_CAT.categories[0]
        df['color'] = COLOR_CAT.categories[0]

        for (r, k, c) in EVENTS:
            idx = df.event.str.match(r)
            df.loc[idx, 'category'] = k
            df.loc[idx, 'color'] = c

        return df

    @cached_property
    def tx_records(self):
        df = self.log.tx_records.copy(deep=False)

        df['color'] = COLOR_CAT.categories[0]
        df.loc[df.nsamples == 0, 'color'] = 'r'

        return df

    @cached_property
    def send(self):
        df = self.log.send.copy(deep=False)

        df['color'] = COLOR_CAT.categories[0]
        df.loc[df.dropped == 'll_drop', 'color'] = 'r'
        df.loc[df.dropped == 'queue_drop', 'color'] = 'y'
        df.loc[df.dropped == 'phy_drop', 'color'] = 'r'

        return df

    @cached_property
    def recv(self):
        df = self.log.recv.copy(deep=False)

        df['color'] = COLOR_CAT.categories[0]
        df.loc[df.payload_valid == 0, 'color'] = 'y'
        df.loc[df.header_valid == 0, 'color'] = 'r'

        return df

class Log:
    """DragonRadio log data"""
    def __init__(self, path, log_collection=None):
        self.h5file = h5py.File(path, 'r')
        """HDF5 file containing radio log data"""

        self.log_collection = log_collection
        """LogCollection to which this log belongs, if any"""

    @property
    def start(self):
        """Time at which logging began (in seconds since the Epoch)"""
        return self.h5file.attrs['start']

    @property
    def delta(self):
        """Time delta between log entries in this log and the global start"""
        if self.log_collection is None:
            return 0

        return self.start - self.log_collection.start.timestamp()

    @property
    def node_id(self):
        """Node ID"""
        return self.h5file.attrs['node_id']

    @property
    def version(self):
        """Radio version"""
        return self.h5file.attrs['version']

    @cached_property
    def config(self):
        """Radio configuration"""
        config = self.h5file.attrs['config']

        # Rewrite new-style pybind11 enumerations
        config = re.sub(r'<(?:CRCScheme|FECScheme|ModulationScheme).(\w*): \d+>', r"'\1'", config, re.M)

        # Rewrite old-style pybind11 enumerations
        config = re.sub(r'(?:CRCScheme|FECScheme|ModulationScheme).(\w*)', r"'\1'", config, re.M)

        # Now we can evaluate the config as a python literal
        return ast.literal_eval(config)

    @cached_property
    def events(self):
        """Radio events"""
        df = self._loadDataset('event')
        df.event = df.event.str.decode('utf-8')

        return df

    @cached_property
    def event_cats(self):
        return Events(self)

    @cached_property
    def send(self):
        """Sent packets"""
        df = self._loadDataset('send')
        self._fixMCS(df)

        # For backwards compatibility; the 'dropped' field was not
        # always present.
        if 'dropped' not in df:
            df['dropped'] = 0

        # For backwards compatibility; the 'nretrans' field was not
        # always present.
        if 'nretrans' not in df:
            df['nretrans'] = 0

        # Convert dropped field to category
        df = df.astype({'dropped': DROPPED_CAT}, copy=False)
        df.dropped = df.dropped.cat.rename_categories(DROPPED)

        # Add packet start and end times based on slot timestamp, bandwidth, and
        # sample start and end.
        df['start'] = df.timestamp

        if 'nsamples' in df:
            df['end'] = df.timestamp + df.nsamples/df.bw
        else:
            df['end'] = df.timestamp + df.iq_data.str.len()/df.bw

        if 'mono_timestamp' in df and 'net_timestamp' in df:
            df['tx_latency'] = df.mono_timestamp - df.net_timestamp

        return df

    @cached_property
    def recv(self):
        """Received packets"""
        df = self._loadDataset('recv')
        self._fixMCS(df)

        has_slot_timestamp = 'slot_timestamp' in df

        if not has_slot_timestamp:
            df['slot_timestamp'] = df.timestamp

        # Add packet start and end times based on slot timestamp, bandwidth, and
        # sample start and end.
        df['start'] = df.slot_timestamp + df.start_samples/df.bw
        df['end'] = df.slot_timestamp + df.end_samples/df.bw

        if not has_slot_timestamp:
            df.timestamp = df.start

        if 'tuntap_latency' in df:
            df['rx_latency'] = df.tuntap_latency

        return df

    @cached_property
    def slots(self):
        """Received MAC slot IQ data"""
        df = self._loadDataset('slots')

        # For backwards compatibility; the 'bw' was renamed to 'fs'
        if 'bw' in df:
            df['fs'] = df.bw

        df['start'] = df.timestamp
        df['end'] = df.timestamp + df.iq_data_len / df.fs

        return df

    @cached_property
    def tx_records(self):
        """Sent MAC transmissions"""
        df = self._loadDataset('tx_records')

        df['start'] = df.timestamp
        df['end'] = df.timestamp + df.nsamples / df.fs

        return df

    @cached_property
    def snapshots(self):
        """Snapshots.

        The iq_data column is IQ data compressed using FLAC.
        """
        df = self._loadDataset('snapshots')
        df['start'] = df.timestamp
        return df

    @cached_property
    def selftx(self):
        """Self-transmissions"""
        return self._loadDataset('selftx')

    @cached_property
    def arq_events(self):
        """ARQ events"""
        df = self._loadDataset('arq_event')

        # Convert dropped field to category
        df = df.astype({'type': ARQ_EVENT_TYPE_CAT}, copy=False)
        df.type = df.type.cat.rename_categories(ARQ_EVENT_TYPE)

        return df

    @cached_property
    def sack(self):
        return self.sacks('sack')

    @cached_property
    def send_sack(self):
        return self.sacks('send_sack')

    def sacks(self, col):
        df = self.arq_events
        df = df[df.type == col]

        sacks_timestamp = []
        sacks_sack = []
        sacks_node = []
        sacks_seq = []

        count = 0

        for (_, row) in df.iterrows():
            count += 1

            if len(row.sacks) == 0:
                continue

            unack = row.seq

            if unack > row.sacks[1]:
                unack = row.sacks[0]
            else:
                unack = min(unack, row.sacks[0])

            i = 0

            while i < len(row.sacks):
                # Selective NAKs
                if unack <= row.sacks[i]:
                    n = row.sacks[i] - unack

                    sacks_seq.append(np.arange(unack, row.sacks[i], dtype=int))
                else:
                    n = 2**16-1 - unack + row.sacks[i]

                    sacks_seq.append(np.arange(unack, 2**16-1))
                    sacks_seq.append(np.arange(0, row.sacks[i]))

                sacks_timestamp.append(np.repeat(row.timestamp, n))
                sacks_sack.append(np.repeat(0, n))
                sacks_node.append(np.repeat(row.node, n))

                # Selective ACKs
                if row.sacks[i] <= row.sacks[i+1]:
                    n = row.sacks[i+1] - row.sacks[i]

                    sacks_seq.append(np.arange(row.sacks[i], row.sacks[i+1]))
                else:
                    n = 2**16-1 - row.sacks[i] + row.sacks[i+1]

                    sacks_seq.append(np.arange(row.sacks[i], 2**16-1))
                    sacks_seq.append(np.arange(0, row.sacks[i+1]))

                sacks_timestamp.append(np.repeat(row.timestamp, n))
                sacks_sack.append(np.repeat(1, n))
                sacks_node.append(np.repeat(row.node, n))

                unack = row.sacks[i+1]
                i += 2

        return pd.DataFrame({ 'timestamp': np.concatenate(sacks_timestamp)
                            , 'node': np.concatenate(sacks_node)
                            , 'seq': np.concatenate(sacks_seq)
                            , 'sack': np.concatenate(sacks_sack)
                            }).\
            astype({ 'timestamp': np.float64, 'node': int, 'seq': int, 'sack': int}, copy=False)

    @cached_property
    def mcs_table(self):
        """MCS table"""
        if self.config['amc_table'] is not None:
            mcs_list = [mcs for (mcs, _) in self.config['amc_table']]
        else:
            mcs_list = [tuple([self.config[key] for key in ('check', 'fec0', 'fec1', 'ms')])]

        df = pd.DataFrame(mcs_list, columns=['crc', 'fec0', 'fec1', 'ms'])

        CRCSchemeCat = enumCategories(CRCScheme)
        FECSchemeCat = enumCategories(FECScheme)
        ModulationSchemeCat = enumCategories(ModulationScheme)

        return df.astype({ 'crc': CRCSchemeCat
                         , 'fec0': FECSchemeCat
                         , 'fec1': FECSchemeCat
                         , 'ms' : ModulationSchemeCat
                         })

    def _loadDataset(self, key):
        """Load an HDF5 dataset into a Pandas DataFrame"""
        ds = self.h5file[key]
        data = np.empty(len(ds), dtype=ds.dtype)
        if len(ds) != 0:
            ds.read_direct(data)
        return pd.DataFrame(data)

    def _fixMCS(self, df):
        """Add crc, fec0, fec1, and ms columns based on mcsidx column and MCS table"""
        mcsidx_df = self.mcs_table.iloc[df.mcsidx.clip(0, len(self.mcs_table) - 1)]

        df['crc'] = mcsidx_df.crc.values
        df['fec0'] = mcsidx_df.fec0.values
        df['fec1'] = mcsidx_df.fec1.values
        df['ms'] = mcsidx_df.ms.values

    def findSlot(self, t):
        """
        Find received MAC slot corresponding to a given time.

        Args:
            t: A time.

        Returns:
            Either a slot or None.
        """
        slots = self.slots

        return slots[(slots.start <= t) & (t < slots.end)]

    def findPacketSlots(self, pkt):
        """Find slots during which a packet was received.

        Args:
            pkt: A packet.

        Returns:
            A Slots object containing the slots.
        """
        if pkt.nexthop != self.node_id:
            raise ValueError("Cannot find slots for packet received by "
              "node {} in log for node {}".format(pkt.nexthop, self.node_id))

        slots = self.slots

        # The packet's timestamp tells us which slot it was received in. We
        # assume there is only one slot with the given timestamp.
        idx = slots['timestamp'] == pkt.slot_timestamp

        if not idx.any():
            return None

        # i_start and i_end are the range if indices (inclusive) of the slots
        # spanned by the packet.
        i_start = i_end = slots.index[idx].values[0]
        fs = slots[idx].fs.iloc[0]

        # The packet may have started being received in the previous slot, in
        # which case start_samples will be negative. We must walk backwards
        # through the slots until we reach the slot in which packet reception
        # started. We track the offset of sample index 0 of the packet.
        offset = 0

        while offset + pkt.start_samples < 0:
            i_start -= 1
            offset += slots.iloc[i_start].iq_data_len

        return Slots(pkt, slots.iloc[i_start:i_end+1], offset)

    def findReceivedPackets(self, t1, t2):
        """Find packets received during a time interval.

        Args:
            t1: Beginning of time interval.
            t2: End of time interval.

        Returns:
            A data frame of packets.
        """
        recv = self.recv

        idx = ((t1 >= recv.start) & (t1 < recv.end)) | \
              ((t2 >= recv.start) & (t2 < recv.end)) | \
              ((t1 < recv.start) & (t2 > recv.end))

        return recv[idx]

    def findSentPackets(self, node, t1, t2):
        """Find packets sent during a time interval.

        Args:
            t1: Beginning of time interval.
            t2: End of time interval.

        Returns:
            A data frame of packets.
        """
        sent = self.sent

        idx = ((t1 >= sent.start) & (t1 < sent.end)) | \
              ((t2 >= sent.start) & (t2 < sent.end)) | \
              ((t1 < sent.start) & (t2 > sent.end))

        return sent[idx]

    def findReceivedPacketIndex(self, seq: int):
        """Find the index of received packet given sequence number.

        Args:
            seq: A sequence number

        Returns:
            The index or None.
        """
        recv = self.recv

        idx = recv.seq == seq

        if idx.any():
            return recv.index[idx].tolist()[0]
        else:
            return None

    def findSentPacketIndex(self, seq: int):
        """Find the index of sent packet given sequence number.

        Args:
            seq: A sequence number

        Returns:
            The index or None.
        """
        send = self.send

        idx = send.seq == seq

        if idx.any():
            return send.index[idx].tolist()[0]
        else:
            return None

class LogCollection:
    """A collection of DragonRadio logs"""
    def __init__(self,
                 start_time=None,
                 cache_path=None):
        """Initialize a collection of DragonRadio logs.

        Args:
            start_time: Explcicit RF scenario start time or None.
            cache_path: Path to root directory for DataFrame cache.

        Returns:
            None
        """

        self.logs = {}
        """Logs in the collection"""

        self.start_time = start_time
        """Time zero for log events (seconds since the epoch)"""

        self.cache_path = cache_path
        """Path to cache holding serialized DataFrames"""

        self.reservation = None
        """Colosseum reservation associate with this log collection"""

        self._start = None
        """Time zero for logs"""

    def load(self, path, start_time=None, srn_logs_path=None, srns=None):
        """Load a log into the collection"""
        if isinstance(path, list):
            for onepath in path:
                try:
                    self.load(onepath, srns=srns)
                except:
                    logger.exception('Could not load %s', onepath)
        elif os.path.isdir(path):
            if self.reservation is not None:
                raise ValueError("Already loaded a reservation")

            if len(self.logs) != 0:
                raise ValueError("Already loaded radio logs")

            self.reservation = ReservationLog(path,
                                              cache_path=self.cache_path,
                                              rf_start_time=self.start_time,
                                              srn_logs_path=srn_logs_path)

            self._start = self.reservation.rf_start_time

            if srns is not None:
                unknown_srns = set(srns) - set(self.reservation.our_srns)
                if len(unknown_srns) != 0:
                    raise ValueError("Unknown SRNs: %s" % unknown_srns)

            for srn in self.reservation.our_srns:
                if srns is None or srn in srns:
                    try:
                        path = os.path.join(self.reservation.srn_logs[srn],
                                            f'node-{srn:03d}',
                                            'radio.h5')
                        log = Log(path, log_collection=self)
                        self.logs[log.node_id] = log
                    except OSError:
                        logging.exception("Could not open HDF5 log file %s", path)
        else:
            log = Log(path, log_collection=self)
            self.logs[log.node_id] = log

    def __getitem__(self, idx):
      return self.logs[idx]

    @property
    def nodes(self):
        """Nodes whose logs are present in the log collection"""
        return self.logs.keys()

    @property
    def start(self):
        """Time zero for log events (seconds since the epoch)"""
        if self._start is not None:
            return self._start

        starts = [log.start for log in self.logs.values()]
        if len(starts) == 0:
            return None
        else:
            return datetime.datetime.fromtimestamp(min(starts))

    def findPacketSlots(self, pkt: int) -> Slots:
        """
        Find slots during which a packet was received.

        Args:
            pkt: A packet.

        Returns:
            A Slots object containing the slots.
        """
        if pkt.nexthop in self.logs:
            return self.logs[pkt.nexthop].findPacketSlots(pkt)
        else:
            return None
