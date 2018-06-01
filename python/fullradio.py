import argparse
import IPython
import os
import signal
import sys

import dragonradio
import dragon.radio

def main():
    parser = argparse.ArgumentParser(description='Run full-radio.',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    dragon.radio.addArguments(parser)
    parser.add_argument('-l', action='store', dest='logfile',
                        default=None,
                        help='log to file')
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

    # Create the radio object
    radio = dragon.radio.Radio()
    radio.loadArgs(args)

    # Set parameters we don't configure from the command line
    radio.config.min_packet_size = 512
    radio.config.num_modulation_threads = 2
    radio.config.num_demodulation_threads = 10
    radio.config.arq_window = 1024
    radio.config.slot_size = .035
    radio.config.guard_size = .01
    radio.config.aloha_prob = .1

    # Set up the radio
    radio.setup()

    # Enable soft gain
    if args.auto_soft_tx_gain:
        for node_id in radio.net:
            radio.net[node_id].desired_soft_tx_gain = 0.0

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

    #
    # Wait for Ctrl-C
    #
    signal.sigwait([signal.SIGINT])

    return 0

if __name__ == '__main__':
    main()
