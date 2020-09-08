import argparse
import asyncio
from concurrent.futures import CancelledError
import logging
import os
import random
import signal
import sys

import dragonradio
import dragon.radio

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

    def getMinRXRateOversample(self):
        return 1

    def getMinTXRateOversample(self):
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

class Radio(dragon.radio.Radio):
    def __init__(self, *args, **kwargs):
        dragon.radio.Radio.__init__(self, *args, **kwargs)

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
    config = dragon.radio.Config()

    # Default to TDMA
    config.mac = 'tdma'

    parser = argparse.ArgumentParser(description='Run dragonradio.',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    config.addArguments(parser)
    parser.add_argument('-n', action='store', type=int, dest='num_nodes',
                        default=2,
                        help='set number of nodes in network')
    parser.add_argument('--mac', action='store',
                        choices=['aloha', 'tdma', 'tdma-fdma', 'fdma'],
                        dest='mac',
                        help='set MAC')
    parser.add_argument('--aloha', action='store_const', const='aloha',
                        dest='mac',
                        help='use slotted ALOHA MAC')
    parser.add_argument('--tdma', action='store_const', const='tdma',
                        dest='mac',
                        help='use pure TDMA MAC')
    parser.add_argument('--fdma', action='store_const', const='fdma',
                        dest='mac',
                        help='use FDMA MAC')
    parser.add_argument('--tdma-fdma', action='store_const', const='tdma-fdma',
                        dest='mac',
                        help='use TDMA/FDMA MAC')

    # Parse arguments
    try:
        parser.parse_args(namespace=config)
    except SystemExit as ex:
        return ex.code

    # Set up logging
    logging.basicConfig(format='%(asctime)s:%(name)s:%(levelname)s:%(message)s',
                        level=config.loglevel)

    if config.log_directory:
        config.log_sources += ['log_recv_packets', 'log_sent_packets', 'log_events']

    # If we are in TDMA mode, set channel bandwidth to None so we use a single
    # channel
    if config.mac == 'tdma':
        config.channel_bandwidth = None

    # Create the radio object
    radio = Radio(config, slotted=(config.mac != 'fdma'))

    # Add all radio nodes to the network
    for i in range(0, config.num_nodes):
        radio.net.addNode(i+1)

    # Configure the MAC
    if config.mac == 'aloha':
        radio.configureALOHA()
    elif config.mac == 'tdma':
        radio.configureSimpleMACSchedule()
    elif config.mac == 'tdma-fdma':
        radio.configureSimpleMACSchedule()
    elif config.mac == 'fdma':
        radio.configureSimpleFDMASchedule(use_fdma_mac=True)
    else:
        raise Exception("Unknown MAC: %s" % config.mac)

    loop = asyncio.get_event_loop()

    if config.log_snapshots != 0:
        loop.create_task(radio.snapshotLogger())

    for sig in [signal.SIGINT, signal.SIGTERM]:
        loop.add_signal_handler(sig, cancel_loop)

    try:
        loop.run_forever()
    finally:
        loop.close()

    return 0

if __name__ == '__main__':
    main()
