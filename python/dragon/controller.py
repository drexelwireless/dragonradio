import asyncio
import json
import logging
import netifaces
import numpy as np
import os
import pandas as pd
import random
import signal
import subprocess
import sys
import threading
import time

import dragonradio

from dragon.collab import CollabAgent, MandatedOutcome, Node, Voxel
from dragon.gpsd import GPSDClient
import dragon.internal
from dragon.internal import InternalProtoClient, InternalProtoServer
from dragon.protobuf import *
import dragon.radio
import dragon.remote as remote
import dragon.schedule
import dragon.scoring as scoring

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

        self.radio = None
        """Our Radio"""

        self.dumpcap_procs = []
        """dumpcap procs we've started"""

        self.state = remote.BOOTING
        """Current radio state"""

        self.started = False
        """Has the radio been started?"""

        self.scenario_started = False
        """Have we received mandates?"""

        self.__scenario_start_time = None
        """RF scenario start time, in seconds since the epoch"""

        self.done = False
        """Is the radio done?"""

        self.bootstrapped = False
        """Has the radio already been bootstrapped?"""

        self.nodes = {}
        """Nodes in our network"""

        self.scorer = scoring.Scorer(config)
        """Match scorer"""

        self.max_reported_mp = 0
        """Maximum MP for which flow statistics have been reported"""

        self.reported_mandate_performance = []
        """Reported mandate performance"""

        self.schedule = None
        """Current TDMA schedule"""

        self.schedule_seq = 0
        """Current TDMA schedule sequence number."""

        self.schedule_nodes = []
        """Nodes in the TDMA schedule"""

        self.voxels = []
        """Voxels used"""

        self.mandated_outcomes_lock = threading.Lock()
        """Lock for mandated outcomes"""

        self.mandated_outcomes = {}
        """Current mandated outcomes"""

        self.stage_mandated_outcomes = {}
        """Mandated outcomes for given stage"""

        self.scoring_percent_threshold = 0
        """Scoring percent threshold"""

        self.scoring_point_threshold = 0
        """Scoring point threshold"""

        self.internal_server = None
        """Internal protocol server"""

        self.internal_client = None
        """Internal protocol client"""

        # Provide default start time
        self.scenario_start_time = time.time()

    @property
    def is_gateway(self):
        radio = self.radio

        return radio.net.nodes[radio.node_id].is_gateway

    @property
    def scenario_start_time(self):
        """RF scenario start time, in seconds since the epoch"""
        return self.__scenario_start_time

    @scenario_start_time.setter
    def scenario_start_time(self, t):
        logging.info('RF scenario start time set: %f', t)
        self.__scenario_start_time = t
        self.scorer.scenario_start_time = t
        if self.radio is not None:
            self.radio.flowperf.start = t

    def timeToMP(self, t):
        """Convert time (in seconds since the epoch) to a measurement period"""
        return int((t - self.scenario_start_time) / self.config.measurement_period)

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
            radio.net.nodes[radio.node_id].is_gateway = True
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
            radio.net.nodes[radio.node_id].is_gateway = True

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
        self.loop.create_task(self.updateAllFlowStatistics())

        # XXX we need *some* task to be running or else we run_forever can't be
        # stopped!
        self.loop.create_task(self.dummy())

        # Start the RPC server
        self.remote_server = self.startServer(remote.Request, remote.REMOTE_HOST, remote.REMOTE_PORT)
        self.loop.create_task(self.remote_server)

        # Bootstrap the radio if we've been asked to. Otherwise, we will not
        # bootstrap until a radio API client tells us to.
        if bootstrap:
            self.loop.create_task(self.startRadio())

        self.state = remote.READY

        try:
            self.loop.run_forever()
        finally:
            logger.info('done running forever')
            self.loop.close()

    async def startRadio(self, timestamp=time.time()):
        if not self.started:
            logging.info('Starting radio: now=%f; timestamp=%f',
                time.time(),
                timestamp)

            # Start task to get traffic interface addresses into ARP cache
            self.loop.create_task(self.cacheTrafficInterfaceARP())

            with await self.radio.lock:
                self.started = True
                self.scenario_start_time = timestamp

                # Start the scorer
                self.scorer.start()

                # Create ALOHA MAC for HELLO messages
                self.radio.configureALOHA()

                self.loop.create_task(self.discoverNeighbors())
                self.loop.create_task(self.addDiscoveredNeighbors())
                self.loop.create_task(self.synchronizeClock())

                if self.is_gateway:
                    self.loop.create_task(self.bootstrapNetwork())
                    self.loop.create_task(self.distributeScheduleViaBroadcast())

                self.state = remote.ACTIVE

    async def stopRadio(self):
        if not self.done:
            # Update score one final time
            if self.is_gateway:
                self.scorer.updateScore()

            # Stop the scorer
            try:
                self.scorer.stop()
            except:
                logger.exception('Could not gracefully terminate scorer')

            # Dump score data if we are the gateway
            if self.is_gateway:
                try:
                    # Dump scoring data one last time
                    self.scorer.dumpScores(final=True)

                    # Dump reported scores
                    self.saveReportedMandatePerformance()
                except:
                    logger.exception('Could not dump scoring data')

            with await self.radio.lock:
                self.done = True
                self.state = remote.STOPPING

                #
                # Stop dumpcap processes
                #
                for p in self.dumpcap_procs:
                    try:
                        p.terminate()
                        p.wait()
                    except:
                        logger.exception('Could not terminate PID %d', p.pid)

                #
                # Compress pcap files
                #
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

                #
                # Remove all nodes
                #
                for node_id in list(self.nodes):
                    self.removeNode(node_id)

                #
                # Leave the collaboration network
                #
                if self.collab_agent:
                    logger.info('Leaving collaboration network...')
                    try:
                        await self.collab_agent.shutdown()
                    except:
                        logger.exception('Could not gracefully terminate collaboration agent')

                #
                # Cancel all remaining tasks
                #
                tasks = [t for t in asyncio.Task.all_tasks() if t is not asyncio.Task.current_task()]

                logger.info('Cancelling tasks...')
                [task.cancel() for task in tasks]

                logging.info('Waiting for outstanding tasks')
                await asyncio.gather(*tasks)

                #
                # Stop the event loop
                #
                logger.info('Terminating event loop...')
                self.loop.stop()
                self.state = remote.FINISHED
                logging.info('Shutdown complete.')

    async def dummy(self):
        while True:
            try:
                await asyncio.sleep(1)
            except CancelledError:
                return

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
            if self.radio.net.nodes[node_id].is_gateway:
                self.internal_client = InternalProtoClient(self,
                                                           loop=self.loop,
                                                           server_host=internalNodeIP(node_id))

            # If we are the gateway, connect an internal protocol client to the
            # new node's server
            if self.is_gateway:
                node.internal_client = InternalProtoClient(self,
                                                           loop=self.loop,
                                                           server_host=internalNodeIP(node_id))

            # If we are the gateway, update the schedule
            if self.is_gateway:
                self.tdma_reschedule.set()

            # Log ARP table
            result = subprocess.run(['arp', '-an'], stdout=subprocess.PIPE)
            logger.info('ARP table:\n%s', result.stdout.decode('utf-8'))

            # Log routes
            result = subprocess.run(['ip', 'route'], stdout=subprocess.PIPE)
            logger.info('Routing table:\n%s', result.stdout.decode('utf-8'))

            # Log ip links
            result = subprocess.run(['ip', 'a'], stdout=subprocess.PIPE)
            logger.info('IP links:\n%s', result.stdout.decode('utf-8'))

            # Log routes
            for node_id in [self.radio.node_id, node_id]:
                for octet in [1]:
                    ipaddr = '192.168.{:d}.{:d}'.format(node_id+100, octet)
                    result = subprocess.run(['ip', 'route', 'get', ipaddr], stdout=subprocess.PIPE)
                    logger.info('IP route for %s:\n%s', ipaddr, result.stdout.decode('utf-8'))

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
        nodes = list(radio.net.nodes)

        # Add discovered nodes
        for n in nodes:
            self.addNode(n)

    async def installMACSchedule(self, seq, sched):
        """Install a new MAC schedule"""
        with await self.radio.lock:
            config = self.config
            radio = self.radio

            if self.schedule_seq is not None and seq <= self.schedule_seq:
                logger.info('Skipping schedule with sequence number %d (currently at %d)',
                    seq, self.schedule_seq)
                return

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

            if len(transmitters) != 0:
                f_start = config.frequency + radio.channels[chan].fc-cbw/2
                f_end = config.frequency + radio.channels[chan].fc+cbw/2

                rx = radio.node_id

                for tx in transmitters:
                    occupancy = (sched[chan] == tx).sum() / len(sched[chan])

                    v = Voxel()
                    v.f_start = f_start
                    v.f_end = f_end
                    v.tx = tx
                    v.rx = [rx]
                    v.duty_cycle = occupancy

                    voxels.append(v)

        return voxels

    async def cacheTrafficInterfaceARP(self):
        """Get all addresses on the traffic interface's subnet into the ARP
        cache.

        See:
            https://gitlab.com/darpa-sc2-phase3/CIL/issues/15
        """
        try:
            IFACE = 'tr0'

            node_id = self.radio.node_id

            if IFACE in netifaces.interfaces():
                procs = []

                for i in range (1, 255):
                    ip = '192.168.{:d}.{:d}'.format(node_id+100, i)

                    p = await asyncio.create_subprocess_exec(
                            'ping',
                            '-c', '1',
                            ip,
                            stdout=asyncio.subprocess.PIPE,
                            stderr=asyncio.subprocess.PIPE)
                    procs.append((p, ip))

                for (p, ip) in procs:
                    # Wait for the subprocess to finish
                    stdout, stderr = await p.communicate()

                    result = stdout.decode().strip()

                    if p.returncode == 0:
                        logging.info('ping %s succeeded:\n%s', ip, result)
                    else:
                        logging.info('ping %s failed:\n%s', ip, result)
        except CancelledError:
            return

    async def getMandatePerformance(self):
        config = self.config

        # Drain the scoring task queue
        #await self.loop.run_in_executor(None, self.scorer.join)

        # Update scoring data
        await self.loop.run_in_executor(None, self.scorer.updateScore)

        # Determine which measurement period to use. We use the latest MP for
        # which we have data from all nodes, except if it is older than
        # config.max_performance_age seconds ago, in which case we extrapolate.
        known_mps = self.scorer.stats_max_mp.values()
        if len(known_mps) == 0:
            min_known_mp = 0
        else:
            min_known_mp = min(self.scorer.stats_max_mp.values())

        min_mp = self.timeToMP(time.time() - config.max_performance_age)

        mp = max(min_known_mp, min_mp)
        timestamp = self.scenario_start_time + mp*config.measurement_period
        stage = self.scorer.getMPStage(mp)
        if stage == 0:
            stage = 1

        logger.info('getMandatePerformance: min_known_mp=%d; min_mp=%d; mp=%d; stage=%d',
            min_known_mp,
            min_mp,
            mp,
            stage)

        # Updated mandated outcomes from the given measureement period
        with self.mandated_outcomes_lock:
            mandated_outcomes = self.stage_mandated_outcomes[stage]

            # Get scorer to update our mandated outcomes
            await self.loop.run_in_executor(None, self.scorer.updateMandatedOutcomes,
                                            mp,
                                            mandated_outcomes)

            # Build performance report
            mandates_achieved = 0
            total_score_achieved = 0
            performance = []

            for mandate in mandated_outcomes.values():
                perf = scoring.MandatePerformance(mandate.scalar_performance,
                                                  mandate.radio_ids,
                                                  mandate.flow_uid,
                                                  mandate.hold_period,
                                                  mandate.achieved_duration,
                                                  mandate.point_value)

                if mandate.achieved_duration >= mandate.hold_period:
                    mandates_achieved += 1
                    total_score_achieved += mandate.point_value

                performance.append(perf)

        # Log the reported score
        self.reported_mandate_performance.append((mp, mandates_achieved, total_score_achieved))

        # Return performance metrics
        return (mp, timestamp, mandates_achieved, total_score_achieved, performance)

    def saveReportedMandatePerformance(self):
        """Save reported performance"""
        config = self.config

        if self.is_gateway and config.log_directory:
            try:
                df = pd.DataFrame(self.reported_mandate_performance,
                                  columns=['mp', 'mandates_achieved', 'total_score_achieved'])
                logging.info('Logging scores to %s', 'score_reported.csv')
                df.to_csv(os.path.join(config.logdir, 'score_reported.csv'))
            except:
                logging.exception('Exception when saving reported mandated performance')

    def updateGoals(self, goals, timestamp):
        logging.debug('Updating goals')

        # Dump current scores and update scorer's goals
        if self.is_gateway:
            self.scorer.dumpScores()

        self.scorer.updateGoals(goals, timestamp)

        # Update mandated outcomes
        mandates = {}

        for goal in goals:
            outcome = MandatedOutcome(json=goal)
            mandates[outcome.flow_uid] = outcome

        self.setMandatedOutcomes(mandates)

        # We have mandates!
        self.scenario_started = True

    async def updateAllFlowStatistics(self):
        """Update all flow statistics"""
        config = self.config
        radio = self.radio
        node_id = radio.node_id

        while not self.done:
            try:
                if self.scenario_started:
                    if self.is_gateway or self.internal_client is not None:
                        reset_stats = True
                    else:
                        reset_stats = False

                    # This is the maximum MP for which we will report flow
                    # statistics
                    max_report_mp = self.timeToMP(time.time() - config.stats_ignore_window)

                    # Get local flow statistics
                    sources = [scoring.mkFlowStats(p, self.max_reported_mp + 1, max_report_mp) for p in radio.flowperf.getSources(reset_stats).values()]
                    sinks = [scoring.mkFlowStats(p, self.max_reported_mp + 1, max_report_mp) for p in radio.flowperf.getSinks(reset_stats).values()]

                    self.max_reported_mp = max_report_mp

                    # Filter out
                    sources = [p for p in sources if scoring.nonzeroFlowStats(p)]
                    sinks = [p for p in sinks if scoring.nonzeroFlowStats(p)]

                    self.scorer.updateSourceStats(node_id,
                                                  time.time(),
                                                  sources)
                    self.scorer.updateSinkStats(node_id,
                                                time.time(),
                                                sinks)

                    # Send flow statistics to the gateway
                    if not self.is_gateway and self.internal_client is not None:
                        await self.internal_client.sendStatus(sources, sinks)

                await asyncio.sleep(config.status_update_period)
            except CancelledError:
                return
            except:
                logging.exception('Exception in updateAllFlowStatistics')

    async def createSchedule(self):
        """Create a new TDMA schedule"""
        NSLOTS = 10

        config = self.config
        radio = self.radio

        while not self.done:
            try:
                await self.tdma_reschedule.wait()
                self.tdma_reschedule.clear()

                logging.debug('Creating schedule')

                # Make sure we know about all nodes
                self.addRadioNodes()

                # Get all nodes we know about
                self.schedule_nodes = list(self.nodes.keys())
                self.schedule_nodes.sort()

                # Make sure we are first in the list so we always get the same
                # channel
                self.schedule_nodes.remove(radio.node_id)
                self.schedule_nodes = [radio.node_id] + self.schedule_nodes

                # Create the schedule
                nchannels = len(radio.channels)
                sched = dragon.schedule.fairMACSchedule(nchannels, NSLOTS, self.schedule_nodes, 3)
                if not np.array_equal(sched, self.schedule):
                    await self.installMACSchedule(self.schedule_seq + 1, sched)
                    self.voxels = self.scheduleToVoxels(sched)
                    await self.distributeSchedule()
            except CancelledError:
                return

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
            try:
                await asyncio.sleep(10)
                await self.broadcastSchedule()
            except CancelledError:
                return

    async def broadcastSchedule(self):
        """Broadcast the TDMA schedule"""
        config = self.config
        radio = self.radio

        if self.schedule is None:
            return

        await self.internal_client.sendSchedule(self.schedule_seq,
                                                self.schedule_nodes,
                                                self.schedule)

    async def reconfigureBandwidthAndFrequency(self, bandwidth, frequency):
        """Reconfigure bandwidth and frequency.

        If we are the gateway node, this will trigger a new TDMA schedule.
        """
        with await self.radio.lock:
            if bandwidth != self.config.bandwidth or frequency != self.config.frequency:
                old_bandwidth = self.radio.bandwidth

                self.radio.reconfigureBandwidthAndFrequency(bandwidth, frequency)

                # If only the center frequency has changed, keep the old
                # schedule. Otherwise create a new schedule.
                if self.radio.bandwidth != old_bandwidth:
                    if self.bootstrapped:
                        self.radio.mac.schedule = []

                    if self.is_gateway:
                        # Force new schedule
                        self.schedule = None
                        self.tdma_reschedule.set()

    async def bootstrapNetwork(self):
        loop = self.loop
        config = self.config
        radio = self.radio

        try:
            # Sleep for the discovery interval
            await asyncio.sleep(config.neighbor_discovery_period)

            # Start the schedule creation task
            self.loop.create_task(self.createSchedule())

            # Trigger TDMA scheduler
            self.tdma_reschedule.set()
        except CancelledError:
            return

    async def discoverNeighbors(self):
        loop = self.loop
        radio = self.radio

        #
        # Perform neighbor discovery by periodically broadcasting HELLO messages
        #
        while not self.done:
            try:
                if self.bootstrapped:
                    period = self.config.standard_hello_interval
                else:
                    period = self.config.discovery_hello_interval

                delta = random.uniform(0.0, period)

                await asyncio.sleep(delta)

                if not self.bootstrapped:
                    chanidx = random.randint(0, len(radio.channels)-1)
                    radio.setALOHAChannel(chanidx)

                radio.controller.broadcastHello()

                await asyncio.sleep(period - delta)
            except CancelledError:
                return

    async def addDiscoveredNeighbors(self):
        """Periodically add nodes discovered by the radio to our local list of nodes"""
        while not self.done:
            try:
                await asyncio.sleep(1)
                self.addRadioNodes()
            except CancelledError:
                return

    async def synchronizeClock(self):
        radio = self.radio
        config = self.config

        while not self.done:
            try:
                await asyncio.sleep(config.clock_sync_interval)

                radio.synchronizeClock()
            except CancelledError:
                return

    @handle('Request.radio_command')
    def radioCommand(self, req):
        info = ''

        if req.radio_command == remote.START:
            if self.state == remote.READY:
                logging.info("Radio start: timestamp=%f",
                    req.timestamp)

                self.loop.create_task(self.startRadio(timestamp=req.timestamp))
                info = 'Radio started'
        elif req.radio_command == remote.STOP:
            if self.state == remote.READY or self.state == remote.ACTIVE:
                logging.info("Radio stop: timestamp=%f",
                    req.timestamp)

                self.loop.create_task(self.stopRadio())
                info = 'Radio stopping'

        resp = remote.Response()
        resp.status.state = self.state
        resp.status.info = info
        return resp

    @handle('Request.update_mandated_outcomes')
    def updateMandatedOutcomes(self, req):
        logger.info('Update mandated outcomes: timestamp=%f\n%s',
            req.timestamp,
            req.update_mandated_outcomes.goals)

        # Parse goals as JSON
        goals = json.loads(req.update_mandated_outcomes.goals)

        # Create scoring info from goals
        self.loop.run_in_executor(None, self.updateGoals, goals, req.timestamp)

        resp = remote.Response()
        resp.status.state = self.state
        resp.status.info = 'Mandated outcomes updated'
        return resp

    def setMandatedOutcomes(self, mandated_outcomes):
        """Set our mandated outcomes and update flowperf"""
        config = self.config
        radio = self.radio

        # Set our mandated outcomes
        with self.mandated_outcomes_lock:
            self.mandated_outcomes = mandated_outcomes
            self.stage_mandated_outcomes[self.scorer.stage] = mandated_outcomes

        # Create a MandatedOutcomeMap for flow performance component
        mandates = dragonradio.MandatedOutcomeMap()

        for (flow, m) in mandated_outcomes.items():
            mandates[flow] = dragonradio.MandatedOutcome(m.hold_period,
                                                         0.0,
                                                         m.point_value,
                                                         m.min_throughput_bps,
                                                         m.max_latency_s,
                                                         m.file_transfer_deadline_s)

        radio.flowperf.mandates = mandates

        # Set allowed flows
        self.setAllowedFlows(mandated_outcomes.keys())

    def setAllowedFlows(self, flows):
        """Decide which flows are allowed by the firewall.

        The specified flows, the internal control port, and all broadcast
        packets will be allowed through the packet firewall.
        """
        radio = self.radio

        allowed = set(flows)
        allowed.add(dragon.internal.INTERNAL_PORT)

        radio.netfirewall.allow_broadcasts = True
        radio.netfirewall.allowed = allowed
        radio.netfirewall.enabled = True

    @handle('Request.update_environment')
    def updateEnvironment(self, req):
        logger.info('Update environment: timestamp=%f\n%s',
            req.timestamp,
            req.update_environment.environment)

        envs = json.loads(req.update_environment.environment)
        # Environment messages contain a *list* of updates...
        for env in envs:
            bandwidth = env.get('scenario_rf_bandwidth', self.config.bandwidth)
            frequency = env.get('scenario_center_frequency', self.config.frequency)

            self.scoring_percent_threshold = env.get('scoring_percent_threshold', 0)
            self.scoring_point_threshold = env.get('scoring_point_threshold', 0)

            self.loop.create_task(self.reconfigureBandwidthAndFrequency(bandwidth, frequency))

        resp = remote.Response()
        resp.status.state = self.state
        resp.status.info = 'Environment updated'
        return resp
