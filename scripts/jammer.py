# Copyright 2018-2024 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

import argparse
import asyncio
import logging
import time
from concurrent.futures import CancelledError
from typing import Optional, Sequence, Type

import numpy as np
from dragonradio.controller import Controller, Radio
from dragonradio.sc2 import Command
from dragonradio.sc2.config import SC2Config
from dragonradio.schedule import Schedule
from dragonradiocpp.radio import (
    MAC,
    FixedSignalSynthesizer,
    PHYChannels,
)

class RadioWarsRadio(Radio):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        # Start frequency hopping task
        self.createTask(self.jammer(),
                        name='jammer')

    def mkSynthesizer(self, mac_class: Type[MAC]):
        return FixedSignalSynthesizer(PHYChannels([]), self.usrp.tx_rate)

    def installSimpleMACSchedule(self, mac_class: Optional[Type[MAC]]=None):
        """Set a simple static MAC schedule."""
        nchannels: int = len(self.channels)
        nodes: Sequence[int]
        sched: Schedule

        assert self.nhood is not None
        nodes = sorted(list(self.nhood.neighbors))

        sched = self.greedySchedule(self.nhood.me.id)

        logging.debug("MAC schedule: %s", sched)

        self.installMACSchedule(sched, mac_class)

    def greedySchedule(self, node: int):
        return np.array([[node]], dtype=int)

    async def jammer(self):
        fc = self.frequency
        bw = self.bandwidth

        # How often to change jammer signal
        siggen_rate = 1.0

        # bandwidth of chirp signal (Hz)
        chirp_bw = 1e6

        # duration of chirp signal (sec)
        chirp_duration = 0.05

        logging.debug("Initial center frequency: %g", fc)
        logging.debug("Starting bandwidth: %g", bw)

        try:
            while True:
                fs = self.usrp.tx_rate
                f0 = fc - chirp_bw/2
                f1 = fc + chirp_bw/2
                sig = chirp(f0, f1, fs, chirp_duration)

                self.synthesizer.signal = sig

                # Sleep for siggen_rate seconds
                now = time.time()
                await asyncio.sleep(siggen_rate - now % siggen_rate)
        except CancelledError:
            pass
        except:
            logging.exception("frequencyHop: exception while frequency hopping")
            raise

class RadioWarsController(Controller):
    def mkRadio(self, *args, **kwargs):
        return RadioWarsRadio(*args, **kwargs)

class RadioWarsCommand(Command):
    def mkController(self):
        return RadioWarsController(self.config)

    def handle(self, parser: argparse.ArgumentParser, args: SC2Config) -> int:
        if args.log_directory:
            args.log_sources += ['log_recv_packets', 'log_sent_packets', 'log_events']

        args.bandwidth = 20e6
        args.channel_bandwidth = 20e6

        return super().handle(parser, args)

def chirp(f0: float, f1: float, Fs: float, T: float) -> np.ndarray:
    """Generate a (complex) chirp.

    Args:
        f0 (float): Initial frequency of tone (Hz).
        f1 (float): Final frequency of tone (Hz).
        Fs (float): Sampling rate (Hz).
        T (float): Duration of signal (sec).

    Returns:
        np.ndarray: Chirp signal (complex).
    """
    n = T*Fs
    c = (f1 - f0)/T

    samples = np.arange(n) / Fs
    return np.exp(2 * np.pi * 1j * (c/2 * samples**2 + f0 * samples))

def expchirp(f0: float, f1: float, Fs: float, T: float) -> np.ndarray:
    """Generate an expontential chirp.

    Args:
        Fc (float): Center frequency of tone.
        Fs (float): Sampling rate.
        T (float): Duration of signal (sec).

    Returns:
        np.ndarray: Chirp signal (complex).
    """
    n = T*Fs
    k = (f1/f0)**(1/T)

    samples = np.arange(n) / Fs
    return np.exp(2 * np.pi * 1j * f0 * (np.power(k, samples) - 1)/np.log(k)) + 0j

def main():
    config = SC2Config()

    # Default options
    config.mac = 'fdma'
    config.num_nodes = 2
    config.frequency = 1.3e9

    ## BEGIN modifications

    # Default options specific to SDR class
    config.rx_antenna = 'RX2'
    config.tx_antenna = 'TX/RX'
    config.tx_gain = 20
    config.rx_gain = 20
    config.auto_soft_tx_gain = 100
    config.bandwidth = 20e6
    config.channel_bandwidth = 20e6
    config.aloha_prob = 1/10
    config.slot_size = 0.050

    ## END modifications

    command = RadioWarsCommand(config=config)
    command.run()

if __name__ == '__main__':
    main()
