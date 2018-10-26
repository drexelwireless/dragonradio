import argparse
import asyncio
from concurrent.futures import CancelledError
import IPython
import logging
import os
from pprint import pprint
import signal
import sys

import dragonradio
import dragon.radio

async def cycle_snr(radio, period):
    gains = [25, 20, 15, 10, 5, 0]
    i = 0

    while True:
        radio.usrp.tx_gain = gains[i % len(gains)]
        i += 1
        print("Gain: ", radio.usrp.tx_gain)
        await asyncio.sleep(period)

def cancel_loop():
    loop = asyncio.get_event_loop()
    for task in asyncio.Task.all_tasks():
        task.cancel()
    loop.stop()

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
    parser.add_argument('--simulate-cycle-snr', type=float, dest='cycle_snr',
                        default=0,
                        help='simulate cycling between SNR levels')

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
    if config.aloha:
        radio.configureALOHA()
    else:
        for i in range(0, config.num_nodes):
            radio.net.addNode(i+1)

        if config.fdma:
            radio.configureTDMA(1)

            if config.tx_channel != None:
                radio.mac.slots[0] = True
                radio.mac.tx_channel = config.tx_channel
            else:
                nodes = list(radio.net)
                sched = radio.defaultFDMASchedule(len(radio.channels), 3, nodes)

                if radio.node_id in sched:
                    idx = sched.index(radio.node_id)

                    radio.mac.slots[0] = True

                    radio.mac.tx_channel = idx
                else:
                    logging.error('No TX channel for radio %d (channels=%s)', idx, config.channels)
        else:
            idx = radio.node_id - 1

            radio.configureTDMA(len(radio.net))
            radio.mac.slots[idx] = True

            radio.mac.tx_channel = 0

    #
    # Start IPython shell if we are in interactive mode. Otherwise, run the
    # event loop.
    #
    if config.interactive:
        IPython.embed()
    else:
        loop = asyncio.get_event_loop()

        if config.cycle_snr != 0:
            loop.create_task(cycle_snr(radio, config.cycle_snr))

        for sig in [signal.SIGINT, signal.SIGTERM]:
            loop.add_signal_handler(sig, cancel_loop)

        try:
            loop.run_forever()
        finally:
            loop.close()

    return 0

if __name__ == '__main__':
    main()
