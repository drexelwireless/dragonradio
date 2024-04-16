# Copyright 2018-2024 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

import logging
from typing import List, Optional, Sequence, Type

import numpy as np

from dragonradio.controller import Controller, Radio
from dragonradio.sc2 import Command
from dragonradio.sc2.config import SC2Config
from dragonradio.schedule import Schedule
from dragonradiocpp.radio import MAC

class MyRadio(Radio):
    def __init__(self, *args, **kwargs):
        super.__init__(*args, **kwargs)

    def installSimpleMACSchedule(self, mac_class: Optional[Type[MAC]]=None):
        """Set a simple static MAC schedule."""
        nchannels: int = len(self.channels)
        nodes: Sequence[int]
        sched: Schedule

        assert self.nhood is not None
        nodes = sorted(list(self.nhood.neighbors))

        sched = self.pureTDMASchedule(nodes)

        logging.debug("TDMA schedule: %s", sched)

        self.installMACSchedule(sched, mac_class)

    def pureTDMASchedule(self, nodes: List[int]):
        """Create a pure TDMA schedule that gives each node a single slot.

        Args:
            nodes: The nodes

        Returns:
            A schedule consisting of a 1 X nslots array of node IDs.
        """
        nslots = len(nodes)
        sched = np.zeros((1, nslots), dtype=int)

        for i in range(0, len(nodes)):
            sched[0][i] = nodes[i]

        return sched

class Command(Command):
    def mkController(self):
        return Controller(self.config)

def main():
    config = SC2Config()

    # Default options
    config.mac = 'tdma'
    config.num_nodes = 2
    config.frequency = 1.3e9

    ## BEGIN modifications

    # Default options specific to SDR class
    config.rx_antenna = 'RX2'
    config.tx_antenna = 'TX/RX'
    config.tx_gain = 20
    config.rx_gain = 20
    config.auto_soft_tx_gain = 100
    config.bandwidth = 500e3
    config.channel_bandwidth = 500e3
    config.aloha_prob = 1/10
    config.slot_size = 0.050

    ## END modifications

    command = Command(config=config)
    command.run()

if __name__ == '__main__':
    main()
