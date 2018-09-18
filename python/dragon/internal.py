from concurrent.futures import CancelledError
import functools
import struct

from dragon.protobuf import *
from dragon.internal_pb2 import *
import dragon.internal_pb2 as internal

INTERNAL_PORT = 8889

#
# Monkey patch Timestamp class to support setting timestamps using
# floating-point seconds.
#
def set_timestamp(self, ts):
    self.seconds = int(ts)
    self.picoseconds = int(ts % 1 * 1e12)

def get_timestamp(self):
    return self.seconds + self.picoseconds*1e-12

internal.TimeStamp.set_timestamp = set_timestamp
internal.TimeStamp.get_timestamp = get_timestamp

@handler(internal.Message)
class InternalAgent(UDPProtoServer, UDPProtoClient):
    def __init__(self,
                 controller,
                 loop=None,
                 local_ip=None,
                 server_host=None):
        UDPProtoServer.__init__(self, loop=loop)
        UDPProtoClient.__init__(self, loop=loop, server_host=server_host, server_port=INTERNAL_PORT)

        self.controller = controller

        self.loop = loop

        self.location_info_period = 30

        self.startServer(internal.Message, local_ip, INTERNAL_PORT)

        loop.create_task(self.location_update())

    def startClient(self, server_host):
        self.server_host = server_host
        self.open()

    async def location_update(self):
        try:
            while True:
                if self.server_host:
                    await self.location_info()

                await asyncio.sleep(self.location_info_period)
        except CancelledError:
            pass

    @handle('Message.location_info')
    def handle_location_info(self, msg):
        id = msg.location_info.radio_id
        if id in self.controller.nodes:
            n = self.controller.nodes[id]
            n.loc.lat = msg.location_info.location.latitude
            n.loc.lon = msg.location_info.location.longitude
            n.loc.alt = msg.location_info.location.elevation
            n.loc.timestamp = msg.location_info.timestamp.get_timestamp()

    @send(internal.Message)
    async def location_info(self, msg):
        me = self.controller.thisNode()

        msg.location_info.radio_id = me.id
        msg.location_info.location.latitude = me.loc.lat
        msg.location_info.location.longitude = me.loc.lon
        msg.location_info.location.elevation = me.loc.alt
        msg.location_info.timestamp.set_timestamp(me.loc.timestamp)
