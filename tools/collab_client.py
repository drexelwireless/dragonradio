#!/usr/bin/env python3
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

sys.path.append(os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), '../python'))

from dragon.protobuf import *
from dragon.collab import CollabAgent
from dragon.gpsd import GPSDClient, GPSLocation

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

class DummyController(object):
    def __init__(self, node_id, loop):
        node = Node(node_id)
        self.radio = DummyRadio(node_id)
        self.mandated_outcomes = {}
        self.scenario_started = False

        self.nodes = {node.id : node}
        self.gpsd = GPSDClient(node.loc, loop=loop)

        self.config = SimpleNamespace()
        self.config.location_update_period = 15
        self.config.spectrum_usage_update_period = 5
        self.config.detailed_performance_update_period = 5

    def getVoxels(self):
        return []

def shutdown(loop, agent):
    async def shutdownGracefully():
        logging.info('Leaving collaboration network...')
        await agent.shutdown()
        logging.info('Shutting down...')
        loop.stop()

    logging.info('Cancelling tasks...')
    for task in asyncio.Task.all_tasks():
        task.cancel()
    logging.info('Stopping loop...')
    loop.create_task(shutdownGracefully())

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

    myip = netifaces.ifaddresses(args.iface)[netifaces.AF_INET][0]['addr']

    controller = DummyController(args.id, loop)

    agent = CollabAgent(controller,
                        loop=loop,
                        local_ip=myip,
                        server_host=args.server_ip,
                        server_port=5556,
                        client_port=5557,
                        peer_port=5558)

    loop.add_signal_handler(signal.SIGINT, functools.partial(shutdown, loop, agent))

    loop.run_forever()
    loop.close()

if __name__ == '__main__':
    main()
