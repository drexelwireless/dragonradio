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
import zmq.asyncio

sys.path.append(os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), '../python'))

from dragon.protobuf import *
from dragon.collab import CollabAgent, Node
from dragon.gpsd import GPSDClient

class DummyController(object):
    def __init__(self, node_id, loop):
        node = Node(node_id)

        self.nodes = {node.id : node}
        self.gpsd = GPSDClient(node.loc, loop=loop)

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
