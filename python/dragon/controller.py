import asyncio
from concurrent.futures import CancelledError
import json
import logging
import math
import netifaces
import numpy as np
import os
import pandas as pd
import random
import signal
import subprocess
import sys
import time

import sc2.cil_pb2 as cil

import dragonradio

from dragon.collab import RegistrationClient, CILClient, CILServer
import dragon.collab
from dragon.gpsd import GPSDClient, GPSLocation
import dragon.internal
from dragon.internal import InternalProtoClient, InternalProtoServer, mkFlowStats, mkSpectrumStats
import dragon.internal as internal
from dragon.protobuf import *
import dragon.radio
import dragon.remote as remote
import dragon.schedule

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

class Node(object):
    def __init__(self, id):
        self.id = id
        self.loc = GPSLocation()

    def __str__(self):
        return 'Node(loc={})'.format(self.loc)

class Voxel(object):
    def __init__(self):
        self.f_start = 0
        self.f_end = 0
        self.tx = None
        self.rx = []
        self.duty_cycle = 1.0

    def __str__(self):
        return 'Voxel(f_start={}, f_end={}, tx={}, rx={}, duty_cycle={})'.format(self.f_start, self.f_end, self.tx, self.rx, self.duty_cycle)

    def toCILVoxel(self, start, end, rx_gain, tx_gain, measured):
        usage = cil.SpectrumVoxelUsage()

        usage.spectrum_voxel.freq_start = self.f_start
        usage.spectrum_voxel.freq_end = self.f_end
        usage.spectrum_voxel.duty_cycle.value = self.duty_cycle
        usage.spectrum_voxel.time_start.set_timestamp(start)
        usage.spectrum_voxel.time_end.set_timestamp(end)

        usage.transmitter_info.radio_id = self.tx
        usage.transmitter_info.power_db.value = tx_gain
        usage.transmitter_info.mac_cca = False

        usage.measured_data = measured

        # Construct list of receivers for this voxel
        receivers = [dragon.collab.mkReceiverInfo(radio_id, rx_gain) for radio_id in self.rx]
        usage.receiver_info.extend(receivers)

        return usage

@handler(remote.Request)
@handler(internal.Message)
class Controller(CILServer):
    def __init__(self, config):
        CILServer.__init__(self)

        self.config = config
        """Our Config"""

        self.radio = None
        """Our Radio"""

        self.dumpcap_tasks = []
        """dumpcap tasks"""

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

        self.in_colosseum = 'tr0' in netifaces.interfaces()
        """Are we in the Colosseum?"""

        self.nodes = {}
        """Nodes in our network"""

        self.scorer = None
        """Match scorer"""

        self.scorer_lock = None
        """Lock on scorer"""

        self.flow_links = {}
        """Link, i.e., source/destination pair, for each flow"""

        self.stats_max_mp = {}
        """Maximum MP for which stats have been received from each SRN"""

        self.max_reported_mp = 0
        """Maximum MP for which flow statistics have been reported"""

        self.reported_mandate_performance = []
        """Reported mandate performance"""

        self.historical_voxel_usage = []
        """Historical voxel usage"""

        self.current_voxel_usage = {}
        """Current voxel usage"""

        self.voxel_lock = None
        """Lock on voxel usage"""

        self.schedule = None
        """Current TDMA schedule"""

        self.schedule_seq = 0
        """Current TDMA schedule sequence number."""

        self.schedule_nodes = []
        """Nodes in the TDMA schedule"""

        self.mandates = {}
        """Current mandated outcomes"""

        self.scoring_percent_threshold = 0
        """Scoring percent threshold"""

        self.scoring_point_threshold = 0
        """Scoring point threshold"""

        self.gpsd_client = None
        """gpsd client"""

        self.remote_server = None
        """Remote protocol server"""

        self.remote_server_task = None
        """Remote protocol server task"""

        self.internal_server = None
        """Internal protocol server"""

        self.internal_server_task = None
        """Internal protocol server task"""

        self.internal_client = None
        """Internal protocol client"""

        self.radio_tasks = []
        """asyncio tasks"""

        # Provide default start time
        self.scenario_start_time = math.floor(time.time())

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
        if self.radio is not None:
            self.radio.flowperf.start = t

    def timeToMP(self, t, closest=False):
        """Convert time (in seconds since the epoch) to a measurement period"""
        if closest:
            return int(round(t - self.scenario_start_time) / self.config.measurement_period)
        else:
            return int((t - self.scenario_start_time) / self.config.measurement_period)

    def currentMP(self):
        """Current measurement period"""
        return self.timeToMP(time.time())

    def setupRadio(self, bootstrap=False):
        # We cannot do this in __init__ because the controller is created
        # *before* we daemonize, and loop isn't valid after we fork
        self.loop = asyncio.get_event_loop()

        # Create Event we can use to trigger TDMA scheduler
        self.tdma_reschedule = asyncio.Event()

        # Set center frequency and bandwidth.
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
            self.radio_tasks.append(self.loop.create_task(radio.snapshotLogger()))

        # Capture interfaces
        for iface in self.config.log_interfaces:
            self.dumpcap(iface)

        # Add us as a node
        self.nodes[radio.node_id] = Node(radio.node_id)
        radio.net.addNode(radio.node_id)

        # Start reading GPS info and attach it to this node
        self.gpsd_client = GPSDClient(self.nodes[radio.node_id].loc, loop=self.loop)

        # See if we are a gateway, and if so, start the collaboration agent
        if self.config.collab_iface in netifaces.interfaces() and self.config.collab_server_ip != None:
            radio.net.nodes[radio.node_id].is_gateway = True
            self.collab_ip = netifaces.ifaddresses(self.config.collab_iface)[netifaces.AF_INET][0]['addr']
            self.startCollab()

        # We might also be forced to be the gateway...
        if self.config.force_gateway:
            radio.net.nodes[radio.node_id].is_gateway = True

        # Start the internal protocol server
        self.internal_server = InternalProtoServer(self,
                                                   loop=self.loop,
                                                   listen_ip='0.0.0.0')
        self.internal_server_task = self.internal_server.start()

        # If we are the gateway, start an internal protocol client connected to
        # the broadcast address
        if self.is_gateway:
            self.internal_client = InternalProtoClient(loop=self.loop,
                                                       server_host=INTERNAL_BCAST_ADDR)

        # If we are the gateway, start the scorer
        if self.is_gateway:
            self.scorer = dragonradio.Scorer()
            self.scorer_lock = asyncio.Lock()
            self.voxel_lock = asyncio.Lock()

        # Start status update
        self.radio_tasks.append(self.loop.create_task(self.updateStatus()))

        # Start the RPC server
        self.remote_server = TCPProtoServer(self, loop=self.loop)
        self.remote_server_task = self.remote_server.start_server(remote.Request,
                                                                  remote.REMOTE_HOST,
                                                                  remote.REMOTE_PORT)

        # Bootstrap the radio if we've been asked to. Otherwise, we will not
        # bootstrap until a radio API client tells us to.
        if bootstrap:
            self.loop.create_task(self.startRadio())

        self.state = remote.READY

        try:
            self.loop.run_forever()
        finally:
            logger.info('Controller terminated')
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

                # Create ALOHA MAC for HELLO messages
                self.radio.configureALOHA()

                self.radio_tasks.append(self.loop.create_task(self.discoverNeighbors()))
                self.radio_tasks.append(self.loop.create_task(self.addDiscoveredNeighbors()))
                self.radio_tasks.append(self.loop.create_task(self.synchronizeClock()))

                if self.is_gateway:
                    self.radio_tasks.append(self.loop.create_task(self.bootstrapNetwork()))
                    self.radio_tasks.append(self.loop.create_task(self.distributeScheduleViaBroadcast()))

                self.state = remote.ACTIVE

    async def stopRadio(self):
        if not self.done:
            self.done = True
            self.state = remote.STOPPING

            # Stop the collaboration server
            if self.collab_server:
                try:
                    await self.stopCollab()
                except:
                    logger.exception('Could not gracefully terminate collaboration agent')

            # Stop radio tasks
            logger.info('Stopping radio tasks')
            for task in self.radio_tasks:
                task.cancel()

            await asyncio.gather(*self.radio_tasks, return_exceptions=True)

            # Stop internal protocol server
            if self.internal_server_task:
                logger.info('Stopping internal protocol server')
                self.internal_server_task.cancel()
                await asyncio.gather(self.internal_server_task, return_exceptions=True)

            # Close internal protocol client
            if self.internal_client:
                logger.info('Stopping internal protocol client')
                self.internal_client.close()

            # Stop the gpsd client
            if self.gpsd_client:
                logger.info('Stopping gpsd client')
                await self.gpsd_client.stop()

            # Dump score data if we are the gateway
            if self.is_gateway:
                logger.info('Dumping final scoring data')
                try:
                    # Dump reported scores
                    self.saveReportedMandatePerformance()
                except:
                    logger.exception('Could not dump scoring data')

            # Terminate any packet captures
            await self.cleanupDumpcap()

            with await self.radio.lock:
                # Remove all nodes
                for node_id in list(self.nodes):
                    self.removeNode(node_id)

            # Close the logger
            self.radio.logger.close()

            # Update radio state to FINISHED
            self.state = remote.FINISHED
            logger.info('Radio stopped')

    async def terminate(self):
        logger.debug('Terminating')
        await self.stopRadio()

        # Stop remote server
        logger.debug('Stopping remote protocol server')
        self.remote_server_task.cancel()
        await asyncio.gather(self.remote_server_task, return_exceptions=True)

        # Log unfinished tasks
        tasks = list(asyncio.Task.all_tasks())
        tasks.remove(asyncio.Task.current_task())

        unfinished_tasks = [t for t in tasks if not t.done()]

        if len(unfinished_tasks) != 0:
            logger.debug('Unfinished tasks: %s', unfinished_tasks)

            # Cancel unfinished tasks
            logger.info('Cancelling unfinished tasks')

            for task in unfinished_tasks:
                pass #task.cancel()

        await asyncio.gather(*tasks, return_exceptions=True)

        # Stop event loop
        logger.info('Stopping event loop')
        self.loop.stop()

    def dumpcap(self, iface):
        if iface in netifaces.interfaces():
            async def f(controller):
                p = await asyncio.create_subprocess_exec('dumpcap', '-i', iface, '-q', '-w', '{logdir}/{iface}.pcapng'.format(iface=iface, logdir=self.config.logdir),
                    stdin=None, stdout=None, stderr=None, loop=self.loop)
                controller.dumpcap_procs.append(p)

            self.dumpcap_tasks.append(self.loop.create_task(f(self)))

    async def cleanupDumpcap(self):
        if len(self.dumpcap_tasks) != 0:
            logger.info('Terminating pcap captures')

            # Wait for dumpcap processes to be created
            await asyncio.gather(*self.dumpcap_tasks, return_exceptions=True)

            # Terminate dumpcap processes. For some reason the TERM signal
            # doesn't always cause dumpcap to terminate, so we keep trying until
            # it works
            while True:
                for p in self.dumpcap_procs:
                    try:
                        p.terminate()
                    except:
                        logger.exception('Could not terminate PID %d', p.pid)

                _done, pending = await asyncio.wait([p.communicate() for p in self.dumpcap_procs],
                    timeout=1)

                if len(pending) == 0:
                    break

            # Compress pcap files
            if self.config.compress_interface_logs:
                # Compressing large interface logs takes too long
                xzprocs = []

                for iface in self.config.log_interfaces:
                    if iface in netifaces.interfaces():
                        try:
                            p = await asyncio.create_subprocess_exec('xz', '{logdir}/{iface}.pcapng'.format(iface=iface, logdir=self.config.logdir),
                                stdin=None, stdout=None, stderr=None, loop=self.loop)
                            xzprocs.append(p)
                        except:
                            logging.exception('Could not xz {logdir}/{iface}.pcapng'.format(iface=iface, logdir=self.config.logdir))

                await asyncio.gather(*[p.communicate() for p in xzprocs])

    def thisNode(self):
        return self.nodes[self.radio.node_id]

    def addNode(self, node_id):
        if node_id != self.radio.node_id and node_id not in self.nodes:
            logger.info('Adding node %d', node_id)
            node = Node(node_id)
            self.nodes[node_id] = node

            # Add a route for the new node
            if self.in_colosseum:
                try:
                    subprocess.check_call('ip route add {} via {}'.format(darpaNodeNet(node_id), internalNodeIP(node_id)), shell=True)
                except:
                    logger.exception('Could not add route to node {}'.format(node_id))

            # If new node is a gateway, connect to it and start sending status
            # updates
            if self.radio.net.nodes[node_id].is_gateway:
                self.internal_client = InternalProtoClient(loop=self.loop,
                                                           server_host=internalNodeIP(node_id))

            # If we are the gateway, connect an internal protocol client to the
            # new node's server
            if self.is_gateway:
                node.internal_client = InternalProtoClient(loop=self.loop,
                                                           server_host=internalNodeIP(node_id))

            # If we are the gateway, update the schedule
            if self.is_gateway:
                self.tdma_reschedule.set()

    async def logNetworkInfo(self):
        # Log ARP table
        p = await asyncio.create_subprocess_exec('arp', '-an',
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE)
        stdout, stderr = await p.communicate()
        logger.info('ARP table:\n%s', stdout.decode('utf-8'))

        # Log routes
        p = await asyncio.create_subprocess_exec('ip', 'route',
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE)
        stdout, stderr = await p.communicate()
        logger.info('Routing table:\n%s', stdout.decode('utf-8'))

        # Log ip links
        p = await asyncio.create_subprocess_exec('ip', 'a',
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE)
        stdout, stderr = await p.communicate()
        logger.info('IP links:\n%s', stdout.decode('utf-8'))

        # Log routes
        for node_id in list(self.nodes):
            for octet in [1]:
                ip = '192.168.{:d}.{:d}'.format(node_id+100, octet)

                p = await asyncio.create_subprocess_exec('ip', 'route', 'get', ip,
                    stdout=asyncio.subprocess.PIPE,
                    stderr=asyncio.subprocess.PIPE)
                stdout, stderr = await p.communicate()
                logger.info('IP route for %s:\n%s', ip, stdout.decode('utf-8'))

    def removeNode(self, node_id):
        if node_id != self.radio.node_id and node_id in self.nodes:
            logger.info('Removing node %d', node_id)

            if self.in_colosseum:
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

    def getDestinations(self, node_id):
        """Return a list of nodes given node transmits to"""
        destinations = set()

        for _, link in self.flow_links.items():
            (src, dest) = link
            # Don't include broadcasts
            if node_id == src and dest != 255:
                destinations.add(dest)

        return destinations

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

    async def updateVoxelUsage(self, vox):
        """Get voxel usage

        Arguments:
            node_id: The node ID of the transmitting node
            vox: The voxel
        """
        config = self.config

        with await self.voxel_lock:
            # Look up most recent usage report by this node on this channel
            duty_cycle = 0

            if vox.tx in self.current_voxel_usage:
                duty_cycle = self.current_voxel_usage[vox.tx].get((vox.f_start, vox.f_end), None)
                if duty_cycle is not None:
                    # Set duty cycle
                    vox.duty_cycle = duty_cycle

                    # Trim channel start and end
                    bw = vox.f_end - vox.f_start
                    vox.f_start += config.spec_chan_trim_lo*bw
                    vox.f_end -= config.spec_chan_trim_hi*bw

                    # Set receivers
                    vox.rx = self.getDestinations(vox.tx)

    async def alohaToVoxels(self):
        """Determine voxel usage for ALOHA"""
        config = self.config
        radio = self.radio

        nnodes = len(self.nodes)
        nchannels = len(radio.channels)

        voxels = []

        for chan in radio.channels:
            transmitters = set(self.nodes)

            f_start = radio.frequency + chan.fc - chan.bw/2
            f_end = radio.frequency + chan.fc + chan.bw/2

            for tx in transmitters:
                vox = Voxel()
                vox.f_start = f_start
                vox.f_end = f_end
                vox.tx = tx

                await self.updateVoxelUsage(vox)

                if vox.duty_cycle != 0:
                    voxels.append(vox)

        return voxels

    async def scheduleToVoxels(self, sched):
        """Determine voxel usage from schedule"""
        config = self.config
        radio = self.radio

        (nchannels, nslots) = sched.shape

        voxels = []

        for chanidx in range(0, nchannels):
            transmitters = set(sched[chanidx])
            if 0 in transmitters:
                transmitters.remove(0)

            if len(transmitters) != 0:
                chan = radio.channels[chanidx]
                f_start = radio.frequency + chan.fc - chan.bw/2
                f_end = radio.frequency + chan.fc + chan.bw/2

                for tx in transmitters:
                    vox = Voxel()
                    vox.f_start = f_start
                    vox.f_end = f_end
                    vox.tx = tx

                    await self.updateVoxelUsage(vox)

                    if vox.duty_cycle != 0:
                        voxels.append(vox)

        return voxels

    async def cacheTrafficInterfaceARP(self):
        """Get all addresses on the traffic interface's subnet into the ARP
        cache.

        See:
            https://sc2colosseum.freshdesk.com/support/solutions/articles/22000220402-traffic-generation
        """
        try:
            if self.in_colosseum:
                node_id = self.radio.node_id

                async def mkARPTask(peer_id):
                    ip = '192.168.{:d}.{:d}'.format(node_id+100, peer_id)
                    ether = '02:10:{:02x}:{:02x}:{:02x}:{:02x}'.format(192, 168, node_id+100, peer_id)

                    p = await asyncio.create_subprocess_exec(
                            'arp', '-s', ip, ether,
                            stdout=asyncio.subprocess.PIPE,
                            stderr=asyncio.subprocess.PIPE)

                    (stdout_data, stderr_data) = await p.communicate()
                    return (ip, stdout_data, stderr_data)

                tasks = [mkARPTask(peer_id) for peer_id in range(1, 255)]
                await asyncio.gather(*tasks, return_exceptions=True)
        except CancelledError:
            return

    async def getMandatePerformance(self):
        config = self.config

        # Determine which measurement period to use. We use the latest MP for
        # which we have data from all nodes, except if it is older than
        # config.max_performance_age seconds ago, in which case we extrapolate.
        known_mps = self.stats_max_mp.values()
        if len(known_mps) == 0:
            min_known_mp = 0
        else:
            min_known_mp = min(self.stats_max_mp.values())

        min_mp = self.timeToMP(time.time() - config.max_performance_age)

        mp = max(min_known_mp, min_mp)
        timestamp = self.scenario_start_time + mp*config.measurement_period

        # Get updated scores
        with await self.scorer_lock:
            self.scorer.updateScore(self.currentMP())

            scores = self.scorer.scores

        # Updated mandated outcomes using scoring data from measurement period
        # mp
        mandates_achieved = 0
        total_score_achieved = 0
        mandates = []

        for (flow_uid, mandate) in self.mandates.items():
            mandates.append(mandate)

            # Get radio IDs involved in this flow
            if flow_uid in self.flow_links:
                (src, dest) = self.flow_links[flow_uid]
                mandate.radio_ids = [src, dest]
            else:
                mandate.radio_ids = []

            # Get score for this flow at this MP
            score = scores[flow_uid][mp]

            logging.debug("Score: flow_uid=%s; mp=%s; update_timestamp_sent=%s; npackets_sent=%s; update_timestamp_recv=%s; npackets_recv=%s",
                flow_uid,
                mp,
                score.update_timestamp_sent,
                score.npackets_sent,
                score.update_timestamp_recv,
                score.npackets_recv)

            # Calculate scalar performance and
            mandate.achieved_duration = score.achieved_duration

            if mandate.achieved_duration >= mandate.hold_period:
                mandate.scalar_performance = 1.0
                mandates_achieved += 1
                total_score_achieved += mandate.point_value
            else:
                mandate.scalar_performance = mandate.achieved_duration/mandate.hold_period

        # Log the reported score
        self.reported_mandate_performance.append((mp, mandates_achieved, total_score_achieved))

        # Return performance metrics
        return (mp, timestamp, mandates_achieved, total_score_achieved, mandates)

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

        # Update mandated outcomes
        mandates = dragonradio.MandateMap()

        for goal in goals:
            flow_uid = goal['flow_uid']
            hold_period = goal['hold_period']
            point_value = goal.get('point_value', 1)
            max_latency_s = goal['requirements'].get('max_latency_s', None)
            min_throughput_bps = goal['requirements'].get('min_throughput_bps', None)
            file_transfer_deadline_s = goal['requirements'].get('file_transfer_deadline_s', None)

            mandates[flow_uid] = dragonradio.Mandate(flow_uid,
                                                     hold_period,
                                                     point_value,
                                                     max_latency_s,
                                                     min_throughput_bps,
                                                     file_transfer_deadline_s)

        self.setMandates(mandates)

        # We have mandates!
        self.scenario_started = True

    async def updateStatus(self):
        """Update status"""
        config = self.config
        radio = self.radio
        node_id = radio.node_id

        while not self.done:
            try:
                if self.scenario_started:
                    (sources, sinks) = await self.getFlowStatistics()
                    spectrum = await self.getSpectrumStatistics()

                    # Update local statistics if we are the gateway
                    if self.is_gateway:
                        await self.updateFlowStatistics(node_id,
                                                        time.time(),
                                                        sources,
                                                        sinks)
                        await self.updateSpectrumStatistics(node_id,
                                                            time.time(),
                                                            spectrum)
                    # Otherwise, send statistics to the gateway
                    elif self.internal_client is not None:
                        await self.internal_client.sendStatus(self.thisNode(),
                                                              sources,
                                                              sinks,
                                                              spectrum)

                await asyncio.sleep(config.status_update_period)
            except CancelledError:
                return
            except:
                logging.exception('Exception in updateStatus')

    async def getFlowStatistics(self):
        """Get flow statistics for this node.

        Return:
            A pair of lists of internal.FlowStats values representing flow
            statistics for sources and sinks, respectively, for this node.
        """
        config = self.config
        radio = self.radio

        # Determine minimum and maximum MP for which we will report
        # flow statistics
        min_mp = self.max_reported_mp + 1
        max_mp = self.timeToMP(time.time() - config.stats_ignore_window)
        self.max_reported_mp = max_mp

        # Get local flow statistics
        if self.is_gateway or self.internal_client is not None:
            reset_stats = True
        else:
            reset_stats = False

        sources = [mkFlowStats(min_mp, max_mp, p) for p in radio.flowperf.getSources(reset_stats).values() if p.low_mp is not None]
        sinks = [mkFlowStats(min_mp, max_mp, p) for p in radio.flowperf.getSinks(reset_stats).values() if p.low_mp is not None]

        return (sources, sinks)

    async def getSpectrumStatistics(self):
        """Get spectrum usage statistics for this node.

        Return:
            A list of internal.SpectrumStats values representing spectrum usage
            statistics for this node.
        """
        config = self.config
        radio = self.radio
        mac = radio.mac

        spectrum_usage = []

        if mac is not None:
            if config.tx_upsample:
                channels = radio.channels
            else:
                channels = [radio.channels[radio.tx_channel_idx]]

            # Get per-channel load from MAC
            load = mac.popLoad()

            # Get current TX sample rate
            fs = radio.usrp.tx_rate

            # Calculate load denominator: the number of samples that could've
            # been transmitted over the loaded period.
            nsamples = fs*load.period

            voxels = []

            for idx, chan in enumerate(channels):
                if idx < len(load.nsamples):
                    duty_cycle = load.nsamples[idx]/nsamples

                    if duty_cycle != 0:
                        vox = Voxel()
                        vox.f_start = self.radio.frequency + chan.fc - chan.bw/2
                        vox.f_end = self.radio.frequency + chan.fc + chan.bw/2
                        vox.duty_cycle = duty_cycle
                        voxels.append(vox)

            if len(voxels) != 0:
                spectrum_usage.append(mkSpectrumStats(load.start, load.end, voxels))

        return spectrum_usage

    async def updateFlowStatistics(self, node_id, timestamp, sources, sinks):
        """Update flow statistics from reported source and sink stats"""
        with await self.scorer_lock:
            for flow in sources:
                # Record maximum MP
                max_mp = flow.first_mp + min(len(flow.npackets), len(flow.nbytes)) - 1
                self.stats_max_mp[node_id] = max(self.stats_max_mp.get(node_id, 0), max_mp)

                logger.info("Updating source flow statistics: node=%d; flow=%d; timestamp=%f; current mp=%d; first_mp=%d; max_mp=%d; npackets=%s; nbytes=%s",
                    node_id,
                    flow.flow_uid,
                    timestamp,
                    self.currentMP(),
                    flow.first_mp,
                    max_mp,
                    flow.npackets,
                    flow.nbytes)

                # Record link between nodes
                self.flow_links[flow.flow_uid] = (flow.src, flow.dest)

                # Update scorer
                self.scorer.updateSentStatistics(flow.flow_uid,
                                                 timestamp,
                                                 flow.first_mp,
                                                 flow.npackets,
                                                 flow.nbytes)

            for flow in sinks:
                # Record maximum MP
                max_mp = flow.first_mp + min(len(flow.npackets), len(flow.nbytes)) - 1
                self.stats_max_mp[node_id] = max(self.stats_max_mp.get(node_id, 0), max_mp)

                logger.info("Updating sink flow statistics: node=%d; flow=%d; timestamp=%f; current mp=%d; first_mp=%d; max_mp=%d; npackets=%s; nbytes=%s",
                    node_id,
                    flow.flow_uid,
                    timestamp,
                    self.currentMP(),
                    flow.first_mp,
                    max_mp,
                    flow.npackets,
                    flow.nbytes)

                # Record link between nodes
                self.flow_links[flow.flow_uid] = (flow.src, flow.dest)

                # Update scorer
                self.scorer.updateReceivedStatistics(flow.flow_uid,
                                                     timestamp,
                                                     flow.first_mp,
                                                     flow.npackets,
                                                     flow.nbytes)

    async def updateSpectrumStatistics(self, node_id, timestamp, reports):
        """Update flow statistics from reported source and sink stats"""
        radio = self.radio
        channels = radio.channels

        load = {}
        historical_usage = []

        for spectrum in reports:
            for usage in spectrum.voxels:
                vox = Voxel()
                vox.f_start = usage.f_start
                vox.f_end = usage.f_end
                vox.duty_cycle = usage.duty_cycle
                vox.tx = node_id
                vox.rx = self.getDestinations(node_id)

                historical_usage.append((spectrum.start.get_timestamp(),
                                         spectrum.end.get_timestamp(),
                                         vox))

                load[(vox.f_start, vox.f_end)] = usage.duty_cycle

                logging.debug("Spectrum usage: start=%s; end=%s; voxel=%s",
                    spectrum.start.get_timestamp(),
                    spectrum.end.get_timestamp(),
                    vox)

        with await self.voxel_lock:
            self.historical_voxel_usage += historical_usage
            self.current_voxel_usage[node_id] = load

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
                                                        self.schedule,
                                                        self.config.frequency,
                                                        self.config.bandwidth,
                                                        self.scenario_start_time)

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
                                                self.schedule,
                                                self.config.frequency,
                                                self.config.bandwidth,
                                                self.scenario_start_time)

    async def reconfigureBandwidthAndFrequency(self, bandwidth, frequency):
        """Reconfigure bandwidth and frequency.

        If we are the gateway node, this will trigger a new TDMA schedule.
        """
        with await self.radio.lock:
            if bandwidth != self.config.bandwidth or frequency != self.config.frequency:
                old_bandwidth = self.radio.bandwidth

                self.radio.reconfigureBandwidthAndFrequency(bandwidth, frequency)
                if self.collab_server:
                    self.push_spectrum_usage()

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

            # Log network info
            try:
                await self.logNetworkInfo()
            except:
                logging.exception("Error while logging network info")

            # Start the schedule creation task
            self.radio_tasks.append(self.loop.create_task(self.createSchedule()))

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

    async def getHistoricalSpectrumUsage(self):
        """Get historical voxel usage from controller and convert it into CIL
        voxels for spectrum usage report"""
        with await self.voxel_lock:
            voxels = self.historical_voxel_usage
            self.historical_voxel_usage = []

        rx_gain = self.radio.usrp.rx_gain
        tx_gain = self.radio.usrp.tx_gain

        return [vox.toCILVoxel(start, end, rx_gain, tx_gain, True) for (start, end, vox) in voxels]

    async def getPredictedSpectrumUsage(self, when):
        """Get predicated voxel usage from controller and convert it into CIL
        voxels for spectrum usage report"""
        mac = self.radio.mac

        if mac is None:
            voxels = []
        elif isinstance(mac, dragonradio.SlottedALOHA):
            voxels = await self.alohaToVoxels()
        else:
            voxels = await self.scheduleToVoxels(self.schedule)

        start = when
        end = when + self.config.spec_future_period
        rx_gain = self.radio.usrp.rx_gain
        tx_gain = self.radio.usrp.tx_gain

        return [vox.toCILVoxel(start, end, rx_gain, tx_gain, False) for vox in voxels]

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

    def setMandates(self, mandates):
        """Set our mandated outcomes and update flowperf"""
        config = self.config
        radio = self.radio

        # Set our mandated outcomes
        self.mandates = mandates

        # Set flow performance mandates
        radio.flowperf.mandates = mandates

        # Set scorer mandates
        if self.scorer:
            self.scorer.setMandates(mandates)

        # Update Mandate queue
        if isinstance(radio.netq, dragonradio.MandateQueue):
            # Add mandates
            radio.netq.mandates = mandates

            # Make internal traffic very high priority
            radio.netq.setFlowQueuePriority(dragon.internal.INTERNAL_PORT, (99, 0.0))
            radio.netq.setFlowQueueType(dragon.internal.INTERNAL_PORT, dragonradio.MandateQueue.FIFO)
        else:
            # Set allowed flows
            self.setAllowedFlows([flow_uid for (flow_uid, _mandate) in mandates.items()])

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

    @handle('Message.status')
    def handle_status(self, msg):
        node_id = msg.status.radio_id

        # Update node location
        if node_id in self.nodes:
            n = self.nodes[node_id]
            loc = msg.status.loc

            n.loc.lat = loc.location.latitude
            n.loc.lon = loc.location.longitude
            n.loc.alt = loc.location.elevation
            n.loc.timestamp = loc.timestamp.get_timestamp()

        # Update statistics
        self.loop.create_task(self.updateFlowStatistics(node_id,
                                                        msg.status.timestamp.get_timestamp(),
                                                        msg.status.source_flows,
                                                        msg.status.sink_flows))
        self.loop.create_task(self.updateSpectrumStatistics(node_id,
                                                            msg.status.timestamp.get_timestamp(),
                                                            msg.status.spectrum_stats))

    @handle('Message.schedule')
    def handle_schedule(self, msg):
        config = self.config
        radio = self.radio

        self.scenario_start_time = msg.schedule.scenario_start_time

        if radio.node_id in msg.schedule.nodes:
            if msg.schedule.bandwidth != config.bandwidth or \
                msg.schedule.frequency != config.frequency:
                logging.info('Not installing schedule with frequency parameters (bw={:g}; fc={:g}) different from ours (bw={:g}; fc={:g})'.\
                    format(msg.schedule.bandwidth,
                           msg.schedule.frequency,
                           config.bandwidth,
                           config.frequency))
                return

            nchannels = msg.schedule.nchannels
            nslots = msg.schedule.nslots

            sched = np.array(msg.schedule.schedule).reshape((nchannels, nslots))

            self.loop.create_task(self.installMACSchedule(msg.schedule.seq, sched))
