# Copyright 2018-2024 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

import asyncio
import logging

import dragonradio
import dragonradio.controller
import dragonradio.config
import dragonradio.radio
import dragonradio.tasks
from dragonradio.tasks import TaskManager

class Controller(TaskManager):
    config: dragonradio.config.Config
    """Radio configuration"""

    started: bool = False
    """Has the radio been started?"""

    def __init__(self, config: dragonradio.config.Config):
        super().__init__()

        self.config = config

    def mkRadio(self, *args, **kwargs):
        return dragonradio.controller.Radio(*args, **kwargs)

    def setupRadio(self, bootstrap: bool=False):
        self.startRadio()

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
        assert(radio.nhood is not None)
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

    async def stopRadio(self):
        """Stop the radio.

        This stops the radio, but the remote API will continue to function.
        """
        if self.radio is None:
            return

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
        await dragonradio.tasks.stopEventLoop(self.loop)
