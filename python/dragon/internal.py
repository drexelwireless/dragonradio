from concurrent.futures import CancelledError
import functools
import numpy as np
import struct
import time

from dragon.protobuf import *
from dragon.internal_pb2 import *
import dragon.internal_pb2 as internal

logger = logging.getLogger('internal')

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
                 handler,
                 loop=None,
                 listen_ip=None):
        super().__init__(handler, loop=loop)

        self.start_server(internal.Message, listen_ip, INTERNAL_PORT)

class InternalProtoClient(UDPProtoClient):
    def __init__(self,
                 controller,
                 server_host=None,
                 **kwargs):
        UDPProtoClient.__init__(self, server_host=server_host, server_port=INTERNAL_PORT, **kwargs)

        self.controller = controller
        self.server_host = server_host

        self.open()

    @send(internal.Message)
    async def sendStatus(self, msg, me, sources, sinks, spectrum):
        msg.status.radio_id = me.id
        msg.status.timestamp.set_timestamp(time.time())
        msg.status.loc.location.latitude = me.loc.lat
        msg.status.loc.location.longitude = me.loc.lon
        msg.status.loc.location.elevation = me.loc.alt
        msg.status.loc.timestamp.set_timestamp(me.loc.timestamp)
        msg.status.source_flows.extend(sources)
        msg.status.sink_flows.extend(sinks)
        msg.status.spectrum_stats.extend(spectrum)

        logging.debug("Sending status %s", msg)

    @send(internal.Message)
    async def sendSchedule(self, msg, seq, nodes, sched, fc, bw, start_time):
        (nchannels, nslots) = sched.shape

        msg.schedule.frequency = fc
        msg.schedule.bandwidth = bw
        msg.schedule.scenario_start_time = start_time
        msg.schedule.seq = seq
        msg.schedule.nchannels = nchannels
        msg.schedule.nslots = nslots
        msg.schedule.nodes.extend(nodes)
        msg.schedule.schedule.extend(sched.reshape(nchannels*nslots))

def mkFlowStats(min_mp, max_mp, flowperf):
    """Convert dragonradio.FlowStats to internal FlowStats"""
    first_mp = min(min_mp, flowperf.low_mp)
    mpstats = flowperf.stats[first_mp:max_mp+1]

    stats = internal.FlowStats()
    stats.flow_uid = flowperf.flow_uid
    stats.src = flowperf.src
    stats.dest = flowperf.dest
    stats.first_mp = first_mp
    stats.npackets.extend([mp.npackets for mp in mpstats])
    stats.nbytes.extend([mp.nbytes for mp in mpstats])

    return stats

def mkSpectrumStats(start, end, voxels):
    """Convert dragonradio.FlowStats to internal SpectrumStats"""
    stats = internal.SpectrumStats()
    stats.start.set_timestamp(start)
    stats.end.set_timestamp(end)

    internal_voxels = []

    for vox in voxels:
        usage = internal.SpectrumUsage()
        usage.f_start = vox.f_start
        usage.f_end = vox.f_end
        usage.duty_cycle = vox.duty_cycle

        internal_voxels.append(usage)

    stats.voxels.extend(internal_voxels)

    return stats
