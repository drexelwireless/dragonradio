import asyncio
from concurrent.futures import CancelledError
import logging
import math
import socket
import struct
import sys
import time
import zmq.asyncio

from dragon.protobuf import *

import sc2.cil_pb2 as cil
import sc2.registration_pb2 as registration

logger = logging.getLogger('collab')

CIL_VERSION = (3, 6, 0)

MAX_LOCATION_AGE = 45

MAX_SPECTRUM_USAGE_UPDATE_PERIOD = 30.5
"""Maximum time allowed between spectrum updates"""

MIN_SPECTRUM_USAGE_UPDATE_PERIOD = 0.5
"""Minimum time allowed between spectrum updates"""

#
# Monkey patch Timestamp class to support setting timestamps using
# floating-point seconds.
#
def set_timestamp(self, ts):
    self.seconds = int(ts)
    self.picoseconds = int(ts % 1 * 1e12)

def get_timestamp(self):
    return self.seconds + self.picoseconds*1e-12

cil.TimeStamp.set_timestamp = set_timestamp
cil.TimeStamp.get_timestamp = get_timestamp

def ip_int_to_string(ip_int):
    """
    Convert integer formatted IP to IP string
    """
    return socket.inet_ntoa(struct.pack('!L',ip_int))

def ip_string_to_int(ip_string):
    """
    Convert string formatted IP to IP int
    """
    return struct.unpack('!L',socket.inet_aton(ip_string))[0]

# See:
#   https://stackoverflow.com/questions/49622924/wait-for-timeout-or-event-being-set-for-asyncio-event
async def event_wait(evt, timeout):
    """Wait for an event with a timeout"""
    try:
        await asyncio.wait_for(evt.wait(), timeout)
    except CancelledError:
        raise
    except asyncio.TimeoutError:
        pass

    return evt.is_set()

def mkReceiverInfo(radio_id, rx_gain):
    rx_info = cil.ReceiverInfo()
    rx_info.radio_id = radio_id
    rx_info.power_db.value = rx_gain
    return rx_info

def sendCIL(f):
    """
    Automatically add support to a function for constructing and sending a
    CIL protobuf message. Should be used to decorate the methods of a
    ZMQProtoClient subclass.

    Args:
        cls (class): The message class to send.
    """
    @wraps(f)
    async def wrapper(self, *args, **kwargs):
        timestamp = kwargs.pop('timestamp', time.time())

        msg = cil.CilMessage()
        msg.sender_network_id = ip_string_to_int(self.local_ip)
        msg.msg_count = self.msg_count
        msg.timestamp.set_timestamp(timestamp)
        msg.network_type.network_type = cil.NetworkType.COMPETITOR

        self.msg_count += 1

        await f(self, msg, *args, **kwargs)

        logger.debug('Sending message {}'.format(str(msg)))
        await self.send(msg)
    return wrapper

class RegistrationClient(ZMQProtoClient):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)

    @send(registration.TalkToServer)
    async def register(self, msg, local_ip):
        msg.register.my_ip_address = ip_string_to_int(local_ip)

    @send(registration.TalkToServer)
    async def keepalive(self, msg, nonce):
        msg.keepalive.my_nonce = nonce

    @send(registration.TalkToServer)
    async def leave(self, msg, nonce):
        msg.leave.my_nonce = nonce

class CILClient(ZMQProtoClient):
    """A CIL protocol client. Used to communicate with CIL peers."""
    def __init__(self, local_ip, peer_host, peer_port, **kwargs):
        super().__init__(server_host=peer_host,
                         server_port=peer_port,
                         **kwargs)
        self.local_ip = local_ip
        self.msg_count = 1
        self.open()
        self.loop.create_task(self.hello())

    @sendCIL
    async def hello(self, msg):
        msg.hello.version.major = CIL_VERSION[0]
        msg.hello.version.minor = CIL_VERSION[1]
        msg.hello.version.patch = CIL_VERSION[2]

    @sendCIL
    async def location_update(self, msg, locations):
        msg.location_update.locations.extend(locations)

    @sendCIL
    async def spectrum_usage(self, msg, voxels):
        msg.spectrum_usage.voxels.extend(voxels)

    @sendCIL
    async def detailed_performance(self,
                                   msg,
                                   performance,
                                   timestamp,
                                   mandates_achieved,
                                   total_score_achieved,
                                   scoring_point_threshold):
        msg.detailed_performance.mandate_count = len(performance)
        if timestamp is not None:
            msg.detailed_performance.timestamp.set_timestamp(timestamp)
        else:
            # Pretend this performance metric is from 5 seconds ago
            msg.detailed_performance.timestamp.set_timestamp(time.time() - 5)

        msg.detailed_performance.mandates.extend(performance)
        msg.detailed_performance.mandates_achieved = mandates_achieved
        msg.detailed_performance.total_score_achieved = total_score_achieved
        msg.detailed_performance.scoring_point_threshold = scoring_point_threshold

@handler(registration.TellClient)
@handler(cil.CilMessage)
class CILServer(object):
    """Base class for CIL server."""
    def __init__(self):
        self.registration_client = None
        """Registration client"""

        self.registration_nonce = None
        """Registration server nonce"""

        self.registration_max_keepalive = 30
        """Maximum time between registration heartbeats"""

        self.collab_server = None
        """CIL server"""

        self.collab_ip = None
        """Local IP address of collaboration server"""

        self.collab_peers = {}
        """CIL peers"""

        self.collab_spectrum_update_event = None
        """Event used to signal CIL spectrum update"""

        self.collab_tasks = []
        """asyncio tasks"""

    def startCollab(self):
        """Start CIL server"""
        self.registration_client = RegistrationClient(loop=self.loop,
                                                      server_host=self.config.collab_server_ip,
                                                      server_port=self.config.collab_server_port)
        self.registration_client.open()

        self.collab_server = ZMQProtoServer(self, loop=self.loop)
        self.collab_tasks.append(self.collab_server.start_server(cil.CilMessage,
                                                                 self.collab_ip,
                                                                 self.config.collab_peer_port))
        self.collab_tasks.append(self.collab_server.start_server(registration.TellClient,
                                                                 self.collab_ip,
                                                                 self.config.collab_client_port))

        self.collab_spectrum_update_event = asyncio.Event()

        self.loop.create_task(self._startCollab())

    async def _startCollab(self):
        await self.registration_client.register(self.collab_ip)

        self.collab_tasks.append(self.loop.create_task(self.heartbeat()))
        self.collab_tasks.append(self.loop.create_task(self.location_update()))
        self.collab_tasks.append(self.loop.create_task(self.spectrum_usage()))
        self.collab_tasks.append(self.loop.create_task(self.detailed_performance()))

    async def stopCollab(self):
        """Stop CIL server"""
        # We must trigger the event, otherwise we will be stuck waiting on it
        # forever
        self.collab_spectrum_update_event.set()

        # Cancel collaboration server tasks
        for task in self.collab_tasks:
            task.cancel()

        await asyncio.gather(*self.collab_tasks, return_exceptions=True)

        # Leave the collaboration network
        if self.registration_nonce:
            logger.info('Leaving collaboration network')
            try:
                await self.registration_client.leave(self.registration_nonce)
            except:
                logger.exception('Could not gracefully terminate collaboration agent')

        # Close collaboration clients
        logger.info('Closing CIL peer connections')
        for _, peer in self.collab_peers.items():
            peer.close()

    def addPeer(self, peer_ip):
        self.collab_peers[peer_ip] = CILClient(self.collab_ip,
                                               peer_ip,
                                               self.config.collab_peer_port,
                                               loop=self.loop)
        logger.info('Adding peer {}'.format(peer_ip))

    def removePeer(self, peer_ip):
        del self.collab_peers[peer_ip]
        logger.info('Removing peer {}'.format(peer_ip))

    def setNeighbors(self, neighbors):
        neighbors = [ip_int_to_string(n) for n in neighbors]

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

    async def getPredictedSpectrumUsage(self, when):
        """Get predicated voxel usage"""
        return []

    async def getMandatePerformance(self,):
        """Get mandate performance"""
        return []

    async def heartbeat(self):
        try:
            while not self.done:
                if self.registration_nonce:
                    try:
                        await self.registration_client.keepalive(self.registration_nonce)
                    except:
                        logger.exception("heartbeat")

                await asyncio.sleep(self.registration_max_keepalive / 2)
        except CancelledError:
            pass

    async def location_update(self):
        config = self.config

        while not self.done:
            try:
                # Calculate locations
                locations = []

                for id, n in self.nodes.items():
                    if n.loc.timestamp > time.time() - MAX_LOCATION_AGE:
                        info = cil.LocationInfo()
                        info.radio_id = n.id
                        info.location.latitude = n.loc.lat
                        info.location.longitude = n.loc.lon
                        info.location.elevation = n.loc.alt
                        info.timestamp.set_timestamp(n.loc.timestamp)

                        locations.append(info)

                # Send location update to all peers
                logging.info('CIL: sending location update')
                for ip, p in self.collab_peers.items():
                    try:
                        await p.location_update(locations)
                    except:
                        logger.exception("location_update")

                await asyncio.sleep(config.location_update_period)
            except CancelledError:
                return
            except:
                logger.exception("location_updates")

    def push_spectrum_usage(self):
        """Force a CIL spectrum usage update"""
        logging.debug('Forcing calculation of spectrum usage at time %f',
                      time.time() - self.scenario_start_time)
        self.collab_spectrum_update_event.set()

    async def spectrum_usage(self):
        config = self.config
        radio = self.radio

        # Average load for last sample period
        last_avg_load = 0

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
                        logging.info('CIL: sending spectrum usage')
                        for ip, p in self.collab_peers.items():
                            try:
                                asyncio.ensure_future(p.spectrum_usage(voxels, timestamp=timestamp), loop=self.loop)
                            except:
                                logger.exception("spectrum_usage")

                # Wait for either a spectrum update event or for the load check
                # period
                delta = config.spectrum_usage_update_period

                while not self.done:
                    if await event_wait(self.collab_spectrum_update_event, delta):
                        self.collab_spectrum_update_event.clear()

                    now = time.time()

                    if timestamp and now - timestamp < MIN_SPECTRUM_USAGE_UPDATE_PERIOD:
                        delta = timestamp + 2*MIN_SPECTRUM_USAGE_UPDATE_PERIOD - now
                    else:
                        break
            except CancelledError:
                return
            except:
                logger.exception("spectrum_usage")

    async def detailed_performance(self):
        while not self.done:
            t1 = time.time()

            try:
                # Only send mandates after the scenario has started
                if self.scenario_started:
                    (mp, timestamp, mandates_achieved, total_score_achieved, mandates) = await self.getMandatePerformance()

                    def mkMandatePerformance(mandate):
                        perf = cil.MandatePerformance()
                        perf.scalar_performance = mandate.scalar_performance
                        perf.radio_ids.extend(mandate.radio_ids)
                        perf.flow_id = mandate.flow_uid
                        perf.hold_period = int(mandate.hold_period)
                        perf.achieved_duration = mandate.achieved_duration
                        perf.point_value = mandate.point_value

                        return perf

                    new_performance = [mkMandatePerformance(m) for m in mandates]

                    # Send mandates to all peers
                    logging.info('CIL: sending detailed performance')
                    for ip, p in self.collab_peers.items():
                        try:
                            await p.detailed_performance(new_performance,
                                                         timestamp,
                                                         mandates_achieved,
                                                         total_score_achieved,
                                                         self.scoring_point_threshold)
                        except:
                            logger.exception("detailed_performance")

                t2 = time.time()
                logger.info("Time to score: %f", t2 - t1)
                delta = self.config.detailed_performance_update_period - (t2 - t1)

                if delta > 0:
                    await asyncio.sleep(delta)
            except CancelledError:
                return
            except:
                logger.exception("detailed_performance")

    @handle('TellClient.inform')
    async def handle_inform(self, msg):
        self.registration_nonce = msg.inform.client_nonce
        self.registration_max_keepalive = msg.inform.keepalive_seconds
        self.setNeighbors(msg.inform.neighbors)

    @handle('TellClient.notify')
    async def handle_notify(self, msg):
        self.setNeighbors(msg.notify.neighbors)

    @handle('CilMessage.hello')
    async def handle_hello(self, msg):
        pass

    @handle('CilMessage.spectrum_usage')
    async def handle_spectrum_usage(self, msg):
        logger.info('Received spectrum usage: %s', msg)

    @handle('CilMessage.location_update')
    async def handle_location_update(self, msg):
        logger.info('Received location update: %s', msg)

    @handle('CilMessage.detailed_performance')
    async def handle_detailed_performance(self, msg):
        logger.info('Received detailed performance: %s', msg)

    @handle('CilMessage.incumbent_notify')
    async def handle_incumbent_notify(self, msg):
        pass
