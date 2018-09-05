import asyncio
import logging
import socket
import struct
import sys
import time
import zmq.asyncio

from dragon.protobuf import *

import sc2.cil_pb2 as cil
import sc2.registration_pb2 as registration

logger = logging.getLogger('collab')

CIL_VERSION = (2, 5, 2)

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

        self.msg_count += 1

        await f(self, msg, *args, **kwargs)

        logger.debug('Sending message {}'.format(str(msg)))
        self.send(msg)
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

@handler(registration.TellClient)
@handler(cil.CilMessage)
class CollabAgent(ZMQProtoServer, ZMQProtoClient):
    def __init__(self,
                 loop=None,
                 local_ip=None,
                 server_host=None,
                 server_port=None,
                 client_port=None,
                 peer_port=None):
        ZMQProtoServer.__init__(self, loop=loop)
        ZMQProtoClient.__init__(self, loop=loop, server_host=server_host, server_port=server_port)

        self.loop = loop
        self.local_ip = local_ip
        self.server_host = server_host
        self.server_port = server_port
        self.client_port = client_port
        self.peer_port = peer_port
        self.peers = {}

        self.nonce = None
        self.max_keepalive = 30

        self.startServer(cil.CilMessage, local_ip, peer_port)
        self.startServer(registration.TellClient, local_ip, client_port)
        self.open()

        loop.create_task(self.register())
        loop.create_task(self.heartbeat())

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
                    await self.keepalive()

                await asyncio.sleep(self.max_keepalive / 2)
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

    @handle('CilMessage.scalar_performance')
    async def handle_scalar_performance(self, msg):
        pass

    @handle('CilMessage.spectrum_usage')
    async def handle_spectrum_usage(self, msg):
        pass

    @handle('CilMessage.location_update')
    async def handle_location_update(self, msg):
        pass

    @handle('CilMessage.detailed_performance')
    async def handle_detailed_performance(self, msg):
        pass

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
