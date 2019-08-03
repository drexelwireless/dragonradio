import argparse
import asyncio
from concurrent.futures import CancelledError
import IPython
from itertools import chain, cycle, starmap
import logging
import os
import random
import signal
import sys

import dragonradio
import dragon.radio

def cycle_algorithm(desc):
    if desc == 'sequential':
        return cycle(chain(range(25, -1, -1), range(1, 25, 1)))
    elif desc == 'discontinuous':
        return cycle(range(25, -1, -5))
    elif desc == 'random':
        def f():
            return random.randint(0, 25)

        return starmap(f, cycle([[]]))
    else:
        return []

async def cycle_tx_gain(radio, period, gains):
    try:
        for g in gains:
            radio.usrp.tx_gain = g
            print("Gain: ", radio.usrp.tx_gain)
            await asyncio.sleep(period)
    except CancelledError:
        pass

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

    parser = argparse.ArgumentParser(description='Run dragonradio.',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    config.addArguments(parser)
    parser.add_argument('-n', action='store', type=int, dest='num_nodes',
                        default=2,
                        help='set number of nodes in network')
    parser.add_argument('--aloha', action='store_true', dest='aloha',
                        default=False,
                        help='use slotted ALOHA MAC')
    parser.add_argument('--interactive',
                        action='store_true', dest='interactive',
                        help='enter interactive shell after radio is configured')
    parser.add_argument('--cycle-tx-gain', action='store',
                        choices=['sequential', 'discontinuous', 'random'],
                        help='enable TX gain cycling')
    parser.add_argument('--cycle-tx-gain-period', type=float,
                        default=10,
                        help='TX gain cycling period')

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

    # Create the radio object
    radio = dragon.radio.Radio(config)

    #
    # Configure the MAC
    #
    for i in range(0, config.num_nodes):
        radio.net.addNode(i+1)

    if config.aloha:
        radio.configureALOHA()
    else:
        radio.configureSimpleMACSchedule()

    #
    # Start IPython shell if we are in interactive mode. Otherwise, run the
    # event loop.
    #
    if config.interactive:
        IPython.embed()
    else:
        loop = asyncio.get_event_loop()

        if config.log_snapshots != 0:
            loop.create_task(radio.snapshotLogger())

        if config.cycle_tx_gain is not None:
            loop.create_task(cycle_tx_gain(radio,
                                           config.cycle_tx_gain_period,
                                           cycle_algorithm(config.cycle_tx_gain)))

        for sig in [signal.SIGINT, signal.SIGTERM]:
            loop.add_signal_handler(sig, cancel_loop)

        try:
            loop.run_forever()
        finally:
            loop.close()

    return 0

if __name__ == '__main__':
    main()
