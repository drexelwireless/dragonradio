# Copyright 2018-2023 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

"""Support for remote radio protocol"""
import time

from dragonradio.protobuf.tcp import TCPProtoClient, rpc
from dragonradio.remote_pb2 import * # pylint: disable=wildcard-import,unused-wildcard-import
import dragonradio.remote_pb2 as remote

DEFAULT_HOST: str = '127.0.0.1'
"""Default host for remote control protocol messages"""

DEFAULT_PORT: int = 8888
"""Default port for remote control protocol messages"""

class RemoteClient(TCPProtoClient):
    """Remove radio protocol client"""
    def __init__(self, *args, server_host=DEFAULT_HOST, server_port=DEFAULT_PORT, **kwargs):
        super().__init__(server_host=server_host,
                         server_port=server_port,
                         *args, **kwargs)

    @rpc(remote.Request, remote.Response)
    def start(self, req, timestamp=None):
        """Tell radio to start"""
        if timestamp is None:
            timestamp = time.time()

        req.timestamp = timestamp
        req.radio_command = remote.START

    @rpc(remote.Request, remote.Response)
    def stop(self, req, timestamp=None):
        """Tell radio to stop"""
        if timestamp is None:
            timestamp = time.time()

        req.timestamp = timestamp
        req.radio_command = remote.STOP

    @rpc(remote.Request, remote.Response)
    def status(self, req, timestamp=None):
        """Get radio status"""
        if timestamp is None:
            timestamp = time.time()

        req.timestamp = timestamp
        req.radio_command = remote.STATUS

    @rpc(remote.Request, remote.Response)
    def updateMandatedOutcomes(self, req, goals, timestamp=None):
        """Update mandated outcomes"""
        if timestamp is None:
            timestamp = time.time()

        req.timestamp = timestamp
        req.update_mandated_outcomes.goals = goals

    @rpc(remote.Request, remote.Response)
    def updateEnvironment(self, req, env, timestamp=None):
        """Update radio environment"""
        if timestamp is None:
            timestamp = time.time()

        req.timestamp = timestamp
        req.update_environment.environment = env
