import argparse
import IPython
import os
from pprint import pprint
import signal
import sys

import dragonradio
import dragon.radio

def main():
    config = dragon.radio.Config()

    parser = argparse.ArgumentParser(description='Run full-radio.',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    config.addArguments(parser)
    parser.add_argument('-i', action='store', type=int, dest='node_id',
                        default=None,
                        help='set node ID')
    parser.add_argument('-n', action='store', type=int, dest='num_nodes',
                        default=2,
                        help='set number of nodes in network')
    parser.add_argument('--aloha', action='store_true', dest='aloha',
                        default=False,
                        help='use slotted ALOHA MAC')
    parser.add_argument('--interactive',
                        action='store_true', dest='interactive',
                        help='enter interactive shell after radio is configured')
    parser.add_argument('-v', action='store_true', dest='verbose',
                        default=False,
                        help='set verbose mode')

    # Parse arguments
    try:
        args = parser.parse_args()
    except SystemExit as ex:
        return ex.code

    if args.log_directory:
        args.log_sources = ['log_slots',
                            'log_recv_packets', 'log_recv_data',
                            'log_sent_packets', 'log_sent_data']

    config.loadArgs(args)

    # Set parameters we don't configure from the command line
    config.min_packet_size = 512
    config.num_modulation_threads = 2
    config.num_demodulation_threads = 10
    config.arq_window = 1024
    config.slot_size = .035
    config.guard_size = .01
    config.aloha_prob = .1

    # Create the radio object
    radio = dragon.radio.Radio(config)

    # Setting the demodulator's ordered property to True forces packets to be
    # demodulated in order. This increases latency.
    #radio.demodulator.ordered = True

    #
    # Configure the MAC
    #
    if args.aloha:
        radio.configureALOHA()
    else:
        for i in range(0, args.num_nodes):
            radio.net.addNode(i+1)

        radio.configureTDMA(len(radio.net))

        radio.mac[radio.node_id - 1] = True

    #
    # Start IPython shell if we are in interactive mode
    #
    if args.interactive:
        IPython.embed()
    else:
        #
        # Wait for Ctrl-C
        #
        signal.sigwait([signal.SIGINT])

    return 0

if __name__ == '__main__':
    main()
