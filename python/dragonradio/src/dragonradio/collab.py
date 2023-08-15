# Copyright 2018-2023 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

"""Collaboration support"""
import asyncio
from asyncio import AbstractEventLoop
import logging
import socket
import struct
import time
from typing import Dict, Optional, Sequence, Tuple

from google.protobuf.message import Message

import sc2.cil_pb2 as cil
import sc2.registration_pb2 as registration

import dragonradio.radio
try:
    from dragonradio.radio.radio import Config, Radio
except:
    pass

from dragonradio.protobuf.handler import ProtobufHandler, handle, handler, send
from dragonradio.protobuf.timestamp import getTimestamp, setTimestamp
from dragonradio.protobuf.zmq import ZMQProtoClient, ZMQProtoServer
import dragonradio.tasks

logger = logging.getLogger('collab')

CIL_VERSION: Tuple[int, int, int] = (3, 6, 0)
"""CIL protocol version"""

MAX_LOCATION_AGE: float = 45
"""Maximum location age (sec)"""

SPECTRUM_USAGE_MAX_RATE: float = 30.5
"""Maximum time allowed between spectrum updates (sec)"""

SPECTRUM_USAGE_MIN_RATE: float = 0.5
"""Minimum time allowed between spectrum updates (sec)"""

SPECTRUM_VOXEL_MAX_TIME_OFFSET: float = 60.5
"""Maximum offset between CIL report timestamp and voxel timestamp"""

def ipIntToString(ip_int: int) -> str:
    """Convert integer formatted IP to IP string"""
    return socket.inet_ntoa(struct.pack('!L', ip_int))

def ipStringToInt(ip_string: str) -> int:
    """Convert string formatted IP to IP int"""
    return struct.unpack('!L', socket.inet_aton(ip_string))[0]

# See:
#   https://stackoverflow.com/questions/49622924/wait-for-timeout-or-event-being-set-for-asyncio-event
async def wait_for_event(evt: asyncio.Event, timeout: float) -> bool:
    """Wait for an event with a timeout"""
    try:
        await asyncio.wait_for(evt.wait(), timeout)
    except asyncio.TimeoutError:
        pass

    return evt.is_set()

def mkReceiverInfo(radio_id: int, rx_gain: float) -> cil.ReceiverInfo:
    """Make a CIL ReceiverInfo object"""
    rx_info = cil.ReceiverInfo()
    rx_info.radio_id = radio_id
    rx_info.power_db.value = rx_gain
    return rx_info

class RegistrationClient(ZMQProtoClient):
    """CIL registration client"""
    def __init__(self, **kwargs):
        super().__init__(**kwargs)

    @send(registration.TalkToServer)
    async def register(self, msg: registration.TalkToServer, local_ip: str):
        """Send a register message"""
        msg.register.my_ip_address = ipStringToInt(local_ip)

    @send(registration.TalkToServer)
    async def keepalive(self, msg: registration.TalkToServer, nonce: int):
        """Send a keepalive message"""
        msg.keepalive.my_nonce = nonce

    @send(registration.TalkToServer)
    async def leave(self, msg: registration.TalkToServer, nonce: int):
        """Send a leave message"""
        msg.leave.my_nonce = nonce

class CILClient(ZMQProtoClient):
    """A CIL protocol client. Used to communicate with CIL peers."""
    def __init__(self, local_ip, peer_host, peer_port, **kwargs):
        super().__init__(server_host=peer_host,
                         server_port=peer_port,
                         **kwargs)
        self.local_ip = local_ip
        self.msg_count = 0
        self.open()
        self.loop.create_task(self.hello())

    async def send_msg(self, msg: Message):
        if isinstance(msg, cil.CilMessage):
            self.msg_count += 1

            msg.sender_network_id = ipStringToInt(self.local_ip)
            msg.msg_count = self.msg_count
            msg.network_type.network_type = cil.NetworkType.COMPETITOR

            if not msg.HasField('timestamp'):
                setTimestamp(msg.timestamp, time.time())

        await super().send_msg(msg)

    @send(cil.CilMessage)
    async def hello(self, msg: cil.CilMessage):
        """Send a hello message"""
        msg.hello.version.major = CIL_VERSION[0]
        msg.hello.version.minor = CIL_VERSION[1]
        msg.hello.version.patch = CIL_VERSION[2]

    @send(cil.CilMessage)
    async def locationUpdate(self,
                             msg: cil.CilMessage,
                             locations: Sequence[cil.LocationInfo]):
        """Send a location update message"""
        msg.location_update.locations.extend(locations)

    @send(cil.CilMessage)
    async def spectrumUsage(self,
                            msg: cil.CilMessage,
                            usage: Sequence[cil.SpectrumVoxelUsage],
                            timestamp: Optional[float]=None):
        """Send a spectrum usage message"""
        # Explicitly choose a timestamp so we know what it is to check for valid
        # usage
        if timestamp is None:
            timestamp = time.time()

        setTimestamp(msg.timestamp, timestamp)

        # Only send valid voxel usage
        def valid(usage: cil.SpectrumVoxelUsage) -> bool:
            time_start = getTimestamp(usage.spectrum_voxel.time_start)
            if abs(time_start - timestamp) > SPECTRUM_VOXEL_MAX_TIME_OFFSET:
                return False

            if usage.spectrum_voxel.HasField('time_end'):
                time_end = getTimestamp(usage.spectrum_voxel.time_end)
                if time_end < time_start or abs(time_end - timestamp) > SPECTRUM_VOXEL_MAX_TIME_OFFSET:
                   return False

            return True

        msg.spectrum_usage.voxels.extend([u for u in usage if valid(u)])

    @send(cil.CilMessage)
    async def detailedPerformance(self,
                                  msg: cil.CilMessage,
                                  performance: Sequence[cil.MandatePerformance],
                                  mandates_achieved: int,
                                  total_score_achieved: int,
                                  scoring_point_threshold: int,
                                  timestamp: Optional[float]=None):
        """Send a detailed performance message"""
        # Of we don't have a timestamp, pretend this performance metric is from
        # 5 seconds ago
        if timestamp is None:
            timestamp = time.time() - 5

        msg.detailed_performance.mandate_count = len(performance)
        setTimestamp(msg.detailed_performance.timestamp, timestamp)
        msg.detailed_performance.mandates.extend(performance)
        msg.detailed_performance.mandates_achieved = mandates_achieved
        msg.detailed_performance.total_score_achieved = total_score_achieved
        msg.detailed_performance.scoring_point_threshold = scoring_point_threshold

@handler(registration.TellClient)
@handler(cil.CilMessage)
class CILServer(dragonradio.tasks.TaskManager, ProtobufHandler):
    """Base class for CIL server."""

    config: 'Config'
    """Radio configuration"""

    radio: Optional['Radio'] = None
    """Radio"""

    done: bool = False
    """Is the radio done?"""

    #neighbors: Dict[int, dragonradio.radio.Node]
    """Neighbors"""

    registration_client: RegistrationClient
    """Registration client"""

    registration_nonce: Optional[int] = None
    """Registration server nonce"""

    registration_max_keepalive: float = 30.0
    """Maximum time between registration heartbeats"""

    collab_server: Optional[ZMQProtoServer] = None
    """CIL server"""

    collab_ip: str
    """Local IP address of collaboration server"""

    collab_peers: Dict[str, CILClient]
    """CIL peers"""

    collab_spectrum_update_event: asyncio.Event
    """Event used to signal CIL spectrum update"""

    scenario_started: bool = False
    """Have we received mandates?"""

    _scenario_start_time: float
    """RF scenario start time, in seconds since the epoch"""

    scoring_percent_threshold: float = 0
    """Scoring percent threshold"""

    scoring_point_threshold: int = 0
    """Scoring point threshold"""

    def __init__(self, loop: Optional[AbstractEventLoop]=None):
        ProtobufHandler.__init__(self)
        dragonradio.tasks.TaskManager.__init__(self, loop)

        self.collab_peers = {}

    @property
    def scenario_start_time(self) -> float:
        """RF scenario start time, in seconds since the epoch"""
        return self._scenario_start_time

    @scenario_start_time.setter
    def scenario_start_time(self, t: float):
        """Set scenario start time"""
        logger.info('RF scenario start time set: %f', t)
        self._scenario_start_time = t
        if self.radio is not None:
            self.radio.flowperf.start = t

    def startCollab(self):
        """Start CIL server"""
        self.registration_client = RegistrationClient(loop=self.loop,
                                                      server_host=self.config.collab_server_ip,
                                                      server_port=self.config.collab_server_port)
        self.registration_client.open()

        self.collab_server = ZMQProtoServer(self, loop=self.loop)
        self.addTask(self.collab_server.start_server(cil.CilMessage,
                                                     self.collab_ip,
                                                     self.config.collab_peer_port))
        self.addTask(self.collab_server.start_server(registration.TellClient,
                                                     self.collab_ip,
                                                     self.config.collab_client_port))

        self.collab_spectrum_update_event = asyncio.Event()

        self.loop.create_task(self.startCollabTasks())

    async def startCollabTasks(self):
        """Register with the CIL server and then start collaboration tasks"""
        await self.registration_client.register(self.collab_ip)

        self.createTask(self._heartbeatTask(), name='heartbeat')
        self.createTask(self._locationUpdateTask(), name='location update')
        self.createTask(self._spectrumUsageTask(), name='spectrum usage')
        self.createTask(self._detailedPerformanceTask(), name='detailed performance')

    async def stopCollab(self):
        """Stop CIL server"""
        # We must trigger the event, otherwise we will be stuck waiting on it
        # forever
        self.collab_spectrum_update_event.set()

        # Leave the collaboration network
        if self.registration_nonce is not None:
            logger.info('Leaving collaboration network')
            try:
                await self.registration_client.leave(self.registration_nonce)
            except: #pylint: disable=bare-except
                logger.exception('Could not gracefully terminate collaboration agent')

        # Close collaboration clients
        logger.info('Closing CIL peer connections')
        for _, peer in self.collab_peers.items():
            peer.close()

    def addPeer(self, peer_ip):
        """Add a collaboration peer"""
        self.collab_peers[peer_ip] = CILClient(self.collab_ip,
                                               peer_ip,
                                               self.config.collab_peer_port,
                                               loop=self.loop)
        logger.info('Adding peer %s', peer_ip)

    def removePeer(self, peer_ip):
        """Remove a collaboration peer"""
        del self.collab_peers[peer_ip]
        logger.info('Removing peer %s', peer_ip)

    def setNeighbors(self, neighbors):
        """Set collaboration neighbors"""
        neighbors = [ipIntToString(n) for n in neighbors]

        old_neighbors = set(self.collab_peers.keys())
        new_neighbors = set(neighbors)
        new_neighbors.remove(self.collab_ip)

        for p in old_neighbors - new_neighbors:
            self.removePeer(p)

        for p in new_neighbors - old_neighbors:
            self.addPeer(p)

    async def getHistoricalSpectrumUsage(self):
        """Get historical voxel usage"""
        return []

    async def getPredictedSpectrumUsage(self, _when):
        """Get predicated voxel usage"""
        return []

    async def getMandatePerformance(self):
        """Get mandate performance"""
        return []

    def forceSpectrumUsageUpdate(self):
        """Force a CIL spectrum usage update"""
        logger.debug('Forcing calculation of spectrum usage at time %f',
                     time.time() - self.scenario_start_time)
        self.collab_spectrum_update_event.set()

    async def _heartbeatTask(self):
        """Task to send heartbeat messages"""
        try:
            while not self.done:
                if self.registration_nonce is not None:
                    try:
                        await self.registration_client.keepalive(self.registration_nonce)
                    except: #pylint: disable=bare-except
                        logger.exception("Exception when sending heartbeat")

                await asyncio.sleep(self.registration_max_keepalive / 2)
        except asyncio.CancelledError:
            pass

    async def _locationUpdateTask(self):
        """Task to send location updates"""
        config = self.config

        while not self.done:
            try:
                # Calculate locations
                locations = []

                for n in self.neighbors.values():
                    if n.loc.timestamp > time.time() - MAX_LOCATION_AGE:
                        info = cil.LocationInfo()
                        info.radio_id = n.id
                        info.location.latitude = n.loc.lat
                        info.location.longitude = n.loc.lon
                        info.location.elevation = n.loc.alt
                        setTimestamp(info.timestamp, n.loc.timestamp)

                        locations.append(info)

                # Send location update to all peers
                logger.info('CIL: sending location update')
                for _ip, p in self.collab_peers.items():
                    try:
                        await p.locationUpdate(locations)
                    except: #pylint: disable=bare-except
                        logger.exception("Exception when sending location update")

                await asyncio.sleep(config.location_update_period)
            except asyncio.CancelledError:
                return
            except: #pylint: disable=bare-except
                logger.exception("Exception when sending location updates")

    async def _spectrumUsageTask(self):
        """Task to periodically send spectrum usage"""
        config = self.config

        # Timestamp of last spectrum update
        timestamp = None

        while not self.done:
            try:
                if self.scenario_started:
                    timestamp = time.time()

                    voxels = await self.getHistoricalSpectrumUsage()
                    voxels += await self.getPredictedSpectrumUsage(timestamp)

                    # Send spectrum usage to all peers
                    if len(voxels) != 0:
                        logger.info('CIL: sending spectrum usage')
                        for _ip, p in self.collab_peers.items():
                            try:
                                asyncio.ensure_future(p.spectrumUsage(voxels, timestamp=timestamp),
                                                      loop=self.loop)
                            except: #pylint: disable=bare-except
                                logger.exception("Exception when sending spectrum usage")

                # Wait for either a spectrum update event or for the load check
                # period
                delta = config.spectrum_usage_update_period

                while not self.done:
                    if await wait_for_event(self.collab_spectrum_update_event, delta):
                        self.collab_spectrum_update_event.clear()

                    now = time.time()

                    if timestamp and now - timestamp < SPECTRUM_USAGE_MIN_RATE:
                        delta = timestamp + 2*SPECTRUM_USAGE_MIN_RATE - now
                    else:
                        break
            except asyncio.CancelledError:
                return
            except: #pylint: disable=bare-except
                logger.exception("Exception when sending spectrum usage")

    async def _detailedPerformanceTask(self):
        """Task to send detailed performance messages"""
        while not self.done:
            t1 = time.time()

            try:
                # Only send mandates after the scenario has started
                if self.scenario_started:
                    (_mp, timestamp, mandates_achieved, total_score_achieved, mandates) = \
                        await self.getMandatePerformance()

                    def mkMandatePerformance(mandate):
                        perf = cil.MandatePerformance()
                        perf.scalar_performance = mandate.scalar_performance
                        perf.radio_ids.extend(mandate.radio_ids)
                        perf.flow_id = mandate.flow_uid
                        perf.hold_period = int(mandate.hold_period.total_seconds())
                        perf.achieved_duration = int(mandate.achieved_duration.total_seconds())
                        perf.point_value = mandate.point_value

                        return perf

                    new_performance = [mkMandatePerformance(m) for m in mandates]

                    # Send mandates to all peers
                    logger.info('CIL: sending detailed performance')
                    for _ip, p in self.collab_peers.items():
                        try:
                            await p.detailedPerformance(new_performance,
                                                        mandates_achieved,
                                                        total_score_achieved,
                                                        self.scoring_point_threshold,
                                                        timestamp=timestamp)
                        except: #pylint: disable=bare-except
                            logger.exception("Exception when sending detailed performance")

                t2 = time.time()
                logger.info("Time to score: %f", t2 - t1)
                delta = self.config.detailed_performance_update_period - (t2 - t1)

                if delta > 0:
                    await asyncio.sleep(delta)
            except asyncio.CancelledError:
                return
            except: #pylint: disable=bare-except
                logger.exception("Exception when sending detailed performance")

    @handle('TellClient.inform')
    async def _handleInform(self, msg: registration.TellClient):
        """Handle neighbor inform message"""
        self.registration_nonce = msg.inform.client_nonce
        self.registration_max_keepalive = msg.inform.keepalive_seconds
        self.setNeighbors(msg.inform.neighbors)

    @handle('TellClient.notify')
    async def _handleNotify(self, msg: registration.TellClient):
        """Handle neighbor notification message"""
        self.setNeighbors(msg.notify.neighbors)

    @handle('CilMessage.hello')
    async def _handleHello(self, msg: cil.CilMessage):
        """Handle hello message"""

    @handle('CilMessage.spectrum_usage')
    async def _handleSpectrumUsage(self, msg: cil.CilMessage):
        """Handle spectrum usage message"""
        logger.info('Received spectrum usage: %s', msg)

    @handle('CilMessage.location_update')
    async def _handleLocationUpdate(self, msg: cil.CilMessage):
        """Handle location update message"""
        logger.info('Received location update: %s', msg)

    @handle('CilMessage.detailed_performance')
    async def _handleDetailedPerformance(self, msg: cil.CilMessage):
        """Handle detailed performance message"""
        logger.info('Received detailed performance: %s', msg)

    @handle('CilMessage.incumbent_notify')
    async def _handleIncumbentNotify(self, msg: cil.CilMessage):
        """Handle incumbent notification message"""
