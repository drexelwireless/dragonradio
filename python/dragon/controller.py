import asyncio
import json
import logging
import netifaces
import os
import random
import signal
import subprocess
import sys

import dragonradio

from dragon.collab import CollabAgent, MandatedOutcome, Node
from dragon.gpsd import GPSDClient
from dragon.internal import InternalAgent
from dragon.protobuf import *
import dragon.radio
import dragon.remote as remote

logger = logging.getLogger('controller')

def internalNodeIP(node_id):
    """
    Return IP address of radio node on internal network
    """
    return '10.10.10.{:d}'.format(node_id)

def darpaNodeNet(node_id):
    """
    Return IP subnet of radio node on DARPA's network.
    """
    return '192.168.{:d}.0/24'.format(node_id+100)

@handler(remote.Request)
class Controller(TCPProtoServer):
    def __init__(self, config):
        self.config = config
        self.state = remote.BOOTING
        self.started_discovery = False
        self.dumpcap_procs = []

        self.nodes = {}
        self.voxels = []
        self.mandated_outcomes = {}

    def setupRadio(self, bootstrap=False):
        # We cannot do this in __init__ because the controller is created
        # *before* we daemonize, and loop isn't valid after we fork
        self.loop = asyncio.get_event_loop()

        # Set center frequency and bandwidth. For now, we just use 5MHz, centered
        if hasattr(self.config, 'center_frequency'):
            self.config.frequency = self.config.center_frequency
        else:
            logger.warning('Center frequency not specified; using %f', self.config.frequency)

        if hasattr(self.config, 'rf_bandwidth'):
            self.config.bandwidth = self.config.rf_bandwidth
        else:
            logger.warning('Bandwidth not specified; using %f', self.config.bandwidth)

        # Create the radio object
        radio = dragon.radio.Radio(self.config)
        self.radio = radio

        # Log snapshots if requested
        if self.config.log_snapshots != 0:
            self.loop.create_task(radio.snapshotLogger())

        # Capture interfaces
        for iface in self.config.log_interfaces:
            self.dumpcap(iface)

        # Add us as a node
        self.nodes[radio.node_id] = Node(radio.node_id)
        radio.net.addNode(radio.node_id)

        # Start reading GPS info and attach it to this node
        self.gpsd_client = GPSDClient(self.nodes[radio.node_id].loc, loop=self.loop)

        # See if we are a gateway, and if so, start the collaboration agent
        self.collab_agent = None

        if self.config.collab_iface in netifaces.interfaces() and self.config.collab_server_ip != None:
            radio.net[radio.node_id].is_gateway = True
            collab_ip = netifaces.ifaddresses(self.config.collab_iface)[netifaces.AF_INET][0]['addr']

            try:
                self.collab_agent = CollabAgent(self,
                                                loop=self.loop,
                                                local_ip=collab_ip,
                                                server_host=self.config.collab_server_ip,
                                                server_port=self.config.collab_server_port,
                                                client_port=self.config.collab_client_port,
                                                peer_port=self.config.collab_peer_port)
            except:
                logger.exception('Could not create collaboration agent')

        # We might also be forced to be the gateway...
        if self.config.force_gateway:
            radio.net[radio.node_id].is_gateway = True

        # Start the internal agent
        self.internal_agent = InternalAgent(self,
                                            loop=self.loop,
                                            local_ip=internalNodeIP(radio.node_id))

        # XXX we need *some* task to be running or else we run_forever can't be
        # stopped!
        self.loop.create_task(self.dummy())

        # Start the RPC server
        self.remote_server = self.startServer(remote.Request, remote.REMOTE_HOST, remote.REMOTE_PORT)
        self.loop.create_task(self.remote_server)

        # Bootstrap the radio if we've been asked to. Otherwise, we will not
        # bootstrap until a radio API client tells us to.
        if bootstrap:
            self.startRadio()

        self.state = remote.READY

        try:
            self.loop.run_forever()
        finally:
            logger.info('done running forever')
            self.loop.close()

    def startRadio(self):
        if not self.started_discovery:
            self.started_discovery = True

            # Create ALOHA MAC for HELLO messages
            self.radio.configureALOHA()

            self.switched_macs = False
            self.loop.create_task(self.discoverNeighbors())
            self.loop.create_task(self.switchToTDMA())

    def stopRadio(self):
        self.state = remote.STOPPING

        for p in self.dumpcap_procs:
            try:
                p.terminate()
                p.wait()
            except:
                logger.exception('Could not terminate PID %d', p.pid)

        if self.config.compress_interface_logs:
            # Compressing large interface logs takes too long
            xzprocs = []
            for iface in self.config.log_interfaces:
                if iface in netifaces.interfaces():
                    try:
                        p = subprocess.Popen('xz {logdir}/{iface}.pcapng'.format(iface=iface, logdir=self.config.logdir),
                                             stdin=None, stdout=None, stderr=None, close_fds=True, shell=True)
                        xzprocs.append(p)
                    except:
                        logging.exception('Could not xz {logdir}/{iface}.pcapng'.format(iface=iface, logdir=self.config.logdir))

            for p in xzprocs:
                try:
                    p.wait()
                except:
                    logger.exception('Failed to wait on xz PID %d', p.pid)

        for node_id in list(self.nodes):
            self.removeNode(node_id)

        async def shutdownGracefully():
            if self.collab_agent:
                logger.info('Leaving collaboration network...')
                try:
                    await self.collab_agent.shutdown()
                except:
                    logger.exception('Could not gracefully terminate collaboration agent')
            logger.info('Shutting down...')
            self.loop.stop()
            self.state = remote.FINISHED

        logger.info('Cancelling tasks...')
        for task in asyncio.Task.all_tasks():
            task.cancel()

        self.loop.create_task(shutdownGracefully())

    async def dummy(self):
        while True:
            await asyncio.sleep(1)

    def dumpcap(self, iface):
        if iface in netifaces.interfaces():
            p = subprocess.Popen('dumpcap -i {iface} -q -w {logdir}/{iface}.pcapng'.format(iface=iface, logdir=self.config.logdir),
                                 stdin=None, stdout=None, stderr=None, close_fds=True, shell=True)
            self.dumpcap_procs.append(p)

    def thisNode(self):
        return self.nodes[self.radio.node_id]

    def addNode(self, node_id):
        if node_id != self.radio.node_id and node_id not in self.nodes:
            logger.info('Adding node %d', node_id)
            self.nodes[node_id] = Node(node_id)

            if self.internal_agent and self.radio.net[node_id].is_gateway:
                self.internal_agent.startClient(internalNodeIP(node_id))

            try:
                subprocess.check_call('ip route add {} via {}'.format(darpaNodeNet(node_id), internalNodeIP(node_id)), shell=True)
            except:
                logger.exception('Could not add route to node {}'.format(node_id))

    def removeNode(self, node_id):
        if node_id != self.radio.node_id and node_id in self.nodes:
            logger.info('Removing node %d', node_id)

            try:
                subprocess.check_call('ip route del {}'.format(darpaNodeNet(node_id)), shell=True)
            except:
                logger.exception('Could not remove route to node {}'.format(node_id))

            del self.nodes[node_id]

    async def switchToTDMA(self):
        config = self.config
        radio = self.radio

        # Wait for initial discovery period to pass
        await asyncio.sleep(self.config.neighbor_discovery_period)

        # Get a sorted list of discovered nodes
        nodes = list(radio.net.keys())

        # Add discovered nodes
        for n in nodes:
            self.addNode(n)

        #
        # Now delete the ALOHA MAC and switch to TDMA
        #
        logger.info('Switching to TDMA MAC, nodes: %s', list(radio.net.keys()))
        self.switched_macs = True

        radio.deleteMAC()

        # Sort nodes and pick our slot/channel based on our position in the node
        # list
        radio.configureFDMATDMASchedule()

        #
        # Specify voxels
        #

        # Calculate center frequency and bandwidth
        bw = config.channel_bandwidth
        cf = radio.usrp.tx_frequency + radio.channels[radio.mac.tx_channel]

        self.voxels = [(cf-bw/2, cf+bw/2)]

        # We are now ready to transmit data
        self.state = remote.ACTIVE

    async def discoverNeighbors(self):
        loop = self.loop
        radio = self.radio

        #
        # Perform neighbor discovery by periodically broadcasting HELLO messages
        # for neighbor_discovery_period seconds
        #
        PERIOD = self.config.discovery_hello_interval

        t0 = loop.time();

        while True:
            delta = random.uniform(0.0, PERIOD)

            await asyncio.sleep(delta)

            if not self.switched_macs:
                radio.setTXChannel(random.randint(0, len(radio.channels)-1))

            radio.controller.broadcastHello()

            await asyncio.sleep(PERIOD - delta)

            if loop.time() > t0 + self.config.neighbor_discovery_period:
                break

        # Now change the period of our broadcasts and broadcast indefinitely
        PERIOD = self.config.standard_hello_interval

        while True:
            delta = random.uniform(0.0, PERIOD)

            await asyncio.sleep(delta)
            radio.controller.broadcastHello()
            await asyncio.sleep(PERIOD - delta)

    @handle('Request.radio_command')
    def radioCommand(self, req):
        info = ''

        if req.radio_command == remote.START:
            if self.state == remote.READY:
                self.startRadio()
                info = 'Radio started'
        elif req.radio_command == remote.STOP:
            if self.state == remote.READY or self.state == remote.ACTIVE:
                self.stopRadio()
                info = 'Radio stopping'

        resp = remote.Response()
        resp.status.state = self.state
        resp.status.info = info
        return resp

    @handle('Request.update_mandated_outcomes')
    def updateMandatedOutcomes(self, req):
        logger.info('Mandated outcomes:\n%s', req.update_mandated_outcomes.goals)

        self.mandated_outcomes = {}

        for goal in json.loads(req.update_mandated_outcomes.goals):
            outcome = MandatedOutcome(json=goal)
            self.mandated_outcomes[outcome.flow_uid] = outcome

        resp = remote.Response()
        resp.status.state = self.state
        resp.status.info = 'Mandated outcomes updated'
        return resp

    @handle('Request.update_environment')
    def updateEnvironment(self, req):
        logger.info('Environment:\n%s', req.update_environment.environment)

        envs = json.loads(req.update_environment.environment)
        # Environment messages contain a *list* of updates...
        for env in envs:
            bandwidth = env.get('scenario_rf_bandwidth', self.config.bandwidth)
            frequency = env.get('scenario_center_frequency', self.config.frequency)

            if bandwidth != self.config.bandwidth or frequency != self.config.frequency:
                self.radio.reconfigureBandwidthAndFrequency(bandwidth, frequency)

                # Delete old MAC since we are about to create a new MAC
                self.radio.deleteMAC()

                self.radio.configureFDMATDMASchedule()

        resp = remote.Response()
        resp.status.state = self.state
        resp.status.info = 'Environment updated'
        return resp

    @property
    def is_gateway(self):
        radio = self.radio

        return radio.net[radio.node_id].is_gateway
