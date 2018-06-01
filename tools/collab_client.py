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
from dragon.collab import CollabAgent

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
    parser.add_argument('--server-host', action='store',
                        default='127.0.0.1',
                        help='set IP address for bind')

    # Parse arguments
    try:
        args = parser.parse_args()
    except SystemExit as ex:
        return ex.code

    # Set up logging
    logging.basicConfig(format='%(asctime)s:%(name)s:%(levelname)s:%(message)s', level=args.loglevel)

    loop = asyncio.get_event_loop()

    myip = netifaces.ifaddresses('eth0')[netifaces.AF_INET][0]['addr']

    agent = CollabAgent(loop=loop,
                        local_ip=myip,
                        server_host=args.server_host,
                        server_port=5556,
                        client_port=5557,
                        peer_port=5558)

    loop.add_signal_handler(signal.SIGINT, functools.partial(shutdown, loop, agent))

    loop.run_forever()
    loop.close()

if __name__ == '__main__':
    main()
