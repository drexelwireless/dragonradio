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

CIL_VERSION = (3, 1, 0)

MAX_LOCATION_AGE = 45

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

class MandatedOutcome(object):
    def __init__(self, json=None):
        self.start = time.time()
        self.scalar_performance = 0
        self.latency_ = None
        self.throughput_ = None
        self.bytes_ = None

        if json:
            fields = [ 'goal_type'
                     , 'flow_uid'
                     , 'goal_set'
                     , 'hold_period']

            for f in fields:
                setattr(self, f, json.get(f, None))

            fields = [ 'max_latency_s'
                     , 'min_throughput_bps'
                     , 'max_packet_drop_rate'
                     , 'file_transfer_deadline_s'
                     , 'file_size_bytes']

            for f in fields:
                setattr(self, f, json['requirements'].get(f, None))

    def __repr__(self):
        return 'MandatedOutcome({})'.format(self.__dict__)

    @property
    def is_discrete(self):
        return hasattr(self, 'file_transfer_deadline_s')

    @property
    def latency(self):
        return self.latency_

    @latency.setter
    def latency(self, x):
        self.latency_ = x
        self.updateMetrics()

    @property
    def throughput(self):
        return self.throughput_

    @throughput.setter
    def throughput(self, x):
        self.throughput_ = x
        self.updateMetrics()

    @property
    def bytes(self):
        return self.bytes_

    @bytes.setter
    def bytes(self, x):
        self.bytes_ = x
        self.updateMetrics()

    def updateMetrics(self):
        metrics = []

        if isinstance(self.max_latency_s, float) and isinstance(self.latency, float):
            metric = self.max_latency_s/self.latency
            if math.isfinite(metric):
                metrics.append(metric)

        if isinstance(self.min_throughput_bps, float) and isinstance(self.throughput, float):
            metric = self.throughput/self.min_throughput_bps
            if math.isfinite(metric):
                metrics.append(metric)

        if len(metrics) != 0:
            self.scalar_performance = min(metrics)
        else:
            self.scalar_performance = 0

class GPSLocation(object):
    def __init__(self):
        self.lat = 0
        self.lon = 0
        self.alt = 0
        self.timestamp = 0

    def __str__(self):
        return 'GPSLocation(lat={},lon={},alt={},timestamp={})'.format(self.lat, self.lon, self.alt, self.timestamp)

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

    def __str__(self):
        return 'Voxel(f_start={}, f_end={}, tx={}, rx={})'.format(self.f_start, self.f_end, self.tx, self.rx)

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
        msg = cil.CilMessage()
        msg.sender_network_id = ip_string_to_int(self.local_ip)
        msg.msg_count = self.msg_count
        msg.timestamp.set_timestamp(time.time())
        msg.network_type.network_type = cil.NetworkType.COMPETITOR

        self.msg_count += 1

        await f(self, msg, *args, **kwargs)

        logger.debug('Sending message {}'.format(str(msg)))
        await self.send(msg)
    return wrapper

class Peer(ZMQProtoClient):
    def __init__(self, collab_agent, peer_host, peer_port):
        ZMQProtoClient.__init__(self,
                                loop=collab_agent.loop,
                                server_host=peer_host,
                                server_port=peer_port)
        self.collab_agent = collab_agent
        self.local_ip = collab_agent.local_ip
        self.msg_count = 1
        self.open()
        self.loop.create_task(self.hello())

    @sendCIL
    async def hello(self, msg):
        msg.hello.version.major = CIL_VERSION[0]
        msg.hello.version.minor = CIL_VERSION[1]
        msg.hello.version.patch = CIL_VERSION[2]

    @sendCIL
    async def location_update(self, msg, nodes):
        for id, n in nodes.items():
            if n.loc.timestamp > time.time() - MAX_LOCATION_AGE:
                info = cil.LocationInfo()
                info.radio_id = n.id
                info.location.latitude = n.loc.lat
                info.location.longitude = n.loc.lon
                info.location.elevation = n.loc.alt
                info.timestamp.set_timestamp(n.loc.timestamp)

                msg.location_update.locations.extend([info])

    @sendCIL
    async def spectrum_usage(self, msg, controller):
        voxels = []

        for vox in controller.voxels:
            usage = cil.SpectrumVoxelUsage()

            usage.spectrum_voxel.freq_start = vox.f_start
            usage.spectrum_voxel.freq_end = vox.f_end
            usage.spectrum_voxel.time_start.set_timestamp(time.time())
            usage.transmitter_info.radio_id = vox.tx
            usage.transmitter_info.power_db.value = controller.radio.usrp.tx_gain
            usage.transmitter_info.mac_cca = False

            receivers = []
            for rx in vox.rx:
                rx_info = cil.ReceiverInfo()
                rx_info.radio_id = rx
                rx_info.power_db.value = controller.radio.usrp.rx_gain

                receivers.append(rx_info)

            usage.receiver_info.extend(receivers)
            usage.measured_data = False

            voxels.append(usage)

        msg.spectrum_usage.voxels.extend(voxels)

    @sendCIL
    async def detailed_performance(self, msg, controller):
        mandates = controller.mandated_outcomes.values()

        msg.detailed_performance.mandate_count = len(mandates)
        # Pretend this performance metric is from 5 seconds ago
        msg.detailed_performance.timestamp.set_timestamp(time.time() - 5)

        for mandate in mandates:
            perf = cil.MandatePerformance()
            perf.scalar_performance = mandate.scalar_performance
            perf.radio_ids.extend([])
            perf.flow_id = mandate.flow_uid
            perf.hold_period = mandate.hold_period
            perf.achieved_duration = 0

            msg.detailed_performance.mandates.extend([perf])

        msg.detailed_performance.mandates_achieved = 0

@handler(registration.TellClient)
@handler(cil.CilMessage)
class CollabAgent(ZMQProtoServer, ZMQProtoClient):
    def __init__(self,
                 controller,
                 loop=None,
                 local_ip=None,
                 server_host=None,
                 server_port=None,
                 client_port=None,
                 peer_port=None):
        ZMQProtoServer.__init__(self, loop=loop)
        ZMQProtoClient.__init__(self, loop=loop, server_host=server_host, server_port=server_port)

        self.controller = controller

        self.loop = loop
        self.local_ip = local_ip
        self.server_host = server_host
        self.server_port = server_port
        self.client_port = client_port
        self.peer_port = peer_port
        self.peers = {}

        self.nonce = None
        self.max_keepalive = 30

        self.location_update_period = 20
        self.spectrum_usage_update_period = 5
        self.detailed_performance_update_period = 5

        self.startServer(cil.CilMessage, local_ip, peer_port)
        self.startServer(registration.TellClient, local_ip, client_port)
        self.open()

        loop.create_task(self.register())
        loop.create_task(self.heartbeat())
        loop.create_task(self.location_update())
        loop.create_task(self.spectrum_usage())
        loop.create_task(self.detailed_performance())

    def addPeer(self, peer_ip):
        self.peers[peer_ip] = Peer(self, peer_ip, self.peer_port)
        logger.info('Adding peer {}'.format(peer_ip))

    def removePeer(self, peer_ip):
        del self.peers[peer_ip]
        logger.info('Removing peer {}'.format(peer_ip))

    def setNeighbors(self, neighbors):
        neighbors = [ip_int_to_string(n) for n in neighbors]

        old_neighbors = set(self.peers.keys())
        new_neighbors = set(neighbors)
        new_neighbors.remove(self.local_ip)

        for p in old_neighbors - new_neighbors:
            self.removePeer(p)

        for p in new_neighbors - old_neighbors:
            self.addPeer(p)

    async def shutdown(self):
        if self.nonce:
            await self.leave()

    async def heartbeat(self):
        try:
            while True:
                if self.nonce:
                    try:
                        await self.keepalive()
                    except:
                        logger.exception("heartbeat")

                await asyncio.sleep(self.max_keepalive / 2)
        except CancelledError:
            pass

    async def location_update(self):
        try:
            while True:
                for ip, p in self.peers.items():
                    try:
                        await p.location_update(self.controller.nodes)
                    except:
                        logger.exception("location_update")

                await asyncio.sleep(self.location_update_period)
        except CancelledError:
            pass

    async def spectrum_usage(self):
        try:
            while True:
                for ip, p in self.peers.items():
                    try:
                        await p.spectrum_usage(self.controller)
                    except:
                        logger.exception("spectrum_usage")

                await asyncio.sleep(self.spectrum_usage_update_period)
        except CancelledError:
            pass

    async def detailed_performance(self):
        try:
            while True:
                for ip, p in self.peers.items():
                    try:
                        await p.detailed_performance(self.controller)
                    except:
                        logger.exception("detailed_performance")

                await asyncio.sleep(self.spectrum_usage_update_period)
        except CancelledError:
            pass

    @handle('TellClient.inform')
    async def handle_inform(self, msg):
        self.nonce = msg.inform.client_nonce
        self.max_keepalive = msg.inform.keepalive_seconds
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

    @send(registration.TalkToServer)
    async def register(self, msg):
        msg.register.my_ip_address = ip_string_to_int(self.local_ip)

    @send(registration.TalkToServer)
    async def keepalive(self, msg):
        msg.keepalive.my_nonce = self.nonce

    @send(registration.TalkToServer)
    async def leave(self, msg):
        msg.leave.my_nonce = self.nonce
