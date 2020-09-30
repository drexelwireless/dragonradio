# Copyright 2018 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

import argparse
import asyncio
import csv
import logging
import math
import numpy as np
import os
import re
import signal
import socket
import struct
import sys
import time

import dragonradio
import dragonradio.radio

IPERF_PORT = 5000

UDP_OVERHEAD = 28

class IperfServerProtocol:
    def __init__(self, writer):
        self.writer = writer
        self.writer.writerow(('timestamp', 'test', 'seq', 'datalen'))

    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, addr):
        (testnum, seq) = struct.unpack_from('!hh', data)
        self.writer.writerow((time.perf_counter(), testnum, seq, len(data)))

class IperfClientProtocol:
    def __init__(self, args, loop):
        self.args = args
        self.loop = loop
        self.transport = None
        self.completed = asyncio.Future()

    def connection_made(self, transport):
        self.transport = transport

    def error_received(self, exc):
        print('Error received:', exc)

    def connection_lost(self, exc):
        if exc is None:
            self.completed.set_result(None)
        else:
            self.completed.set_exception(exc)

    def npackets(self):
        """Return the number of packets sent during a test"""
        return int(math.ceil(self.args.duration * self.args.bw/self.args.len))

    async def iperf(self, testnum, whiten):
        if whiten:
            bs = bytearray(np.random.bytes(self.args.len-UDP_OVERHEAD))
        else:
            bs = bytearray(self.args.len-UDP_OVERHEAD)

        t_sleep = self.args.len/self.args.bw

        n = self.npackets()

        for seq in range (0, n):
            struct.pack_into('!hh', bs, 0, testnum, seq)
            self.transport.sendto(bs)
            await asyncio.sleep(t_sleep)

def runSweep(writer, radio, args, client):
    testnum = 0

    # Write header
    writer.writerow(('test', 'npackets', 'whiten', 'crc', 'fec0', 'fec1', 'ms', 'gain', 'auto gain', 'clip threshold'))

    # Load test configuration
    with open(args.test_config, 'r') as f:
        config = f.read()

    ldict = {}

    exec(config, globals(), ldict)

    params = ldict['params']

    # Run the tests
    for (w, crc, fec0, fec1, ms, g, c) in params:
        print('Running test:', (testnum, w, crc, fec0, fec1, ms, g, c), '...', end='', flush=True)

        radio.setTXParams(crc, fec0, fec1, ms, g, c)

        client.loop.run_until_complete(client.iperf(testnum, w))
        print('done')
        if g == 'auto':
            realg = radio.net.tx_params[0].soft_tx_gain_0dBFS
        else:
            realg = g

        writer.writerow((testnum, client.npackets(), w, crc, fec0, fec1, ms, realg, g == 'auto', c))
        testnum += 1

SUFFIX = { 'k': 1000, 'm': 1000000 }

def parseHuman(n):
    m = re.match('^([.\d]+)([kKmM]?)$', n)
    if not m:
        print('Cannot parse number %s', n, file=sys.stderr)
        return None

    return float(m.group(1))* SUFFIX[m.group(2).lower()]

def main():
    config = dragonradio.radio.Config()

    parser = argparse.ArgumentParser(description='Run dragonradio.',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    config.addArguments(parser)
    parser.add_argument('-n', action='store', type=int, dest='num_nodes',
                        default=2,
                        help='set number of nodes in network')
    parser.add_argument('--len', action='store', type=int, dest='len',
                        default=1500,
                        help='set default payload size')
    parser.add_argument('--bw', action='store', type=str, dest='bw',
                        default='500k',
                        help='set bandwidth')
    parser.add_argument('--duration', action='store', type=int, dest='duration',
                        default=10,
                        help='test duration')
    parser.add_argument('--test-config', action='store', type=str,
                        dest='test_config',
                        default='',
                        help='set test configuration')
    parser.add_argument('--server', action='store_true', dest='server',
                        default=False,
                        help='run iperf server')
    parser.add_argument('--client', action='store', type=str, dest='client',
                        default=None,
                        help='run iperf client')
    parser.add_argument('-o', '--output', action='store', type=str, dest='output',
                        default=None,
                        help='test output')

    # Parse arguments
    try:
        parser.parse_args(namespace=config)
    except SystemExit as ex:
        return ex.code

    # Set up logging
    logging.basicConfig(format='%(asctime)s:%(name)s:%(levelname)s:%(message)s',
                        level=config.loglevel)

    # Validate client/server arguments
    if not config.server and not config.client:
        parser.error('One of --client or --server must be specified')

    if config.client and not hasattr(config, 'test_config'):
        parser.error('the following arguments are required: --test-config')

    if config.log_directory:
        config.log_sources += ['log_recv_packets', 'log_sent_packets', 'log_events']

    # Parse human-readable bandwidth in bps and convert to Bps
    config.bw = parseHuman(config.bw)/8.0

    # Open output file
    if hasattr(config, 'output'):
        f = open(config.output, 'w')
        writer = csv.writer(f)
    else:
        f = None
        writer = csv.writer(sys.stdout)

    # Create the radio object
    radio = dragonradio.radio.Radio(config, 'tdma')

    #
    # Configure the MAC
    #
    for i in range(0, config.num_nodes):
        radio.net.addNode(i+1)

    radio.configureTDMA(len(radio.net))

    radio.mac.slots[radio.node_id - 1] = True

    #
    # Output parameters
    #
    writer.writerow(('# version %s' % dragonradio.__version__,))

    for attr in ['len', 'bw', 'duration']:
        writer.writerow(('# %s %s' % (attr, getattr(config, attr)),))

    for attr in ['rx_gain', 'tx_gain', 'phy', 'M', 'cp_len', 'taper_len']:
        writer.writerow(('# %s %s' % (attr, getattr(radio.config, attr)),))

    #
    # Run the iperf test
    #
    loop = asyncio.get_event_loop()

    if config.server:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(('', IPERF_PORT))
        listen = loop.create_datagram_endpoint(lambda : IperfServerProtocol(writer), sock=sock)
        transport, protocol = loop.run_until_complete(listen)

        try:
            loop.run_forever()
        except KeyboardInterrupt:
            pass

        transport.close()
        loop.close()
    elif config.client:
        client = IperfClientProtocol(config, loop)
        connect = loop.create_datagram_endpoint(lambda : client,
                                                remote_addr=(config.client, IPERF_PORT))
        transport, protocol = loop.run_until_complete(connect)

        runSweep(writer, radio, config, client)

        transport.close()
        loop.run_until_complete(client.completed)

        # Wait for the radio to drain
        time.sleep(1)

        loop.close()

    if f:
        f.close()

    return 0

if __name__ == '__main__':
    main()
