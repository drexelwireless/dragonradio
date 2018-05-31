import argparse
import IPython
import os
import signal
import sys

import dragonradio
import dragon.radio

def enumHelp(cls):
    return ', '.join(sorted(cls.__members__.keys()))

def main():
    parser = argparse.ArgumentParser(description='Run full-radio.',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-v', action='store_true', dest='verbose',
                        default=False,
                        help='set verbose mode')
    parser.add_argument('-l', action='store', dest='logfile',
                        help='log to file')
    parser.add_argument('--addr', action='store', dest='addr',
                        default='',
                        help='specify device address')
    parser.add_argument('--rx-antenna', action='store', dest='rx_antenna',
                        default='RX2',
                        help='set RX antenna')
    parser.add_argument('--tx-antenna', action='store', dest='tx_antenna',
                        default='TX/RX',
                        help='set TX antenna')
    parser.add_argument('--phy', action='store', dest='phy',
                        choices=['flexframe', 'ofdm', 'multiofdm'],
                        default='flexframe',
                        help='set PHY')
    parser.add_argument('-i', action='store', type=int, dest='node_id',
                        default=None,
                        help='set node ID')
    parser.add_argument('-n', action='store', type=int, dest='num_nodes',
                        default=2,
                        help='set number of nodes in network')
    parser.add_argument('-f', action='store', type=float, dest='frequency',
                        default=3e9,
                        help='set center frequency (Hz)')
    parser.add_argument('-b', action='store', type=float, dest='bandwidth',
                        default=5e6,
                        help='set bandwidth (Hz)')
    parser.add_argument('-g', '--soft-tx-gain',
                        action='store', type=float, dest='soft_tx_gain',
                        default=-12,
                        help='set soft TX gain (dB)')
    parser.add_argument('-G', '--tx-gain',
                        action='store', type=float, dest='tx_gain',
                        default=25,
                        help='set UHD TX gain (dB)')
    parser.add_argument('-R', '--rx-gain',
                        action='store', type=float, dest='rx_gain',
                        default=25,
                        help='set UHD RX gain (dB)')
    parser.add_argument('--auto-soft-tx-gain',
                        action='store_true', dest='auto_soft_tx_gain',
                        default=False,
                        help='automatically choose soft TX gain')
    parser.add_argument('-M',
                        action='store', type=int, dest='M',
                        default=48,
                        help='set number of OFDM subcarriers')
    parser.add_argument('-C', '--cp',
                        action='store', type=int, dest='cp_len',
                        default=6,
                        help='set OFDM cyclic prefix length')
    parser.add_argument('-T', '--taper',
                        action='store', type=int, dest='taper_len',
                        default=4,
                        help='set OFDM taper length')
    parser.add_argument('-m', '--mod',
                        action='store', type=dragonradio.ModulationScheme, dest='ms',
                        default='qpsk',
                        help='set modulation scheme: ' + enumHelp(dragonradio.ModulationScheme))
    parser.add_argument('-c', '--fec0',
                        action='store', type=dragonradio.FECScheme, dest='fec0',
                        default='v29',
                        help='set inner FEC: ' + enumHelp(dragonradio.FECScheme))
    parser.add_argument('-k', '--fec1',
                        action='store', type=dragonradio.FECScheme, dest='fec1',
                        default='rs8',
                        help='set outer FEC: ' + enumHelp(dragonradio.FECScheme))
    parser.add_argument('-r', '--check',
                        action='store', type=dragonradio.CRCScheme, dest='check',
                        default='crc32',
                        help='set data validity check: ' + enumHelp(dragonradio.CRCScheme))
    parser.add_argument('--interactive',
                        action='store_true', dest='interactive',
                        help='enter interactive shell after radio is configured')
    parser.add_argument('--arq', action='store_true', dest='arq',
                        default=False,
                        help='enable ARQ')
    parser.add_argument('--aloha', action='store_true', dest='aloha',
                        default=False,
                        help='use slotted ALOHA MAC')

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
