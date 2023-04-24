# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

import argparse
import asyncio
from concurrent.futures import CancelledError
import logging
import os
import random
import signal
import sys

import dragonradio
import dragonradio.radio

class OFDMModulator(dragonradio.PacketModulator):
    def __init__(self,
                 phy,
                 header_mcs,
                 M,
                 cp_len,
                 taper_len,
                 subcarriers):
        dragonradio.PacketModulator.__init__(self, phy)
        self.phy = phy
        self.mod = dragonradio.liquid.OFDMModulator(header_mcs, M, cp_len, taper_len, subcarriers)

    def modulate(self, pkt, g):
        self.mod.payload_mcs = self.phy._mcs_table[pkt.mcsidx]
        sig = self.mod.modulate(pkt.hdr, pkt.payload)
        sig *= g
        iqbuf = dragonradio.IQBuf(sig)

        self.phy.updateAutoGain(pkt, g, iqbuf)

        mpkt = dragonradio.ModPacket()
        mpkt.offset = 0
        mpkt.nsamples = len(iqbuf)
        mpkt.samples = iqbuf
        mpkt.pkt = pkt

        return mpkt

class OFDMDemodulator(dragonradio.PacketDemodulator):
    def __init__(self,
                 phy,
                 header_mcs,
                 soft_header,
                 soft_payload,
                 M,
                 cp_len,
                 taper_len,
                 subcarriers):
        dragonradio.PacketDemodulator.__init__(self, phy)
        self.phy = phy
        self.demod = dragonradio.liquid.OFDMDemodulator(header_mcs, soft_header, soft_payload, M, cp_len, taper_len, subcarriers)

    def isFrameOpen(self):
        return self.demod.is_frame_open

    def reset(self, channel):
        self.channel = channel
        self.demod.reset()

    def timestamp(self, timestamp, snapshot_off, offset, delay, rate, rx_rate):
        self.t = timestamp

    def demodulate(self, iqbuf):
        pkts = self.demod.demodulate(iqbuf)

        rpkts = []

        for (hdr, payload, stats) in pkts:
            if hdr:
                rpkt = self.phy.mkRadioPacket(hdr, payload)
                rpkt.timestamp = self.t
                rpkt.evm = stats.evm
                rpkt.rssi = stats.rssi
                rpkt.cfo = stats.cfo
                rpkts.append(rpkt)

        return rpkts

class OFDM(dragonradio.PHY):
    def __init__(self,
                 snapshot_collector,
                 node_id,
                 header_mcs,
                 mcs_table,
                 soft_header,
                 soft_payload,
                 M,
                 cp_len,
                 taper_len,
                 subcarriers):
        dragonradio.PHY.__init__(self, snapshot_collector, node_id)
        self.header_mcs = header_mcs
        self._mcs_table = [mcs for (mcs, _autogain) in mcs_table]
        self.mcs_table = mcs_table
        self.soft_header = soft_header
        self.soft_payload = soft_payload
        self.M = M
        self.cp_len = cp_len
        self.taper_len = taper_len
        self.subcarriers = subcarriers

    def getRXOversampleFactor(self):
        return 1

    def getTXOversampleFactor(self):
        return 1

    def getModulatedSize(self, mcsidx, n):
        mod = dragonradio.liquid.OFDMModulator(self.header_mcs,
                                               self.M,
                                               self.cp_len,
                                               self.taper_len,
                                               self.subcarriers)
        mod.payload_mcs = self._mcs_table[mcsidx]

        hdr = dragonradio.Header()
        payload = b'0' * 1500

        iqbuf = mod.modulate(hdr, payload)
        return len(iqbuf)

    def mkPacketModulator(self):
        return OFDMModulator(self,
                             self.header_mcs,
                             self.M,
                             self.cp_len,
                             self.taper_len,
                             self.subcarriers)

    def mkPacketDemodulator(self):
        return OFDMDemodulator(self,
                               self.header_mcs,
                               self.soft_header,
                               self.soft_payload,
                               self.M,
                               self.cp_len,
                               self.taper_len,
                               self.subcarriers)

class Radio(dragonradio.radio.Radio):
    def __init__(self, *args, **kwargs):
        dragonradio.radio.Radio.__init__(self, *args, **kwargs)

    def mkPHY(self, header_mcs, mcs_table):
        return OFDM(self.snapshot_collector,
                    self.node_id,
                    header_mcs,
                    mcs_table,
                    self.config.soft_header,
                    self.config.soft_payload,
                    self.config.M,
                    self.config.cp_len,
                    self.config.taper_len,
                    self.config.subcarriers)

async def cancel_tasks(loop):
    tasks = [t for t in asyncio.Task.all_tasks() if t is not asyncio.Task.current_task()]
    for task in tasks:
        task.cancel()
        await task

    loop.stop()

def cancel_loop():
    loop = asyncio.get_event_loop()
    loop.create_task(cancel_tasks(loop))

def main():
    # Create configuration and set defaults
    config = dragonradio.radio.Config()

    config.mac = 'tdma'
    config.num_nodes = 2

    # Create command-line argument parser
    parser = config.parser()

    # Parse arguments
    try:
        parser.parse_args(namespace=config)
    except SystemExit as ex:
        return ex.code

    # Set up logging
    logging.basicConfig(format='%(asctime)s:%(name)s:%(levelname)s:%(message)s',
                        level=config.loglevel)

    # If a log directory is set, log packets and events
    if config.log_directory:
        config.log_sources += ['log_recv_packets', 'log_sent_packets', 'log_events', 'log_arq_events']

    # Create the radio
    radio = dragonradio.radio.Radio(config, config.mac)

    # Run the radio
    return radio.start()

if __name__ == '__main__':
    main()
