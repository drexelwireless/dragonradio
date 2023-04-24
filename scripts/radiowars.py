# Copyright 2018-2023 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

import asyncio
import io
import logging
import os
from pathlib import Path
import signal
import sys
import time
from typing import Optional

import daemon
import daemon.pidfile

import numpy as np

import dragonradio
import dragonradio.radio
from dragonradio.tasks import TaskManager

RADIOCONF_PATH: Path = Path('/root/radio_api/radio.conf')

## You may modify the MyRadio class as you see fit
class MyRadio(dragonradio.radio.Radio):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    def configureSimpleMACSchedule(self):
        """Set a simple static MAC schedule."""
        nchannels = len(self.channels)
        nodes = sorted(list(self.radionet.nodes))

        sched = self.pureTDMASchedule(nodes)

        logging.debug("TDMA schedule: %s", sched)

        self.installMACSchedule(sched)

    def pureTDMASchedule(self, nodes):
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

class Controller(TaskManager):
    config: dragonradio.radio.Config
    """Radio configuration"""

    started: bool=False
    """Has the radio started?"""

    def __init__(self, config: dragonradio.radio.Config):
        super().__init__(self)

        self.config = config

    def startRadio(self):
        """Set up the radio"""
        # We cannot do this in __init__ because the controller is created
        # *before* we daemonize, and loop isn't valid after we fork
        self.loop = asyncio.get_event_loop()

        # Set center frequency and bandwidth.
        if hasattr(self.config, 'center_frequency'):
            self.config.frequency = self.config.center_frequency

        if hasattr(self.config, 'rf_bandwidth'):
            self.config.bandwidth = self.config.rf_bandwidth

        # Create the radio object
        radio = self.mkRadio(self.config, self.config.mac, loop=self.loop)
        self.radio = radio

        # Collect snapshots if requested
        if self.config.snapshot_frequency is not None:
            radio.startSnapshots()

        # Add radio nodes to the network if number of nodes was specified
        if self.config.num_nodes is not None and not self.config.manet:
            for i in range(0, self.config.num_nodes):
                radio.nhood.addNeighbor(i+1)

        # Configure the MAC
        radio.configureMAC(self.config.mac_class)

        # Either start the interactive loop or run the loop ourselves
        user_ns = locals()
        user_ns['radio'] = self.radio
        user_ns['controller'] = self

        self.radio.run(finalizer=self.terminate, user_ns=user_ns)

        logging.info('Controller terminated')

    def mkRadio(self, *args, **kwargs):
        return MyRadio(*args, **kwargs)

    async def stopRadio(self):
        """Stop the radio.

        This stops the radio, but the remote API will continue to function.
        """
        try:
            # Stop tasks
            logging.info('Stopping tasks')
            await self.stopTasks()

            # Stop radio tasks
            logging.info('Stopping radio tasks')
            await self.radio.stopTasks()

            # Close the logger
            if self.radio.logger:
                self.radio.logger.close()

            logging.info('Radio stopped')
        except:
            logging.exception('Exception when stopping radio')

    def terminate(self):
        """Stop the controller and all associated tasks"""
        self.loop.create_task(self._terminate())

    async def _terminate(self):
        """Terminate the radio"""
        logging.debug('Terminating')
        await self.stopRadio()

        # Wait for remaining tasks and stop the event loop
        await dragonradio.tasks.stopEventLoop(self.loop, logging)

class SC2Config(dragonradio.radio.Config):
    foreground: bool = False
    "Run as a foreground process"

    colosseum_ini_path: str = '/root/radio_api/colosseum_config.ini'
    """Path to Colosseum .ini file"""

    pidfile: str = '/var/run/dragonradio.pid'
    """Path to Colosseum .ini file"""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    def getPid(self) -> Optional[int]:
        """Get the pid from the pidfile"""
        try:
            with io.open(self.pidfile, 'r') as f:
                return int(f.read().strip())
        except IOError:
            return None

    def configureLogging(self):
        logger = logging.getLogger()
        logger.setLevel(logging.DEBUG)
        formatter = logging.Formatter('%(asctime)s:%(name)s:%(levelname)s:%(message)s')
        logger.handlers = []

        protobuf_logger = logging.getLogger('protobuf')
        if self.log_protobuf:
            protobuf_logger.setLevel(logging.DEBUG)
        else:
            protobuf_logger.setLevel(logging.INFO)

        if self.foreground:
            # Set up python logger
            sh = logging.StreamHandler()
            sh.setFormatter(formatter)
            sh.setLevel(self.loglevel)
            logger.addHandler(sh)

            (fh, fout, ferr) = (None, None, None)
        else:
            logdir = self.logdir

            if logdir is None:
                raise ValueError('A log directory must be specified when running as a daemon. See the -l option.')

            # Set up python logger
            fh = logging.FileHandler(os.path.join(logdir, 'dragonradio.log'), mode='a')
            fh.setFormatter(formatter)
            fh.setLevel(self.loglevel)
            logger.addHandler(fh)

            # Open files to log stdout and stderr
            fout = io.open(os.path.join(logdir, 'stdout.log'), 'a')
            ferr = io.open(os.path.join(logdir, 'stderr.log'), 'a')

        return (fh, fout, ferr)

def run(config):
    # Configure logging
    (fh, fout, ferr) = config.configureLogging()

    # Create the controller
    controller = Controller(config)

    if config.foreground:
        controller.startRadio()
    else:
        # See:
        #   https://www.python.org/dev/peps/pep-3143/
        #   https://dpbl.wordpress.com/2017/02/12/a-tutorial-on-python-daemon/
        with daemon.DaemonContext(files_preserve=[fh.stream],
                                  stdout=fout,
                                  stderr=ferr,
                                  detach_process=True,
                                  prevent_core=False,
                                  pidfile=daemon.pidfile.TimeoutPIDLockFile(config.pidfile)):
            controller.startRadio()

    return 0

def start(config):
    if not config.foreground:
        pid = config.getPid()
        if pid:
            print('pidfile {} already exists. Daemon already running?\n'.format(config.pidfile), file=sys.stderr)
            return 1

    return run(config)

def stop(config):
    pid = config.getPid()
    if not pid:
        print('pidfile {} does not exist. Daemon not running?\n'.format(config.pidfile), file=sys.stderr)
        return 0

    # Try killing the daemon process
    try:
        os.kill(pid, signal.SIGTERM)
    except OSError as ex:
        print('Could not terminate daemon: {}\n'.format(str(ex)), file=sys.stderr)
        return 1

    return 0

def main():
    config = SC2Config()

    # Default options
    config.mac = 'tdma'
    config.num_nodes = 2
    config.frequency = 1.3e9

    ## BEGIN modifications

    # Default options specific to SDR class
    config.tx_antenna = 'TX/RX'
    config.rx_antenna = 'RX2'
    config.tx_gain = 25
    config.rx_gain = 25
    config.auto_soft_tx_gain = 100
    config.channel_bandwidth = 500e3
    config.bandwidth = 500e3
    config.aloha_prob = 1/10
    config.slot_size = 0.050

    ## END modifications

    ## DO NOT modify the code below
    parser = config.parser()

    sc2 = parser.add_argument_group('SC2 radio options')
    sc2.add_argument('--foreground', action='store_true', dest='foreground',
                     default=False,
                     help='run as a foreground process')
    sc2.add_argument('--colosseum-ini', type=Path, action='store', dest='colosseum_ini_path',
                     default=Path('/root/radio_api/colosseum_config.ini'),
                     help='specify Colosseum ini file')
    sc2.add_argument('--pidfile', type=str, action='store', dest='pidfile',
                     default=Path('/var/run/dragonradio.pid'),
                     help='specify PID file')

    parser.add_argument('action', choices=['start', 'stop'])

    if os.path.exists(RADIOCONF_PATH):
        config.load(RADIOCONF_PATH)

    try:
        parser.parse_args(namespace=config)
    except SystemExit as err:
        return err.code

    if os.path.exists(config.colosseum_ini_path):
        config.load(Path(config.colosseum_ini_path))

    if config.action == 'start':
        return start(config)
    elif config.action == 'stop':
        return stop(config)

if __name__ == '__main__':
    main()
