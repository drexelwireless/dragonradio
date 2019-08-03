import argparse
import asyncio
from concurrent.futures import CancelledError
import IPython
import logging
import os
import signal
import sys

import dragonradio
import dragon.radio

async def cycle_tx_gain(radio, period):
    gains = [25, 20, 15, 10, 5, 0]
    i = 0

    try:
        while True:
            radio.usrp.tx_gain = gains[i % len(gains)]
            i += 1
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
    parser.add_argument('--cycle-tx-gain', type=float,
                        default=0,
                        help='cycle between TX gain levels')

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

        if config.cycle_tx_gain != 0:
            loop.create_task(cycle_tx_gain(radio, config.cycle_tx_gain))

        for sig in [signal.SIGINT, signal.SIGTERM]:
            loop.add_signal_handler(sig, cancel_loop)

        try:
            loop.run_forever()
        finally:
            loop.close()

    return 0

if __name__ == '__main__':
    main()
