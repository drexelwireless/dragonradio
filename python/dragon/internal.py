from concurrent.futures import CancelledError
import functools
import numpy as np
import struct
import time

from dragon.protobuf import *
from dragon.internal_pb2 import *
import dragon.internal_pb2 as internal

INTERNAL_PORT = 4096

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
class InternalProtoServer(UDPProtoServer):
    def __init__(self,
                 controller,
                 loop=None,
                 local_ip=None):
        UDPProtoServer.__init__(self, loop=loop)

        self.loop = loop
        self.controller = controller

        self.startServer(internal.Message, local_ip, INTERNAL_PORT)

    @handle('Message.status')
    def handle_status(self, msg):
        node_id = msg.status.radio_id

        # Update node location
        if node_id in self.controller.nodes:
            n = self.controller.nodes[node_id]
            loc = msg.status.loc

            n.loc.lat = loc.location.latitude
            n.loc.lon = loc.location.longitude
            n.loc.alt = loc.location.elevation
            n.loc.timestamp = loc.timestamp.get_timestamp()

        # Update set of active flows
        for flow in msg.status.source_flows:
            self.controller.addLink(node_id, flow.dest, flow.flow_uid)

        # Update flow statistics
        for flow in msg.status.sink_flows:
            self.controller.addLink(flow.src, node_id, flow.flow_uid)
            self.controller.updateMandateStats(flow.flow_uid,
                                               flow.latency,
                                               flow.throughput,
                                               flow.bytes)

    @handle('Message.schedule')
    def handle_schedule(self, msg):
        config = self.controller.config
        radio = self.controller.radio

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

            self.loop.create_task(self.controller.installMACSchedule(msg.schedule.seq, sched))

class InternalProtoClient(UDPProtoClient):
    def __init__(self,
                 controller,
                 loop=None,
                 server_host=None):
        UDPProtoClient.__init__(self, loop=loop, server_host=server_host, server_port=INTERNAL_PORT)

        self.loop = loop
        self.controller = controller
        self.server_host = server_host

        self.open()

    @send(internal.Message)
    async def sendStatus(self, msg):
        me = self.controller.thisNode()

        radio = self.controller.radio

        msg.status.radio_id = me.id
        msg.status.timestamp.set_timestamp(time.time())
        msg.status.loc.location.latitude = me.loc.lat
        msg.status.loc.location.longitude = me.loc.lon
        msg.status.loc.location.elevation = me.loc.alt
        msg.status.loc.timestamp.set_timestamp(me.loc.timestamp)
        msg.status.source_flows.extend(toFlowInfo(radio.flowsource.flows))
        msg.status.sink_flows.extend(toFlowInfo(radio.flowsink.flows))

    @send(internal.Message)
    async def sendSchedule(self, msg, seq, nodes, sched):
        config = self.controller.config

        (nchannels, nslots) = sched.shape

        msg.schedule.frequency = config.frequency
        msg.schedule.bandwidth = config.bandwidth
        msg.schedule.seq = seq
        msg.schedule.nchannels = nchannels
        msg.schedule.nslots = nslots
        msg.schedule.nodes.extend(nodes)
        msg.schedule.schedule.extend(sched.reshape(nchannels*nslots))

def toFlowInfo(flows):
    """Convert dragonradio.FlowInfo to internal agent FlowInfo"""
    internal_flows = []

    for flow_uid, flow_info in flows.items():
        info = internal.FlowInfo()
        info.flow_uid = flow_uid
        info.src = flow_info.src
        info.dest = flow_info.dest
        info.window = flow_info.latency.time_window
        info.latency = flow_info.latency.value
        info.throughput = flow_info.throughput.value
        info.bytes = flow_info.bytes

        internal_flows.append(info)

    return internal_flows
