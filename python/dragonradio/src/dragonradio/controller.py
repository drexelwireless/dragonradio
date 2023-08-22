# Copyright 2018-2023 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

"""Controller for intelligent radio functionality"""

import asyncio
import ipaddress
import json
import logging
import math
import os
from pathlib import Path
import random
import time
from typing import Dict, List, Optional, Sequence, Set, Tuple
from typing_extensions import TypeAlias

import importlib_resources
import netifaces # type: ignore
import numpy as np
import pandas as pd

from dragonradio.collab import CILServer
import dragonradio.collab
from dragonradio.gpsd import GPSDClient
import dragonradio.internal as internal
from dragonradio.internal import InternalProtoClient, InternalProtoServer
from dragonradio.internal import FlowStats, mkFlowStats, mkSpectrumStats
import dragonradio.net
import dragonradio.proto.sc2.cil_pb2 as cil
from dragonradio.protobuf.handler import handle, handler
from dragonradio.protobuf.tcp import TCPProtoServer
from dragonradio.protobuf.timestamp import getTimestamp, setTimestamp
import dragonradio.radio
from dragonradio.radio import PHYChannel
from dragonradio.radio.radio import Config, ConfigException, Radio
import dragonradio.remote.client as remote
import dragonradio.schedule
import dragonradio.tasks
from dragonradio.util import save_dataframe

MP: TypeAlias = int

NodeId: TypeAlias = int

FlowUID: TypeAlias = int

FlowLink: TypeAlias = Tuple[NodeId, NodeId]

Load: TypeAlias = Dict[Tuple[float, float], float]

VoxelUsage: TypeAlias = Tuple[float, float, 'Voxel']

logger = logging.getLogger('controller')

INTERNAL_BCAST_ADDR: str = '10.10.10.255'

def internalNodeIP(node_id: int) -> str:
    """Return IP address of radio node on internal network"""
    return '10.10.10.{:d}'.format(node_id)

def darpaNodeNet(node_id: int) -> str:
    """Return IP subnet of radio node on DARPA's network."""
    return '192.168.{:d}.0'.format(node_id+100)

def darpaNodeNetmask() -> str:
    """Return IP subnet mask of radio node on DARPA's network."""
    return '255.255.255.0'

def darpaNodeIP(node_id: int, octet: int=0) -> str:
    """Return IP address of radio node on DARPA's network."""
    return '192.168.{:d}.{:d}'.format(node_id+100, octet)

def darpaNodeMAC(node_id: int, octet: int=0) -> str:
    """Return MAC address of radio node on DARPA's network."""
    return '02:10:{:02x}:{:02x}:{:02x}:{:02x}'.\
        format(192, 168, node_id+100, octet)

class Voxel:
    """A voxel"""
    f_start: float = 0
    """Frequency start"""

    f_end: float = 0
    """Frequency end"""

    tx: int
    """"Transmitting node"""

    rx: List[int]
    """Receiving nodes"""

    duty_cycle: float = 1.0
    """Duty cycle (fraction)"""

    def __init__(self):
        self.rx = []

    def __str__(self):
        return 'Voxel(f_start={}, f_end={}, tx={}, rx={}, duty_cycle={})'.\
            format(self.f_start, self.f_end, self.tx, self.rx, self.duty_cycle)

    def toCILVoxel(self, start: float, end: float, rx_gain: float, tx_gain: float, measured: bool) -> cil.SpectrumVoxelUsage:
        """Convert Voxel to a CIL voxel.

        Arguments:
            start: timestamp of start of voxel
            end: timestamp of end of voxel
            rx_gain: RX gain (dB)
            tx_gain: TX gain (dB)
            measured: Whether this voxel was measured (True) or not (False)

        Return:
            A CIL SpectrumVoxelUsage
        """
        usage = cil.SpectrumVoxelUsage()

        usage.spectrum_voxel.freq_start = self.f_start
        usage.spectrum_voxel.freq_end = self.f_end
        usage.spectrum_voxel.duty_cycle.value = self.duty_cycle
        setTimestamp(usage.spectrum_voxel.time_start, start)
        setTimestamp(usage.spectrum_voxel.time_end, end)

        usage.transmitter_info.radio_id = self.tx
        usage.transmitter_info.power_db.value = tx_gain
        usage.transmitter_info.mac_cca = False

        usage.measured_data = measured

        # Construct list of receivers for this voxel
        receivers = [dragonradio.collab.mkReceiverInfo(radio_id, rx_gain) for radio_id in self.rx]
        usage.receiver_info.extend(receivers)

        return usage

def channelToVoxel(tx: int, fs: float, phy_chan: PHYChannel) -> Voxel:
    vox = Voxel()
    vox.f_start = fs + phy_chan.channel.fc - phy_chan.channel.bw/2
    vox.f_end = fs + phy_chan.channel.fc + phy_chan.channel.bw/2
    vox.tx = tx

    return vox

@handler(remote.Request)
@handler(internal.Message)
class Controller(CILServer, dragonradio.radio.NeighborhoodListener):
    """High-level radio controller.

    This class implements all high-level control necessary for the Colosseum.
    """
    config: Config
    """Radio configuration"""

    radio: Optional[Radio] = None
    """Radio"""

    dumpcap_tasks: List[asyncio.Task]
    """dumpcap tasks"""

    dumpcap_procs: List[asyncio.subprocess.Process]
    """dumpcap procs we've started"""

    _state: remote.State = remote.BOOTING
    """Current radio state"""

    started: bool = False
    """Has the radio been started?"""

    done: bool = False
    """Is the radio done?"""

    bootstrapped: bool = False
    """Has the radio already been bootstrapped?"""

    has_traffic_iface: bool = False
    """Do we have a Colosseum traffic interface?"""

    scorer: Optional[dragonradio.radio.Scorer] = None
    """Match scorer"""

    scorer_lock: asyncio.Lock
    """Lock on scorer"""

    flow_links: Dict[FlowUID, FlowLink]
    """Link, i.e., source/destination pair, for each flow"""

    stats_max_mp: Dict[NodeId, MP]
    """Maximum MP for which stats have been received from each SRN"""

    max_reported_mp: MP = 0
    """Maximum MP for which flow statistics have been reported"""

    reported_mandate_performance: List[Tuple[MP, int, float]]
    """Reported mandate performance"""

    historical_voxel_usage: List[Tuple[float, float, Voxel]]
    """Historical voxel usage"""

    current_voxel_usage: Dict
    """Current voxel usage"""

    voxel_lock: asyncio.Lock
    """Lock on voxel usage"""

    tdma_reschedule: asyncio.Event
    """Event to trigger rescheduling"""

    schedule: Optional[dragonradio.schedule.Schedule] = None
    """Current TDMA schedule"""

    schedule_seq: int = 0
    """Current TDMA schedule sequence number."""

    schedule_nodes: List[NodeId]
    """Nodes in the TDMA schedule"""

    mandates: dragonradio.radio.MandateMap
    """Current mandated outcomes"""

    gpsd_client: Optional[GPSDClient] = None
    """gpsd client"""

    remote_server: Optional[TCPProtoServer] = None
    """Remote protocol server"""

    remote_server_task: asyncio.Task
    """Remote protocol server task"""

    internal_server: Optional[InternalProtoServer] = None
    """Internal protocol server"""

    internal_server_task: asyncio.Task
    """Internal protocol server task"""

    internal_client: Optional[InternalProtoClient] = None
    """Internal protocol client"""

    node_internal_clients: Dict[NodeId, InternalProtoClient]
    """Node internal protocol clients"""

    # Provide default start time
    scenario_start_time: float

    def __init__(self, config: Config):
        CILServer.__init__(self)
        dragonradio.radio.NeighborhoodListener.__init__(self)

        self.config = config

        self.dumpcap_tasks = []
        self.dumpcap_procs = []

        self.has_traffic_iface = config.traffic_iface in netifaces.interfaces()

        self.flow_links = {}
        self.stats_max_mp = {}

        self.reported_mandate_performance = []
        self.historical_voxel_usage = []
        self.current_voxel_usage = {}

        self.schedule_nodes = []
        self.mandates = dragonradio.radio.MandateMap()

        self.node_internal_clients = {}

        # Provide default start time
        self.scenario_start_time = math.floor(time.time())

        if not config.arq:
            raise ConfigException("Neighbor discovery is only supported with the "
                                  "SmartController LLC. Please enable AMC and ARQ, e.g., "
                                  "with the --amc and --arq command-line flags.")

    def __del__(self):
        try:
            if self.radio is not None:
                if self.radio.nhood is not None:
                    self.radio.nhood.removeListener(self)
        except:
            logger.exception("Could not remove controller as neighborhood listener")

    @property
    def state(self):
        return self._state

    @state.setter
    def state(self, new_state: remote.State):
        if new_state != self._state:
            old_state = self._state
            self._state = new_state
            self.stateChange(old_state, new_state)

    def stateChange(self, old_state: remote.State, new_state: remote.State):
        pass

    def timeToMP(self, t, closest=False):
        """Convert time (in seconds since the epoch) to a measurement period"""
        if closest: # pylint: disable=no-else-return
            return int(round(t - self.scenario_start_time) / self.config.measurement_period)
        else:
            return int((t - self.scenario_start_time) / self.config.measurement_period)

    def currentMP(self):
        """Current measurement period"""
        return self.timeToMP(time.time())

    def setupRadio(self, bootstrap=False):
        """Set up the SC2 radio"""
        # We cannot do this in __init__ because the controller is created
        # *before* we daemonize, and loop isn't valid after we fork
        self.loop = asyncio.get_event_loop()

        # Create Event we can use to trigger TDMA scheduler. This must also be
        # done after any fork.
        self.tdma_reschedule = asyncio.Event()

        # Set center frequency and bandwidth.
        if hasattr(self.config, 'center_frequency'):
            self.config.frequency = getattr(self.config, 'center_frequency')
        else:
            logger.warning('Center frequency not specified; using %f', self.config.frequency)

        if hasattr(self.config, 'rf_bandwidth'):
            self.config.bandwidth = getattr(self.config, 'rf_bandwidth')
        else:
            logger.warning('Bandwidth not specified; using %f', self.config.bandwidth)

        # Create the radio object
        radio = self.mkRadio(self.config, 'aloha', loop=self.loop)
        self.radio = radio

        # Listen for neighborhood events
        assert self.radio.nhood is not None

        self.radio.nhood.addListener(self)

        # Collect snapshots if requested
        if self.config.snapshot_frequency is not None:
            radio.startSnapshots()

        # Capture interfaces
        for iface in self.config.log_interfaces:
            self.dumpcap(iface)

        # Start reading GPS info and attach it to this node
        self.gpsd_client = GPSDClient(self.this_node.loc, loop=self.loop)

        # See if we are a gateway, and if so, start the collaboration agent
        if self.haveCollabInterface() and self.config.collab_server_ip is not None:
            self.this_node.is_gateway = True

            collab_addrs = netifaces.ifaddresses(self.config.collab_iface)
            self.collab_ip = collab_addrs[netifaces.AF_INET][0]['addr']

            self.startCollab()

        # We might also be forced to be the gateway...
        if self.config.force_gateway:
            self.this_node.is_gateway = True

        # Start the internal protocol server
        self.internal_server = InternalProtoServer(handler_obj=self,
                                                   listen_ip='0.0.0.0',
                                                   loop=self.loop)
        self.internal_server_task = self.internal_server.start()

        # If we are the gateway, start an internal protocol client connected to
        # the broadcast address
        if self.is_gateway:
            int_net = ipaddress.IPv4Network((self.config.internal_net, self.config.internal_netmask))

            self.internal_client = \
                InternalProtoClient(loop=self.loop,
                                    server_host=str(int_net.broadcast_address))

        # If we are the gateway, start the scorer
        if self.is_gateway:
            self.scorer = dragonradio.radio.Scorer()
            self.scorer_lock = asyncio.Lock()
            self.voxel_lock = asyncio.Lock()

        # Start status update
        self.createTask(self.updateStatus(), name='update status')

        # Start the RPC server
        self.remote_server = TCPProtoServer(self, loop=self.loop)
        self.remote_server_task = self.remote_server.start_server(remote.Request,
                                                                  remote.DEFAULT_HOST,
                                                                  remote.DEFAULT_PORT)

        # Cache ARP table entries for Colosseum traffic interface
        self.cacheTrafficInterfaceARP()

        # Bootstrap the radio if we've been asked to. Otherwise, we will not
        # bootstrap until a radio API client tells us to.
        if bootstrap:
            self.createTask(self.startRadio(), "Start radio")

        self.state = remote.READY

        # Either start the interactive loop or run the loop ourselves
        user_ns = locals()
        user_ns['radio'] = self.radio
        user_ns['controller'] = self

        self.radio.run(finalizer=self.terminate, user_ns=user_ns)

        logger.info('Controller terminated')

    def mkRadio(self, *args, **kwargs):
        return dragonradio.radio.Radio(*args, **kwargs)

    async def startRadio(self, timestamp=None):
        """Start the radio"""
        assert self.radio is not None

        if timestamp is None:
            timestamp = time.time()

        if not self.started:
            try:
                logger.info('Starting radio: now=%f; timestamp=%f',
                    time.time(),
                    timestamp)

                async with self.radio.lock:
                    self.started = True
                    self.scenario_start_time = timestamp

                    # Create ALOHA MAC for HELLO messages
                    self.radio.configureMAC(dragonradio.radio.SlottedALOHA)

                    self.createTask(self.discoverNeighbors(),
                                    name='discover neighbors')

                    if self.config.clock_sync_period is not None:
                        self.createTask(self.synchronizeClock(),
                                        name='synchronize clock')

                    if self.is_gateway:
                        self.createTask(self.bootstrapNetwork(),
                                        name='bootstrap network')
                        self.createTask(self.distributeScheduleViaBroadcast(),
                                        name='distribute schedule')

                    if self.config.colosseum_scenario is not None:
                        self.createTask(self.staticMandatedOutcomes(),
                                        name='static scenario mandated outcomes updates')
                        self.createTask(self.staticEnvironmentUpdates(),
                                        name='static scenario environment updates')

                    self.state = remote.ACTIVE
            except:
                logging.exception("Could not start radio")

    def scenario_path(self, component: str) -> str:
        assert self.config.colosseum_scenario is not None

        scenario = self.config.colosseum_scenario
        scenario_node = self.config.colosseum_scenario_node
        if scenario_node is None:
            scenario_node = 1

        return importlib_resources.files('colosseum.scenarios').joinpath(str(self.config.colosseum_scenario)).joinpath(component).joinpath("Node{scenario_node}{component}_{scenario}.json".format(scenario=scenario, scenario_node=scenario_node, component=component.replace("_", "")))

    async def staticMandatedOutcomes(self):
        if self.config.colosseum_scenario is None:
            return

        with open(self.scenario_path("Mandated_Outcomes"), "r") as f:
            updates = sorted(json.load(f), key=lambda x: x['timestamp'])

        for update in updates:
            delta = update["timestamp"]
            goals = update["scenario_goals"]

            deadline = self.scenario_start_time + delta
            now = time.time()

            await asyncio.sleep(deadline - now)

            self.updateMandatedOutcomes(goals, time.time())

    async def staticEnvironmentUpdates(self):
        if self.config.colosseum_scenario is None:
            return

        with open(self.scenario_path("Environment"), "r") as f:
            updates = sorted(json.load(f), key=lambda x: x['timestamp'])

        for update in updates:
            delta = update["timestamp"]
            envs = update["environment"]

            deadline = self.scenario_start_time + delta
            now = time.time()

            await asyncio.sleep(deadline - now)

            self.updateEnvironment(envs, time.time())

    async def stopRadio(self):
        """Stop the radio.

        This stops the radio, but the remote API will continue to function.
        """
        assert self.radio is not None

        if not self.done:
            try:
                self.done = True
                self.state = remote.STOPPING

                # Stop the collaboration server
                if self.collab_server is not None:
                    try:
                        await self.stopCollab()
                    except: # pylint: disable=bare-except
                        logger.exception('Could not gracefully terminate collaboration agent')

                # Stop tasks
                logger.info('Stopping tasks')
                await self.stopTasks()

                # Stop internal protocol server
                if self.internal_server_task:
                    logger.info('Stopping internal protocol server')
                    self.internal_server_task.cancel()
                    await asyncio.gather(self.internal_server_task, return_exceptions=True)

                # Close internal protocol client
                if self.internal_client is not None:
                    logger.info('Stopping internal protocol client')
                    self.internal_client.close()

                # Stop the gpsd client
                if self.gpsd_client is not None:
                    logger.info('Stopping gpsd client')
                    await self.gpsd_client.stop()

                # Dump timestamps
                self.saveTimestamps()

                # Dump score data if we are the gateway
                if self.is_gateway:
                    logger.info('Dumping final scoring data')
                    try:
                        # Dump reported scores
                        self.saveReportedMandatePerformance()
                    except: # pylint: disable=bare-except
                        logger.exception('Could not dump scoring data')

                # Terminate any packet captures
                await self.cleanupDumpcap()

                # Stop radio tasks
                logger.info('Stopping radio tasks')
                await self.radio.stopTasks()

                async with self.radio.lock:
                    # Remove all nodes
                    for node in self.neighbors.values():
                        await self.removeNode(node)

                # Close the logger
                if self.radio.logger is not None:
                    self.radio.logger.close()

                # Update radio state to FINISHED
                self.state = remote.FINISHED
                logger.info('Radio stopped')
            except:
                logger.exception('Exception when stopping radio')

    def terminate(self):
        """Stop the controller and all associated tasks"""
        self.loop.create_task(self._terminate())

    async def _terminate(self):
        """Terminate the radio"""
        logger.debug('Terminating')
        await self.stopRadio()

        # Stop listening for neighborhood events
        assert self.radio is not None
        assert self.radio.nhood is not None

        self.radio.nhood.removeListener(self)

        # Stop remote server
        logger.debug('Stopping remote protocol server')
        self.remote_server_task.cancel()
        await asyncio.gather(self.remote_server_task, return_exceptions=True)

        # Wait for remaining tasks and stop the event loop
        await dragonradio.tasks.stopEventLoop(self.loop, logger)

    def haveCollabInterface(self):
        """Determine whether or not we have a colaboration interface"""
        return self.config.collab_iface in netifaces.interfaces()

    def dumpcap(self, iface):
        """Start a dumpcap process for and interface"""
        if iface in netifaces.interfaces():
            async def f(controller):
                p = await asyncio.create_subprocess_exec(
                    'dumpcap', '-i', iface, '-q', '-w', self.pcapFile(iface),
                    stdin=None, stdout=None, stderr=None)
                controller.dumpcap_procs.append(p)

            self.dumpcap_tasks.append(self.loop.create_task(f(self)))

    async def cleanupDumpcap(self):
        """Terminate all dumpcap processes"""
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
                    except: # pylint: disable=bare-except
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
                            p = await asyncio.create_subprocess_exec(
                                'xz', self.pcapFile(iface),
                                stdin=None, stdout=None, stderr=None)
                            xzprocs.append(p)
                        except: # pylint: disable=bare-except
                            logger.exception('Could not xz %s', self.pcapFile(iface))

                await asyncio.gather(*[p.communicate() for p in xzprocs])

    def pcapFile(self, iface):
        """Get path to pcap file for interface"""
        return '{logdir}/{iface}.pcapng'.format(iface=iface,
                                                logdir=self.config.logdir)

    @property
    def this_node(self):
        """Return the current node"""
        assert self.radio is not None
        assert self.radio.nhood is not None

        return self.radio.nhood.me

    @property
    def is_gateway(self):
        """Is this the gateway?"""
        return self.this_node.is_gateway

    @property
    def neighbors(self) -> Dict[int, dragonradio.radio.Node]:
        """Return known nodes"""
        assert self.radio is not None
        assert self.radio.nhood is not None

        return self.radio.nhood.neighbors

    def neighborAdded(self, node : dragonradio.radio.Node):
        self.createTask(self.addNode(node), f"Add node {node.id:}")

    def neighborRemoved(self, node : dragonradio.radio.Node):
        pass

    def gatewayAdded(self, node : dragonradio.radio.Node):
        self.createTask(self.addGateway(node), f"Add gateway {node.id:}")

    async def addNode(self, node):
        """Add a node"""
        logger.info('Adding node %d', node.id)

        # Add a route for the new node
        if self.has_traffic_iface:
            self.addNodeRoute(node)

        # If new node is a gateway, connect to it and start sending status
        # updates
        if node.is_gateway:
            self.internal_client = InternalProtoClient(server_host=internalNodeIP(node.id),
                                                       loop=self.loop)

        # If we are the gateway, connect an internal protocol client to the
        # new node's server and update the schedule
        if self.is_gateway:
            internal_client = InternalProtoClient(server_host=internalNodeIP(node.id),
                                                  loop=self.loop)
            self.node_internal_clients[node.id] = internal_client

            self.tdma_reschedule.set()

    async def removeNode(self, node):
        """Remove a node"""
        logger.info('Removing node %d', node.id)

        if self.has_traffic_iface:
            if node.id != self.this_node.id:
                self.removeNodeRoute(node)

    async def addGateway(self, node):
        """Add a gateway"""
        logger.info('Adding gateway %d', node.id)

        # Connect to new gateway and start sending status updates
        self.internal_client = InternalProtoClient(server_host=internalNodeIP(node.id),
                                                   loop=self.loop)

    def addNodeRoute(self, node):
        dst = darpaNodeNet(node.id)
        mask = darpaNodeNetmask()
        nexthop = internalNodeIP(node.id)

        try:
            dragonradio.net.addRoute(dst, mask, nexthop)
        except:
            logger.exception('Could not add route: %s/%s %s', dst, mask, nexthop)

    def removeNodeRoute(self, node):
        dst = darpaNodeNet(node.id)
        mask = darpaNodeNetmask()

        try:
            dragonradio.net.deleteRoute(dst, mask)
        except:
            logger.exception('Could not delete route: %s', dst)

    async def logNetworkInfo(self):
        """Log useful network information"""
        # Log ARP table
        p = await asyncio.create_subprocess_exec('arp', '-an',
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE)
        stdout, _stderr = await p.communicate()
        logger.info('ARP table:\n%s', stdout.decode('utf-8'))

        # Log routes
        p = await asyncio.create_subprocess_exec('ip', 'route',
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE)
        stdout, _stderr = await p.communicate()
        logger.info('Routing table:\n%s', stdout.decode('utf-8'))

        # Log ip links
        p = await asyncio.create_subprocess_exec('ip', 'a',
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE)
        stdout, _stderr = await p.communicate()
        logger.info('IP links:\n%s', stdout.decode('utf-8'))

        # Log routes
        for node_id in self.neighbors:
            for octet in [1]:
                ip = darpaNodeIP(node_id, octet)

                p = await asyncio.create_subprocess_exec('ip', 'route', 'get', ip,
                    stdout=asyncio.subprocess.PIPE,
                    stderr=asyncio.subprocess.PIPE)
                stdout, _stderr = await p.communicate()
                logger.info('IP route for %s:\n%s', ip, stdout.decode('utf-8'))

    def getDestinations(self, node_id: NodeId) -> Set[NodeId]:
        """Return a list of nodes given node transmits to"""
        destinations = set()

        for _, link in self.flow_links.items():
            src, dest = link
            # Don't include broadcasts
            if node_id == src and dest != 255:
                destinations.add(dest)

        return destinations

    async def installMACSchedule(self, seq, sched):
        """Install a new MAC schedule"""
        assert self.radio is not None
        assert self.radio.nhood is not None

        async with self.radio.lock:
            radio = self.radio

            if self.schedule_seq is not None and seq <= self.schedule_seq:
                logger.info('Skipping schedule with sequence number %d (currently at %d)',
                    seq, self.schedule_seq)
                return

            if self.schedule is None or not np.array_equal(sched, self.schedule):
                if not self.bootstrapped:
                    logger.info('Switching to TDMA MAC')
                    self.bootstrapped = True
                    radio.setMAC(self.config.mac_class)

                # Make sure the Neighborhood is aware of all nodes in the schedule
                nodes_with_slot = set(sched.flatten())
                if 0 in nodes_with_slot:
                    nodes_with_slot.remove(0)

                if not self.config.manet:
                    for node_id in nodes_with_slot:
                        self.radio.nhood.addNeighbor(node_id)

                radio.installMACSchedule(sched, self.config.mac_class)
                self.schedule = sched

            self.schedule_seq = seq

    async def updateVoxelUsage(self, vox: Voxel):
        """Get voxel usage

        Arguments:
            node_id: The node ID of the transmitting node
            vox: The voxel
        """
        config = self.config

        async with self.voxel_lock:
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
                    vox.rx = list(self.getDestinations(vox.tx))

    async def alohaToVoxels(self):
        """Determine voxel usage for ALOHA"""
        assert self.radio is not None

        radio = self.radio

        voxels = []

        for chan in radio.channels:
            transmitters = set(self.neighbors)

            for tx in transmitters:
                vox = channelToVoxel(tx, radio.frequency, chan)

                await self.updateVoxelUsage(vox)

                if vox.duty_cycle != 0:
                    voxels.append(vox)

        return voxels

    async def scheduleToVoxels(self, sched):
        """Determine voxel usage from schedule"""
        assert self.radio is not None

        radio = self.radio

        (nchannels, _nslots) = sched.shape

        voxels = []

        for chanidx in range(0, nchannels):
            chan = radio.channels[chanidx]

            transmitters = set(sched[chanidx])
            if 0 in transmitters:
                transmitters.remove(0)

            for tx in transmitters:
                vox = channelToVoxel(tx, radio.frequency, chan)

                await self.updateVoxelUsage(vox)

                if vox.duty_cycle != 0:
                    voxels.append(vox)

        return voxels

    def cacheTrafficInterfaceARP(self):
        """Get all addresses on the traffic interface's subnet into the ARP
        cache.

        See:
            https://sc2colosseum.freshdesk.com/support/solutions/articles/22000220402-traffic-generation
        """
        if self.has_traffic_iface:
            logger.debug('Caching ARP table entries for traffic interface')
            assert self.radio is not None

            node_id = self.radio.node_id

            for peer_id in range(1, 255):
                ip = darpaNodeIP(node_id, peer_id)
                mac = darpaNodeMAC(node_id, peer_id)

                try:
                    dragonradio.net.addStaticARPEntry(self.config.traffic_iface, ip, mac)
                except:
                    logger.exception('Could not cache ARP table entry: %s %s', ip, mac)

    async def getMandatePerformance(self):
        """Compute mandate performance metrics"""
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
        assert self.scorer is not None

        async with self.scorer_lock:
            self.scorer.updateScore(self.currentMP())

            scores = self.scorer.scores

        # Updated mandated outcomes using scoring data from measurement period
        # mp
        mandates_achieved: int = 0
        total_score_achieved: float = 0
        mandates: List[dragonradio.radio.Mandate] = []

        for flow_uid, mandate in self.mandates.items():
            mandates.append(mandate)

            # Get radio IDs involved in this flow
            if flow_uid in self.flow_links:
                (src, dest) = self.flow_links[flow_uid]
                mandate.radio_ids = [src, dest]
            else:
                mandate.radio_ids = []

            # Get score for this flow at this MP
            score = scores[flow_uid][mp]

            logger.debug(("Score: "
                          "flow_uid=%s; "
                          "mp=%s; "
                          "update_timestamp_sent=%s; "
                          "npackets_sent=%s; "
                          "update_timestamp_recv=%s; "
                          "npackets_recv=%s"),
                flow_uid,
                mp,
                score.update_timestamp_sent,
                score.npackets_sent,
                score.update_timestamp_recv,
                score.npackets_recv)

            # Calculate scalar performance, mandates achieved, and total score achieved
            mandate.achieved_duration = float(score.achieved_duration)

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
            self.logCSV(self.reported_mandate_performance,
                        ['mp', 'mandates_achieved', 'total_score_achieved'],
                        'score_reported.csv')

    def saveTimestamps(self):
        assert self.radio is not None

        if self.config.logdir is None:
            return

        path = Path(self.config.logdir) / 'timestamps.h5'

        if self.radio.timesync is not None:
            (sigma, delta, tau) = self.radio.timesync

            df_timesync = pd.DataFrame([(delta, sigma, tau)],
                                       columns=['delta', 'sigma', 'tau'])
            save_dataframe(df_timesync, path, 'timesync')

        if len(self.radio.me_timestamps) != 0:
            df_me_timestamps = pd.DataFrame(self.radio.me_timestamps, columns=['t_send', 't_recv'], dtype=float)
            save_dataframe(df_me_timestamps, path, 'me_timestamps')

        if len(self.radio.master_timestamps) != 0:
            df_master_timestamps = pd.DataFrame(self.radio.master_timestamps, columns=['t_send', 't_recv'], dtype=float)
            save_dataframe(df_master_timestamps, path, 'master_timestamps')

    def logCSV(self, data, columns, filename):
        """Log data to CSV file"""
        if self.config.logdir is None:
            return

        try:
            df = pd.DataFrame(data, columns=columns)
            logger.info('Logging to %s', filename)
            df.to_csv(os.path.join(self.config.logdir, filename), index=False)
        except: # pylint: disable=bare-except
            logger.exception('Exception when writing %s', filename)

    def updateGoals(self, goals, _timestamp):
        """Update mandate goals"""
        logger.debug('Updating goals')

        # Update mandated outcomes
        mandates = dragonradio.radio.MandateMap()

        for goal in goals:
            flow_uid = goal['flow_uid']
            hold_period = float(goal['hold_period'])
            point_value = goal.get('point_value', 1)
            max_latency_s = goal['requirements'].get('max_latency_s', None)
            min_throughput_bps = goal['requirements'].get('min_throughput_bps', None)
            file_transfer_deadline_s = goal['requirements'].get('file_transfer_deadline_s', None)

            if max_latency_s is not None:
                max_latency_s = float(max_latency_s)

            if file_transfer_deadline_s is not None:
                file_transfer_deadline_s = float(file_transfer_deadline_s)

            mandates[flow_uid] = dragonradio.radio.Mandate(flow_uid,
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
        assert self.radio is not None

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
                        await self.internal_client.sendStatus(self.this_node,
                                                              sources,
                                                              sinks,
                                                              spectrum)

                await asyncio.sleep(config.status_update_period)
            except asyncio.CancelledError:
                return
            except: # pylint: disable=bare-except
                logger.exception('Exception when updating status')

    async def getFlowStatistics(self):
        """Get flow statistics for this node.

        Return:
            A pair of lists of internal.FlowStats values representing flow
            statistics for sources and sinks, respectively, for this node.
        """
        assert self.radio is not None

        config = self.config
        radio = self.radio

        # Determine minimum and maximum MP for which we will report
        # flow statistics
        min_mp = self.max_reported_mp + 1
        max_mp = self.timeToMP(time.time() - config.stats_ignore_window)
        self.max_reported_mp = max_mp

        # Get local flow statistics
        reset_stats = self.is_gateway or self.internal_client is not None

        sources = [mkFlowStats(min_mp, max_mp, p) \
            for p in radio.flowperf.getSources(reset_stats).values() \
            if p.low_mp is not None]
        sinks = [mkFlowStats(min_mp, max_mp, p) \
            for p in radio.flowperf.getSinks(reset_stats).values() \
            if p.low_mp is not None]

        return (sources, sinks)

    async def getSpectrumStatistics(self):
        """Get spectrum usage statistics for this node.

        Return:
            A list of internal.SpectrumStats values representing spectrum usage
            statistics for this node.
        """
        assert self.radio is not None

        config = self.config
        radio = self.radio
        mac = radio.mac

        spectrum_usage = []

        if mac is not None:
            if config.tx_upsample:
                channels = radio.channels
            else:
                assert radio.tx_channel_idx is not None
                channels = [radio.channels[radio.tx_channel_idx]]

            # Get per-channel load from MAC
            load = mac.popLoad()

            # Get current TX sample rate
            fs = radio.usrp.tx_rate

            # Calculate load denominator: the number of samples that could've
            # been transmitted over the loaded period.
            nsamples = fs*load.period

            voxels: List[Voxel] = []

            for idx, chan in enumerate(channels):
                if idx < len(load.nsamples):
                    duty_cycle = load.nsamples[idx]/nsamples

                    if duty_cycle != 0:
                        vox = channelToVoxel(radio.node_id, radio.frequency, chan)
                        vox.duty_cycle = duty_cycle
                        voxels.append(vox)

            if len(voxels) != 0:
                spectrum_usage.append(mkSpectrumStats(load.start, load.end, voxels))

        return spectrum_usage

    async def updateFlowStatistics(self, node_id: int, timestamp: float,
                                   sources: Sequence[FlowStats],
                                   sinks: Sequence[FlowStats]):
        """Update flow statistics from reported source and sink stats"""
        assert self.scorer is not None

        async with self.scorer_lock:
            for flow in sources:
                # Record maximum MP
                max_mp = flow.first_mp + min(len(flow.npackets), len(flow.nbytes)) - 1
                self.stats_max_mp[node_id] = max(self.stats_max_mp.get(node_id, 0), max_mp)

                logger.info(("Updating source flow statistics: "
                             "node=%d; "
                             "flow=%d; "
                             "timestamp=%f; "
                             "current mp=%d; "
                             "first_mp=%d; "
                             "max_mp=%d; "
                             "npackets=%s; "
                             "nbytes=%s"),
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

                logger.info(("Updating sink flow statistics: "
                             "node=%d; "
                             "flow=%d; "
                             "timestamp=%f; "
                             "current mp=%d; "
                             "first_mp=%d; "
                             "max_mp=%d; "
                             "npackets=%s; "
                             "nbytes=%s"),
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
                                                     list(flow.npackets),
                                                     list(flow.nbytes))

    async def updateSpectrumStatistics(self,
                                       node_id: NodeId,
                                       _timestamp: float,
                                       reports: Sequence[internal.SpectrumStats]):
        """Update flow statistics from reported source and sink stats"""
        load: Load = {}
        historical_usage: List[VoxelUsage] = []

        for spectrum in reports:
            for usage in spectrum.voxels:
                vox = Voxel()
                vox.f_start = usage.f_start
                vox.f_end = usage.f_end
                vox.duty_cycle = usage.duty_cycle
                vox.tx = node_id
                vox.rx = list(self.getDestinations(node_id))

                historical_usage.append((getTimestamp(spectrum.start),
                                         getTimestamp(spectrum.end),
                                         vox))

                load[(vox.f_start, vox.f_end)] = usage.duty_cycle

                logger.debug("Spectrum usage: start=%s; end=%s; voxel=%s",
                    getTimestamp(spectrum.start),
                    getTimestamp(spectrum.end),
                    vox)

        async with self.voxel_lock:
            self.historical_voxel_usage += historical_usage
            self.current_voxel_usage[node_id] = load

    async def createSchedule(self, nslots=10):
        """Create a new TDMA schedule"""
        assert self.radio is not None

        radio = self.radio

        if self.config.mac == 'fdma':
            nslots = 1

        # We start out without any channel assignments
        assignments = {}

        while not self.done:
            try:
                await self.tdma_reschedule.wait()
                self.tdma_reschedule.clear()

                logger.debug('Creating schedule')

                # Get all nodes we know about
                self.schedule_nodes = list(self.neighbors.keys())
                self.schedule_nodes.sort()

                # Make sure we are first in the list so we always get the same
                # channel
                self.schedule_nodes.remove(radio.node_id)
                self.schedule_nodes = [radio.node_id] + self.schedule_nodes

                # Create the schedule
                nchannels = len(radio.channels)
                sched, assignments = dragonradio.schedule.fairMACSchedule(nchannels,
                                                                          nslots,
                                                                          self.schedule_nodes,
                                                                          assignments=assignments)

                if not np.array_equal(sched, self.schedule):
                    await self.installMACSchedule(self.schedule_seq + 1, sched)
                    await self.distributeSchedule()
            except asyncio.CancelledError:
                return
            except: # pylint: disable=bare-except
                logger.exception('Exception while creating schedule')

    async def distributeSchedule(self):
        """Distribute the TDMA schedule to known nodes"""
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
            if node_id not in self.node_internal_clients:
                logger.error('No internal protocol client for node %d', node_id)
                continue

            internal_client = self.node_internal_clients[node_id]
            await internal_client.sendSchedule(self.schedule_seq,
                                               self.schedule_nodes,
                                               self.schedule,
                                               self.config.frequency,
                                               self.config.bandwidth,
                                               self.scenario_start_time)

        # Now broadcast a few times for robustness
        for _ in range(0, 10):
            await asyncio.sleep(0.5)
            await self.broadcastSchedule()

    async def distributeScheduleViaBroadcast(self):
        """Distribute the TDMA schedule via broadcast"""
        while not self.done:
            try:
                await asyncio.sleep(10)
                await self.broadcastSchedule()
            except asyncio.CancelledError:
                return
            except: # pylint: disable=bare-except
                logger.exception('Exception when broadcasting schedule')

    async def broadcastSchedule(self):
        """Broadcast the TDMA schedule"""
        if self.schedule is None:
            return

        if self.internal_client is None:
            return

        await self.internal_client.sendSchedule(self.schedule_seq,
                                                self.schedule_nodes,
                                                self.schedule,
                                                self.config.frequency,
                                                self.config.bandwidth,
                                                self.scenario_start_time)

    async def setBandwidthAndFrequency(self, bandwidth, frequency):
        """Reconfigure bandwidth and frequency.

        If we are the gateway node, this will trigger a new TDMA schedule.
        """
        assert self.radio is not None

        async with self.radio.lock:
            if bandwidth != self.config.bandwidth or frequency != self.config.frequency:
                old_bandwidth = self.radio.bandwidth

                self.radio.setBandwidthAndFrequency(bandwidth, frequency)
                if self.collab_server is not None:
                    self.forceSpectrumUsageUpdate()

                # If only the center frequency has changed, keep the old
                # schedule. Otherwise create a new schedule.
                if self.radio.bandwidth != old_bandwidth:
                    if self.bootstrapped:
                        assert self.radio.mac is not None
                        self.radio.mac.schedule = []

                    if self.is_gateway:
                        # Force new schedule
                        self.schedule = None
                        self.tdma_reschedule.set()

    async def bootstrapNetwork(self):
        """Task to bootstrap the network after neighbor discovery"""
        config = self.config

        try:
            # Sleep for the discovery interval
            await asyncio.sleep(config.neighbor_discovery_period)

            # Log network info
            try:
                await self.logNetworkInfo()
            except: #pylint: disable=bare-except
                logger.exception('Error while logging network info')

            # Start the schedule creation task
            self.createTask(self.createSchedule(), name='create schedule')

            # Trigger TDMA scheduler
            self.tdma_reschedule.set()
        except asyncio.CancelledError:
            return
        except:
            logger.exception('Exception while bootstrapping network')
            raise

    async def discoverNeighbors(self):
        """Task to discover neighbors"""
        assert self.radio is not None

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

                if isinstance(radio.controller, dragonradio.radio.SmartController):
                    radio.controller.broadcastHello()

                await asyncio.sleep(period - delta)
            except asyncio.CancelledError:
                return
            except: # pylint: disable=bare-except
                logger.exception('Exception while discovering neighbors')

    async def synchronizeClock(self):
        """Task to synchronize clock with the gateway node"""
        assert self.radio is not None

        radio = self.radio
        config = self.config

        while not self.done:
            try:
                assert config.clock_sync_period is not None
                await asyncio.sleep(config.clock_sync_period)

                radio.synchronizeClock()
            except asyncio.CancelledError:
                return
            except: # pylint: disable=bare-except
                logger.exception('Exception while synchronizing clock')

    async def getHistoricalSpectrumUsage(self):
        """Get historical voxel usage from controller and convert it into CIL
        voxels for spectrum usage report"""
        assert self.radio is not None

        async with self.voxel_lock:
            voxels = self.historical_voxel_usage
            self.historical_voxel_usage = []

        rx_gain = self.radio.usrp.rx_gain
        tx_gain = self.radio.usrp.tx_gain

        return [vox.toCILVoxel(start, end, rx_gain, tx_gain, True) for (start, end, vox) in voxels]

    async def getPredictedSpectrumUsage(self, when):
        """Get predicated voxel usage from controller and convert it into CIL
        voxels for spectrum usage report"""
        assert self.radio is not None

        mac = self.radio.mac

        if mac is None:
            voxels = []
        elif isinstance(mac, dragonradio.radio.SlottedALOHA):
            voxels = await self.alohaToVoxels()
        else:
            voxels = await self.scheduleToVoxels(self.schedule)

        start = when
        end = when + self.config.spec_future_period
        rx_gain = self.radio.usrp.rx_gain
        tx_gain = self.radio.usrp.tx_gain

        return [vox.toCILVoxel(start, end, rx_gain, tx_gain, False) for vox in voxels]

    @handle('Request.radio_command')
    def _handleRadioCommand(self, req):
        """Handle a remote radio command"""
        info = ''

        if req.radio_command == remote.START:
            if self.state == remote.READY:
                logger.info("Radio start: timestamp=%f",
                    req.timestamp)

                self.createTask(self.startRadio(timestamp=req.timestamp), "Start radio")
                info = 'Radio started'
        elif req.radio_command == remote.STOP:
            if self.state == remote.READY or self.state == remote.ACTIVE:
                logger.info("Radio stop: timestamp=%f",
                    req.timestamp)

                self.loop.create_task(self.stopRadio())
                info = 'Radio stopping'

        resp = remote.Response()
        resp.status.state = self.state
        resp.status.info = info
        return resp

    @handle('Request.update_mandated_outcomes')
    def _handleUpdateMandatedOutcomes(self, req):
        """Handle mandated outcomes update"""
        # Parse goals as JSON
        goals = json.loads(req.update_mandated_outcomes.goals)

        # Update mandated outcomes
        self.updateMandatedOutcomes(goals, req.timestamp)

        resp = remote.Response()
        resp.status.state = self.state
        resp.status.info = 'Mandated outcomes updated'
        return resp

    def updateMandatedOutcomes(self, goals, timestamp):
        logger.info('Update mandated outcomes: timestamp=%f; delta=%f\n%s',
                    timestamp,
                    timestamp - self.scenario_start_time,
                    json.dumps(goals))

        # Create scoring info from goals
        self.loop.run_in_executor(None, self.updateGoals, goals, timestamp)

    def setMandates(self, mandates):
        """Set our mandated outcomes and update flowperf"""
        assert self.radio is not None

        radio = self.radio

        # Set our mandated outcomes
        self.mandates = mandates

        # Set flow performance mandates
        radio.flowperf.mandates = mandates

        # Set scorer mandates
        if self.scorer:
            self.scorer.setMandates(mandates)

        # Update Mandate queue
        if isinstance(radio.netq, dragonradio.radio.MandateQueue):
            # Add mandates
            radio.netq.mandates = mandates

            # Make internal traffic very high priority
            radio.netq.setFlowQueuePriority(internal.INTERNAL_PORT, (99, 0.0))
            radio.netq.setFlowQueueType(internal.INTERNAL_PORT, dragonradio.radio.MandateQueue.FIFO)
        else:
            # Set allowed flows
            self.setAllowedFlows([flow_uid for (flow_uid, _mandate) in mandates.items()])

    def setAllowedFlows(self, flows):
        """Decide which flows are allowed by the firewall.

        The specified flows, the internal control port, and all broadcast
        packets will be allowed through the packet firewall.
        """
        assert self.radio is not None

        radio = self.radio

        allowed = set(flows)
        allowed.add(internal.INTERNAL_PORT)

        radio.netfirewall.allow_broadcasts = True
        radio.netfirewall.allowed = allowed
        radio.netfirewall.enabled = True

    @handle('Request.update_environment')
    def _handleUpdateEnvironment(self, req):
        """Handle an environment update"""
        envs = json.loads(req.update_environment.environment)

        # Update environment
        self.updateEnvironment(envs, req.timestamp)

        resp = remote.Response()
        resp.status.state = self.state
        resp.status.info = 'Environment updated'
        return resp

    def updateEnvironment(self, envs, timestamp):
        assert self.radio is not None

        logger.info('Update environment: timestamp=%f; delta=%f\n%s',
                    timestamp,
                    timestamp - self.scenario_start_time,
                    json.dumps(envs))

        self.radio.environmentDiscontinuity()

        # Environment messages contain a *list* of updates...
        for env in envs:
            bandwidth = env.get('scenario_rf_bandwidth', self.config.bandwidth)
            frequency = env.get('scenario_center_frequency', self.config.frequency)

            self.scoring_percent_threshold = env.get('scoring_percent_threshold', 0)
            self.scoring_point_threshold = env.get('scoring_point_threshold', 0)

            self.createTask(self.setBandwidthAndFrequency(bandwidth, frequency),
                            "Reconfigure bandwidth and frequency")

    @handle('Message.status')
    def _handleStatus(self, msg):
        """Handle a status message from another node"""
        node_id = msg.status.radio_id

        # Update node location
        if node_id in self.neighbors:
            n = self.neighbors[node_id]
            loc = msg.status.loc

            n.loc.lat = loc.location.latitude
            n.loc.lon = loc.location.longitude
            n.loc.alt = loc.location.elevation
            n.loc.timestamp = getTimestamp(loc.timestamp)

        # Update statistics
        self.createTask(self.updateFlowStatistics(node_id,
                                                  getTimestamp(msg.status.timestamp),
                                                  msg.status.source_flows,
                                                  msg.status.sink_flows))
        self.createTask(self.updateSpectrumStatistics(node_id,
                                                      getTimestamp(msg.status.timestamp),
                                                      msg.status.spectrum_stats))

    @handle('Message.schedule')
    def _handleSchedule(self, msg):
        """Handle a schedule message from gateway."""
        assert self.radio is not None

        config = self.config
        radio = self.radio

        self.scenario_start_time = msg.schedule.scenario_start_time

        if radio.node_id in msg.schedule.nodes:
            if msg.schedule.bandwidth != config.bandwidth or \
                msg.schedule.frequency != config.frequency:
                logger.info(("Not installing schedule with frequency parameters "
                             "(bw=%g; fc=%g) "
                             "different from ours (bw=%g; fc=%g)"),
                            msg.schedule.bandwidth,
                            msg.schedule.frequency,
                            config.bandwidth,
                            config.frequency)
                return

            nchannels = msg.schedule.nchannels
            nslots = msg.schedule.nslots

            sched = np.array(msg.schedule.schedule).reshape((nchannels, nslots))

            self.createTask(self.installMACSchedule(msg.schedule.seq, sched), "Install MAC schedule")
