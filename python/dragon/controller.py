import asyncio
import logging
import netifaces
import random
import subprocess
import sys

import dragonradio

from dragon.collab import CollabAgent
from dragon.protobuf import *
import dragon.dragonradio_pb2 as internal
import dragon.radio
import dragon.radio_api as radio_api

logger = logging.getLogger('controller')

@handler(internal.Request)
class Controller(TCPProtoServer):
    def __init__(self, config_args, config_path, colosseum_ini_path, *args, **kwargs):
        self.config_args = config_args
        self.config_path = config_path
        self.colosseum_ini_path = colosseum_ini_path
        self.state = internal.BOOTING
        self.started_discovery = False
        self.nodes = set()

    def setupRadio(self, start=False):
        # We cannot do this in __init__ because the controller is created
        # *before* we daemonize, and loop isn't valid after we fork
        self.loop = asyncio.get_event_loop()

        # Create the radio object
        radio = dragon.radio.Radio()
        self.radio = radio
        radio.loadConfig(self.config_path)
        radio.loadColosseumIni(self.colosseum_ini_path)
        radio.loadArgs(self.config_args)

        # Set center frequency and bandwidth. For now, we just use 5MHz, centered
        if hasattr(radio.config, 'center_frequency'):
            radio.config.frequency = radio.config.center_frequency
        else:
            logger.warning('Center frequency not specified; using %f', radio.config.frequency)
        radio.config.bandwidth = 5e6

        # Set up the radio
        radio.setup()

        # Add us as a node
        radio.net.addNode(radio.node_id)

        # See if we are a gateway, and if so, start the collaboration agent
        self.agent = None

        if radio.config.collab_iface in netifaces.interfaces():
            radio.net[radio.node_id].is_gateway = True
            collab_ip = netifaces.ifaddresses(radio.config.collab_iface)[netifaces.AF_INET][0]['addr']

            try:
                self.agent = CollabAgent(loop=self.loop,
                                         local_ip=collab_ip,
                                         server_host=radio.config.collab_server_ip,
                                         server_port=radio.config.collab_server_port,
                                         client_port=radio.config.collab_client_port,
                                         peer_port=radio.config.collab_peer_port)
            except:
                logger.exception('Could not create collaboration agent')

        # XXX we need *some* task to be running or else we run_forever can't be
        # stopped!
        self.loop.create_task(self.dummy())

        # Start the RPC server
        self.radio_api_server = self.startServer(internal.Request, radio_api.RADIO_API_HOST, radio_api.RADIO_API_PORT)
        self.loop.create_task(self.radio_api_server)

        # Start the radio if we've been asked to. Otherwise, we will start it up
        # later.
        if start:
            self.startRadio()

        self.state = internal.READY

        try:
            self.loop.run_forever()
        finally:
            logger.info('done running forever')
            self.loop.close()

    def startRadio(self):
        if not self.started_discovery:
            self.started_discovery = True
            self.loop.create_task(self.discoverNeighbors())

    def stopRadio(self):
        self.state = internal.STOPPING

        for node_id in list(self.nodes):
            self.removeNode(node_id)

        async def shutdownGracefully():
            if self.agent:
                logger.info('Leaving collaboration network...')
                try:
                    await self.agent.shutdown()
                except:
                    logger.exception('Could not gracefully terminate collaboration agent')
            logger.info('Shutting down...')
            self.loop.stop()
            self.state = internal.FINISHED

        logger.info('Cancelling tasks...')
        for task in asyncio.Task.all_tasks():
            task.cancel()

        self.loop.create_task(shutdownGracefully())

    async def dummy(self):
        while True:
            await asyncio.sleep(1)

    def addNode(self, node_id):
        if node_id != self.radio.node_id and node_id not in self.nodes:
            logger.info('Adding node %d', node_id)
            self.nodes.add(node_id)

            try:
                subprocess.check_call('ip route add 192.168.{:d}.0/24 via 10.10.10.{:d}'.format(node_id+100, node_id), shell=True)
            except:
                logger.exception('Could not add route to node {}'.format(node_id))

    def removeNode(self, node_id):
        if node_id != self.radio.node_id and node_id in self.nodes:
            logger.info('Removing node %d', node_id)

            try:
                subprocess.check_call('ip route del 192.168.{:d}.0/24'.format(node_id+100), shell=True)
            except:
                logger.exception('Could not remove route to node {}'.format(node_id))

            self.nodes.remove(node_id)

    async def discoverNeighbors(self):
        loop = self.loop
        radio = self.radio

        # Create ALOHA MAC for HELLO messages
        radio.configureALOHA()

        #
        # Perform neighbor discovery by periodically broadcasting HELLO messages for
        # neighbor_discovery_period seconds
        #
        t = loop.time();

        while True:
            delta = random.randint(1, 5)

            if loop.time() + delta > t + radio.config.neighbor_discovery_period:
                break

            await asyncio.sleep(delta)
            radio.controller.broadcastHello()

        # Get a sorted list of discovered nodes
        nodes = list(radio.net.keys())

        # Add discovered nodes
        for n in nodes:
            self.addNode(n)

        #
        # Now delete the ALOHA MAC and switch to TDMA
        #
        logger.info('Switching to TDMA MAC.')

        del radio.mac

        radio.configureTDMA(len(radio.net))

        # Sort nodes and pick our TDMA slot based on our position in the node
        # list
        nodes.sort()
        radio.mac[nodes.index(radio.node_id)] = True

        # We are now ready to transmit data
        self.state = internal.ACTIVE

    @handle('Request.radio_command')
    def radioCommand(self, req):
        info = ''

        if req.radio_command == internal.START:
            if self.state == internal.READY:
                self.startRadio()
                info = 'Radio started'
        elif req.radio_command == internal.STOP:
            if self.state == internal.READY or self.state == internal.ACTIVE:
                self.stopRadio()
                info = 'Radio stopping'

        resp = internal.Response()
        resp.status.state = self.state
        resp.status.info = info
        return resp

    @handle('Request.update_outcomes')
    def updateOutcomes(self, req):
        resp = internal.Response()
        resp.status.state = self.state
        resp.status.info = 'Mandated outcomes updated'
        return resp
