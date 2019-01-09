import asyncio
import json
import logging
import netifaces
import numpy as np
import os
import random
import signal
import subprocess
import sys

import dragonradio

from dragon.collab import CollabAgent, MandatedOutcome, Node, Voxel
from dragon.gpsd import GPSDClient
import dragon.internal
from dragon.internal import InternalProtoClient, InternalProtoServer
from dragon.protobuf import *
import dragon.radio
import dragon.remote as remote

logger = logging.getLogger('controller')

INTERNAL_BCAST_ADDR = '10.10.10.255'

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
        """Our Config"""

        self.dumpcap_procs = []
        """dumpcap procs we've started"""

        self.state = remote.BOOTING
        """Current radio state"""

        self.started = False
        """Has the radio been started?"""

        self.done = False
        """Is the radio done?"""

        self.bootstrapped = False
        """Has the radio already been bootstrapped?"""

        self.nodes = {}
        """Nodes in our network"""

        self.links = {}
        """Flows for each link, i.e., source destination pair, in the network"""

        self.schedule = None
        """Current TDMA schedule"""

        self.schedule_seq = 0
        """Current TDMA schedule sequence number."""

        self.schedule_nodes = []
        """Nodes in the TDMA schedule"""

        self.voxels = []
        """Voxels used"""

        self.mandated_outcomes = {}
        """Current mandated outcomes"""

        self.internal_server = None
        """Internal protocol server"""

        self.internal_client = None
        """Internal protocol client"""

    def setupRadio(self, bootstrap=False):
        # We cannot do this in __init__ because the controller is created
        # *before* we daemonize, and loop isn't valid after we fork
        self.loop = asyncio.get_event_loop()

        #
        # Create Event we can use to trigger TDMA scheduler
        #
        self.tdma_reschedule = asyncio.Event()

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

        # Start the internal protocol server
        self.internal_server = InternalProtoServer(self,
                                                   loop=self.loop,
                                                   local_ip='0.0.0.0')

        # If we are the gateway, start an internal protocol client connected to
        # the broadcast address
        if self.is_gateway:
            self.internal_client = InternalProtoClient(self,
                                                       loop=self.loop,
                                                       server_host=INTERNAL_BCAST_ADDR)

        # Start our local status update
        self.loop.create_task(self.localStatisticsUpdate())

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
        if not self.started:
            self.started = True

            # Create ALOHA MAC for HELLO messages
            self.radio.configureALOHA()

            self.loop.create_task(self.discoverNeighbors())
            self.loop.create_task(self.addDiscoveredNeighbors())
            self.loop.create_task(self.synchronizeClock())

            if self.is_gateway:
                self.loop.create_task(self.bootstrapNetwork())
                self.loop.create_task(self.distributeScheduleViaBroadcast())

            self.state = remote.ACTIVE

    def stopRadio(self):
        self.done = True
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
        self.tdma_reschedule.set()
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
            node = Node(node_id)
            self.nodes[node_id] = node

            # Add a route for the new node
            try:
                subprocess.check_call('ip route add {} via {}'.format(darpaNodeNet(node_id), internalNodeIP(node_id)), shell=True)
            except:
                logger.exception('Could not add route to node {}'.format(node_id))

            # If new node is a gateway, connect to it and start sending status
            # updates
            if self.radio.net[node_id].is_gateway:
                self.internal_client = InternalProtoClient(self,
                                                           loop=self.loop,
                                                           server_host=internalNodeIP(node_id))
                self.loop.create_task(self.gatewayStatisticsUpdate())

            # If we are the gateway, connect an internal protocol client to the
            # new node's server
            if self.is_gateway:
                node.internal_client = InternalProtoClient(self,
                                                           loop=self.loop,
                                                           server_host=internalNodeIP(node_id))

            # If we are the gateway, update the schedule
            if self.is_gateway:
                self.tdma_reschedule.set()

    def removeNode(self, node_id):
        if node_id != self.radio.node_id and node_id in self.nodes:
            logger.info('Removing node %d', node_id)

            try:
                subprocess.check_call('ip route del {}'.format(darpaNodeNet(node_id)), shell=True)
            except:
                logger.exception('Could not remove route to node {}'.format(node_id))

            del self.nodes[node_id]

    def addRadioNodes(self):
        """Add nodes discovered by the radio to our local list of nodes"""
        config = self.config
        radio = self.radio

        # Get a sorted list of discovered nodes
        nodes = list(radio.net.keys())

        # Add discovered nodes
        for n in nodes:
            self.addNode(n)

    def addLink(self, src, dest, flow_uid):
        """Record a link between nodes"""
        link = (src, dest)
        if link not in self.links:
            self.links[link] = [flow_uid]
        else:
            self.links[link].append([flow_uid])

    def updateMandateStats(self, flow_uid, latency, throughput, bytes):
        """Update local mandate with flow statistics"""
        mandate = self.mandated_outcomes.get(flow_uid, None)
        if mandate:
            mandate.latency = latency
            mandate.throughput = throughput
            mandate.bytes = bytes

    def installMACSchedule(self, seq, sched):
        """Install a new MAC schedule"""
        config = self.config
        radio = self.radio

        if self.schedule_seq is not None and seq <= self.schedule_seq:
            logger.info('Skipping schedule with sequence number %d (currently at %d)',
                seq, self.schedule_seq)

        if not np.array_equal(sched, self.schedule):
            (nchannels, nslots) = sched.shape

            if not self.bootstrapped:
                logger.info('Switching to TDMA MAC with %s slots', nslots)
                self.bootstrapped = True
                radio.deleteMAC()
                radio.configureTDMA(nslots)

            radio.installMACSchedule(sched)
            self.schedule = sched

        self.schedule_seq = seq

    def scheduleToVoxels(self, sched):
        """Determine voxel usage from schedule"""
        config = self.config
        radio = self.radio

        (nchannels, nslots) = sched.shape
        cbw = radio.channel_bandwidth

        voxels = []

        for chan in range(0, nchannels):
            transmitters = set(sched[chan])
            if 0 in transmitters:
                transmitters.remove(0)

            if transmitters != set():
                f_start = config.frequency + radio.channels[chan]-cbw/2
                f_end = config.frequency + radio.channels[chan]+cbw/2

                rx = radio.node_id

                for tx in transmitters:
                    v = Voxel()
                    v.f_start = f_start
                    v.f_end = f_end
                    v.tx = tx
                    v.rx = [rx]

                    voxels.append(v)

        return voxels

    async def localStatisticsUpdate(self):
        """Update local flow statistics"""
        radio = self.radio
        node_id = radio.node_id

        try:
            while not self.done:
                for flow_uid, stats in radio.flowsource.flows.items():
                    self.addLink(node_id, stats.dest, flow_uid)

                for flow_uid, stats in radio.flowsink.flows.items():
                    self.addLink(stats.src, node_id, flow_uid)
                    self.updateMandateStats(flow_uid,
                                            stats.latency.value,
                                            stats.throughput.value,
                                            stats.bytes)

                await asyncio.sleep(5)
        except CancelledError:
            pass

    async def gatewayStatisticsUpdate(self):
        """Send flow statistics update to gateway"""
        radio = self.radio
        config = self.config

        if self.is_gateway or config.status_update_period == 0:
            return

        try:
            while not self.done:
                await self.internal_client.sendStatus()
                await asyncio.sleep(config.status_update_period)
        except CancelledError:
            pass

    async def createSchedule(self):
        """Create a new TDMA schedule"""
        NSLOTS = 10

        config = self.config
        radio = self.radio

        while not self.done:
            await self.tdma_reschedule.wait()
            self.tdma_reschedule.clear()

            logging.debug('Creating schedule')

            # Get all nodes we know about
            self.schedule_nodes = list(radio.net)
            self.schedule_nodes.sort()

            # Make sure we are first in the list so we always get the same
            # channel
            self.schedule_nodes.remove(radio.node_id)
            self.schedule_nodes = [radio.node_id] + self.schedule_nodes

            # Create the schedule
            nchannels = len(radio.channels)
            sched = dragon.radio.fairMACSchedule(nchannels, NSLOTS, self.schedule_nodes, 3)
            if not np.array_equal(sched, self.schedule):
                self.installMACSchedule(self.schedule_seq + 1, sched)
                self.voxels = self.scheduleToVoxels(sched)
                await self.distributeSchedule()

    async def distributeSchedule(self):
        """Distribute the TDMA schedule to known nodes"""
        config = self.config
        radio = self.radio

        if self.schedule is None:
            return

        # Only distribute the MAC schedule to nodes with a slot in the schedule.
        # Otherwise we get stuck waiting for an ACK to our SYN and the
        # destination will never have an opportunity to see a later packet
        # containing the schedule.
        nodes_with_slot = set(self.schedule.flatten())
        if 0 in nodes_with_slot:
            nodes_with_slot.remove(0)

        for node_id in nodes_with_slot:
            node = self.nodes[node_id]
            if hasattr(node, 'internal_client'):
                await node.internal_client.sendSchedule(self.schedule_seq,
                                                        self.schedule_nodes,
                                                        self.schedule)

        # Now broadcast a few times for robustness
        for i in range(0, 10):
            await asyncio.sleep(0.5)
            await self.broadcastSchedule()

    async def distributeScheduleViaBroadcast(self):
        """Distribute the TDMA schedule via broadcast"""

        while not self.done:
            await asyncio.sleep(10)
            await self.broadcastSchedule()

    async def broadcastSchedule(self):
        """Broadcast the TDMA schedule"""
        config = self.config
        radio = self.radio

        if self.schedule is None:
            return

        await self.internal_client.sendSchedule(self.schedule_seq,
                                                self.schedule_nodes,
                                                self.schedule)

    async def bootstrapNetwork(self):
        loop = self.loop
        config = self.config
        radio = self.radio

        # Sleep for the discovery interval
        await asyncio.sleep(config.neighbor_discovery_period)

        # Start the schedule creation task
        self.loop.create_task(self.createSchedule())

        # Trigger TDMA scheduler
        self.tdma_reschedule.set()

    async def discoverNeighbors(self):
        loop = self.loop
        radio = self.radio

        #
        # Perform neighbor discovery by periodically broadcasting HELLO messages
        #
        while not self.done:
            if self.bootstrapped:
                period = self.config.standard_hello_interval
            else:
                period = self.config.discovery_hello_interval

            delta = random.uniform(0.0, period)

            await asyncio.sleep(delta)

            if not self.bootstrapped:
                radio.setTXChannel(random.randint(0, len(radio.channels)-1))

            radio.controller.broadcastHello()

            await asyncio.sleep(period - delta)

    async def addDiscoveredNeighbors(self):
        """Periodically add nodes discovered by the radio to our local list of nodes"""
        while not self.done:
            await asyncio.sleep(1)
            self.addRadioNodes()

    async def synchronizeClock(self):
        radio = self.radio
        config = self.config

        while not self.done:
            await asyncio.sleep(config.clock_sync_interval)

            radio.synchronizeClock()

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

        radio = self.radio

        # Update mandated outcomes
        mandates = {}

        for goal in json.loads(req.update_mandated_outcomes.goals):
            outcome = MandatedOutcome(json=goal)
            mandates[outcome.flow_uid] = outcome

        self.setMandatedOutcomes(mandates)

        resp = remote.Response()
        resp.status.state = self.state
        resp.status.info = 'Mandated outcomes updated'
        return resp

    def setMandatedOutcomes(self, mandates):
        """Set our mandated outcomes and update the flowsink and flowsource"""
        config = self.config
        radio = self.radio

        self.mandated_outcomes = mandates

        # Create a MandatedOutcomeMap for flow source and sink components
        mandateMap = dragonradio.MandatedOutcomeMap()

        for (flow, m) in mandates.items():
            mandateMap[flow] = dragonradio.MandatedOutcome(config.measurement_period,
                                                           0.0,
                                                           m.min_throughput_bps,
                                                           m.max_latency_s,
                                                           m.file_transfer_deadline_s)


        radio.flowsink.mandates = mandateMap
        radio.flowsource.mandates = mandateMap

        self.setAllowedFlows()

    def setAllowedFlows(self):
        """Decide which flows are allowed"""
        config = self.config
        radio = self.radio

        allowed = set([dragon.internal.INTERNAL_PORT])

        for (flow, m) in self.mandated_outcomes.items():
            allowed.add(flow)

        radio.netfirewall.allow_broadcasts = True
        radio.netfirewall.allowed = allowed
        radio.netfirewall.enabled = True

    @handle('Request.update_environment')
    def updateEnvironment(self, req):
        logger.info('Environment:\n%s', req.update_environment.environment)

        envs = json.loads(req.update_environment.environment)
        # Environment messages contain a *list* of updates...
        for env in envs:
            bandwidth = env.get('scenario_rf_bandwidth', self.config.bandwidth)
            frequency = env.get('scenario_center_frequency', self.config.frequency)

            if bandwidth != self.config.bandwidth or frequency != self.config.frequency:
                old_bandwidth = self.radio.bandwidth

                self.radio.reconfigureBandwidthAndFrequency(bandwidth, frequency)

                # If only the center frequency has changed, keep the old
                # schedule. Otherwise create a new schedule.
                if self.radio.bandwidth != old_bandwidth:
                    if self.bootstrapped:
                        self.radio.mac.slots = []

                    if self.is_gateway:
                        # Force new schedule
                        self.schedule = None
                        self.tdma_reschedule.set()
                else:
                    # We need to re-set the channel after a frequency change
                    # because although the channel number may be the same, the
                    # corresponding frequency will be different.
                    self.radio.setTXChannel(self.radio.tx_channel)

        resp = remote.Response()
        resp.status.state = self.state
        resp.status.info = 'Environment updated'
        return resp

    @property
    def is_gateway(self):
        radio = self.radio

        return radio.net[radio.node_id].is_gateway
