# Copyright 2018-2023 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

"""Support for internal DragonRadio protocol"""
from asyncio import AbstractEventLoop
import logging
import time
from typing import Any, Optional, Sequence

import dragonradio.radio
from dragonradio.radio import Node
from dragonradio.proto.internal_pb2 import FlowStats, Message, SpectrumStats, SpectrumUsage
from dragonradio.protobuf.handler import send
from dragonradio.protobuf.timestamp import setTimestamp
from dragonradio.protobuf.udp import UDPProtoClient, UDPProtoServer

logger = logging.getLogger('internal')

INTERNAL_PORT: int = 4096
"""Port for internal control protocol messages"""

class InternalProtoServer(UDPProtoServer):
    """Server for internal protocol"""
    listen_ip: str
    """IP address on which to listen"""

    def __init__(self,
                 handler_obj: Any,
                 listen_ip: str,
                 loop: Optional[AbstractEventLoop]=None):
        super().__init__(handler_obj, loop=loop)

        self.listen_ip = listen_ip

    def start(self):
        """Start internal protocol server"""
        return self.start_server(Message, self.listen_ip, INTERNAL_PORT)

class InternalProtoClient(UDPProtoClient):
    """Client for internal protocol"""
    def __init__(self, server_host: str, **kwargs):
        UDPProtoClient.__init__(self, server_host=server_host, server_port=INTERNAL_PORT, **kwargs)

        self.server_host = server_host

        self.open()

    @send(Message)
    async def sendStatus(self,
                         msg: Message,
                         me: Node,
                         sources: Sequence[FlowStats],
                         sinks: Sequence[FlowStats],
                         spectrum: Sequence[SpectrumStats]):
        """Send a status message"""
        msg.status.radio_id = me.id
        setTimestamp(msg.status.timestamp, time.time())
        msg.status.loc.location.latitude = me.loc.lat
        msg.status.loc.location.longitude = me.loc.lon
        msg.status.loc.location.elevation = me.loc.alt
        setTimestamp(msg.status.loc.timestamp, me.loc.timestamp)
        msg.status.source_flows.extend(sources)
        msg.status.sink_flows.extend(sinks)
        msg.status.spectrum_stats.extend(spectrum)

        logger.debug("Sending status %s", msg)

    @send(Message)
    async def sendSchedule(self,
                           msg: Message, seq, nodes, sched, fc, bw, start_time):
        """Send a schedule message"""
        (nchannels, nslots) = sched.shape

        msg.schedule.frequency = fc
        msg.schedule.bandwidth = bw
        msg.schedule.scenario_start_time = start_time
        msg.schedule.seq = seq
        msg.schedule.nchannels = nchannels
        msg.schedule.nslots = nslots
        msg.schedule.nodes.extend(nodes)
        msg.schedule.schedule.extend(sched.reshape(nchannels*nslots))

def mkFlowStats(min_mp: int, max_mp: int, flowperf: dragonradio.radio.FlowStats) -> FlowStats:
    """Convert dragonradio.FlowStats to internal FlowStats"""
    first_mp: int = min_mp

    if flowperf.low_mp is not None:
        min(first_mp, flowperf.low_mp)

    mpstats = flowperf.stats[first_mp:max_mp+1]

    stats = FlowStats()
    stats.flow_uid = flowperf.flow_uid
    stats.src = flowperf.src
    stats.dest = flowperf.dest
    stats.first_mp = first_mp
    stats.npackets.extend([mp.npackets for mp in mpstats])
    stats.nbytes.extend([mp.nbytes for mp in mpstats])

    return stats

def mkSpectrumStats(start: float, end: float, voxels) -> SpectrumStats:
    """Convert dragonradio.FlowStats to internal SpectrumStats"""
    stats = SpectrumStats()

    setTimestamp(stats.start, start)
    setTimestamp(stats.end, end)

    internal_voxels = []

    for vox in voxels:
        usage = SpectrumUsage()
        usage.f_start = vox.f_start
        usage.f_end = vox.f_end
        usage.duty_cycle = vox.duty_cycle

        internal_voxels.append(usage)

    stats.voxels.extend(internal_voxels)

    return stats
