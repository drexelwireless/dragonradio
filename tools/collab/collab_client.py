#!/usr/bin/env python3
# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

import argparse
import asyncio
import functools
import logging
import netifaces
import os
import signal
import socket
import struct
import sys
from types import SimpleNamespace
import zmq.asyncio

from dragonradio.protobuf import *
from dragonradio.collab import CILServer
from dragonradio.gpsd import GPSDClient

class GPSLocation:
    """A GPS location"""
    # pylint: disable=too-few-public-methods

    def __init__(self):
        self.lat = 0
        """Latitude"""

        self.lon = 0
        """Longitude"""

        self.alt = 0
        """Altitude"""

        self.timestamp = 0
        """Timestamp of last update"""

    def __str__(self):
        return 'GPSLocation(lat={},lon={},alt={},timestamp={})'.\
            format(self.lat, self.lon, self.alt, self.timestamp)

class Node(object):
    def __init__(self, id):
        self.id = id
        self.loc = GPSLocation()

    def __str__(self):
        return 'Node(loc={})'.format(self.loc)

class DummyUSRP(object):
    def __init__(self):
        self.rx_gain = 0

class DummyRadio(object):
    def __init__(self, node_id):
        self.node_id = node_id
        self.usrp = DummyUSRP()

        self.mac = None

class DummyController(CILServer):
    def __init__(self, node_id, loop):
        CILServer.__init__(self)

        node = Node(node_id)

        self.loop = loop
        """Our event loop"""

        self.radio = DummyRadio(node_id)
        """Our Radio"""

        self.scenario_started = False
        """Have we received mandates?"""

        self.done = False
        """Is the radio done?"""

        self.nodes = {node.id : node}
        """Nodes in our network"""

        self.gpsd_client = GPSDClient(node.loc, loop=loop)
        """gpsd client"""

        self.config = SimpleNamespace()
        self.config.collab_server_port = 5556
        self.config.collab_client_port = 5557
        self.config.collab_peer_port = 5558
        self.config.location_update_period = 15
        self.config.spectrum_usage_update_period = 5
        self.config.detailed_performance_update_period = 5

    def stop(self):
        self.loop.create_task(self._stop())

    async def _stop(self):
        if not self.done:
            self.done = True

            # Stop collaboration server
            logger.info('Stopping collaboration server')
            await self.stopCollab()

            # Stop gpsd client
            logger.info('Stopping gpsd client')
            await self.gpsd_client.stop()

            # Stop event loop
            logging.info('Stopping event loop')
            self.loop.stop()

    def getVoxels(self):
        return []

def main():
    parser = argparse.ArgumentParser(description='Run CIL client.',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-d', '--debug', action='store_const', const=logging.DEBUG,
                        dest='loglevel',
                        default=logging.WARNING,
                        help='print debugging information')
    parser.add_argument('-v', '--verbose', action='store_const', const=logging.INFO,
                        dest='loglevel',
                        help='be verbose')
    parser.add_argument('--server-ip', action='store',
                        default='127.0.0.1',
                        help='specify IP address of collab server')
    parser.add_argument('-i', '--id', action='store', type=int,
                        default=1,
                        help='set node ID')
    parser.add_argument('--iface', action='store',
                        default='eth0',
                        help='set ethernet interface')

    # Parse arguments
    try:
        args = parser.parse_args()
    except SystemExit as ex:
        return ex.code

    # Set up logging
    logging.basicConfig(format='%(asctime)s:%(name)s:%(levelname)s:%(message)s', level=args.loglevel)

    loop = asyncio.get_event_loop()

    controller = DummyController(args.id, loop)

    controller.collab_ip = netifaces.ifaddresses(args.iface)[netifaces.AF_INET][0]['addr']
    controller.config.collab_server_ip = args.server_ip

    controller.startCollab()

    loop.add_signal_handler(signal.SIGINT, controller.stop)

    loop.run_forever()
    loop.close()

if __name__ == '__main__':
    main()
