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
    parser.add_argument('-d', '--debug', action='store_const', const=logging.DEBUG,
                        dest='loglevel',
                        default=logging.WARNING,
                        help='print debugging information')
    parser.add_argument('-v', '--verbose', action='store_const', const=logging.INFO,
                        dest='loglevel',
                        help='be verbose')
    parser.add_argument('--config', action='store', dest='config_path',
                        default=None,
                        help='specify configuration file')
    parser.add_argument('-i', action='store', type=int, dest='node_id',
                        default=None,
                        help='set node ID')
    parser.add_argument('-n', action='store', type=int, dest='num_nodes',
                        default=2,
                        help='set number of nodes in network')
    parser.add_argument('--aloha', action='store_true', dest='aloha',
                        default=False,
                        help='use slotted ALOHA MAC')
    parser.add_argument('--log-iq',
                        action='store_true', dest='log_iq',
                        help='log IQ data')
    parser.add_argument('--interactive',
                        action='store_true', dest='interactive',
                        help='enter interactive shell after radio is configured')
    parser.add_argument('--simulate-cycle-snr', type=float, dest='cycle_snr',
                        default=0,
                        help='simulate cycling between SNR levels')

    # Parse arguments
    try:
        args = parser.parse_args()
    except SystemExit as ex:
        return ex.code

    # Set up logging
    logging.basicConfig(format='%(asctime)s:%(name)s:%(levelname)s:%(message)s',
                        level=args.loglevel)

    if args.loglevel <= logging.INFO:
        args.verbose = True

    if args.loglevel <= logging.DEBUG:
        args.debug = True

    if args.log_directory:
        args.log_sources = ['log_recv_packets', 'log_sent_packets', 'log_events']

        if args.log_iq:
            args.log_sources += ['log_slots', 'log_recv_data', 'log_sent_data']

    config.loadArgs(args)
    if hasattr(args, 'config_path'):
        config.loadConfig(args.config_path)

    # Create the radio object
    radio = dragon.radio.Radio(config)

    #
    # Configure the MAC
    #
    if args.aloha:
        radio.configureALOHA()
    else:
        for i in range(0, args.num_nodes):
            radio.net.addNode(i+1)

        if config.fdma or config.spaced_fdma:
            radio.configureTDMA(1)
            radio.mac.slots[0] = True

            if config.spaced_fdma:
                radio.mac.tx_channel = 2*(radio.node_id - 1)
            else:
                radio.mac.tx_channel = radio.node_id - 1
        else:
            radio.configureTDMA(len(radio.net))
            radio.mac.slots[radio.node_id - 1] = True

            radio.mac.tx_channel = 0

    #
    # Start IPython shell if we are in interactive mode. Otherwise, run the
    # event loop.
    #
    if args.interactive:
        IPython.embed()
    else:
        loop = asyncio.get_event_loop()

        if args.cycle_snr != 0:
            loop.create_task(cycle_snr(radio, args.cycle_snr))

        for sig in [signal.SIGINT, signal.SIGTERM]:
            loop.add_signal_handler(sig, cancel_loop)

        try:
            loop.run_forever()
        finally:
            loop.close()

    return 0

if __name__ == '__main__':
    main()
