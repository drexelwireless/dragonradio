# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

import asyncio
from concurrent.futures import CancelledError
from itertools import chain, cycle, starmap
import logging
import random

import dragonradio
import dragonradio.radio

class CycleTXGain:
    """Class for managing TX gain cycling"""
    def __init__(self, algorithm, period):
        self._algorithm = None
        """Gain-cycling algorithm"""

        self._generator = None
        """Gain-cycling algorithm"""

        self._new_generator = False
        """Use new generator"""

        self.algorithm = algorithm

        self.period = period
        """Gain-cycling period (sec)"""

        self.verbose = True
        """Be verbose"""

    @property
    def algorithm(self):
        """Gain-cycling algorithm. One of 'sequential', 'discontinuous', or 'random'"""
        return self._algorithm

    @algorithm.setter
    def algorithm(self, algorithm):
        new_generator = self._generator is not None

        if algorithm == 'sequential':
            self._generator = cycle(chain(range(25, -1, -1), range(1, 25, 1)))
        elif algorithm == 'discontinuous':
            self._generator = cycle(range(25, -1, -5))
        elif algorithm == 'random':
            def f():
                return random.randint(0, 25)

            self._generator = starmap(f, cycle([[]]))
        else:
            raise ValueError('Unknown gain cycling algorithm %s' % self.algorithm)

        self._algorithm = algorithm
        self._new_generator = new_generator

    async def cycleTXGainTask(self, radio):
        try:
            while True:
                for gain in self._generator:
                    # Switch to new generator if needed
                    if self._new_generator:
                        self._new_generator = False
                        break

                    radio.usrp.tx_gain = gain

                    if self.verbose:
                        print("Gain: ", radio.usrp.tx_gain)

                    await asyncio.sleep(self.period)

        except CancelledError:
            pass

def main():
    # Create configuration and set defaults
    config = dragonradio.radio.Config()

    config.mac = 'tdma'
    config.num_nodes = 2

    # Create command-line argument parser
    parser = config.parser()

    gain_cycle = parser.add_argument_group('TX Gain cycling')

    gain_cycle.add_argument('--cycle-tx-gain', action='store',
                            choices=['sequential', 'discontinuous', 'random'],
                            help='enable TX gain cycling')
    gain_cycle.add_argument('--cycle-tx-gain-period', type=float,
                            default=10,
                            metavar='SEC',
                            help='TX gain cycling period')

    # Parse arguments
    try:
        parser.parse_args(namespace=config)
    except SystemExit as err:
        return err.code

    # Set up logging
    logging.basicConfig(format='%(asctime)s:%(name)s:%(levelname)s:%(message)s',
                        level=config.loglevel)

    # If a log directory is set, log packets and events
    if config.log_directory:
        config.log_sources += ['log_recv_packets', 'log_sent_packets', 'log_events', 'log_arq_events']

    # Create the radio
    radio = dragonradio.radio.Radio(config, config.mac)

    # Start TX gain cycling task
    user_ns = {}

    if config.cycle_tx_gain is not None:
        tx_gain_cycler = CycleTXGain(config.cycle_tx_gain,
                                     config.cycle_tx_gain_period)
        user_ns['tx_gain_cycler'] = tx_gain_cycler

        radio.createTask(tx_gain_cycler.cycleTXGainTask(radio),
                         name='cycle TX gain')

    # Run the radio
    return radio.start(user_ns=user_ns)

if __name__ == '__main__':
    main()
