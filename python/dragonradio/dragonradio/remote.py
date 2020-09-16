"""Support for remote radio protocol"""
import time

from dragonradio.protobuf import rpc
from dragonradio.protobuf import TCPProtoClient
from dragonradio.remote_pb2 import * # pylint: disable=wildcard-import,unused-wildcard-import
import dragonradio.remote_pb2 as remote

REMOTE_HOST = '127.0.0.1'
REMOTE_PORT = 8888

class RemoteClient(TCPProtoClient):
    """Remove radio protocol client"""
    def __init__(self, *args, server_host=REMOTE_HOST, server_port=REMOTE_PORT, **kwargs):
        super().__init__(server_host=server_host,
                         server_port=server_port,
                         *args, **kwargs)

    @rpc(remote.Request, remote.Response)
    def start(self, req, timestamp=time.time()):
        """Tell radio to start"""
        req.timestamp = timestamp
        req.radio_command = remote.START

    @rpc(remote.Request, remote.Response)
    def stop(self, req, timestamp=time.time()):
        """Tell radio to stop"""
        req.timestamp = timestamp
        req.radio_command = remote.STOP

    @rpc(remote.Request, remote.Response)
    def status(self, req, timestamp=time.time()):
        """Get radio status"""
        req.timestamp = timestamp
        req.radio_command = remote.STATUS

    @rpc(remote.Request, remote.Response)
    def updateMandatedOutcomes(self, req, goals, timestamp=time.time()):
        """Update mandated outcomes"""
        req.timestamp = timestamp
        req.update_mandated_outcomes.goals = goals

    @rpc(remote.Request, remote.Response)
    def updateEnvironment(self, req, env, timestamp=time.time()):
        """Update radio environment"""
        req.timestamp = timestamp
        req.update_environment.environment = env

state_map = { remote.OFF: 'OFF'
            , remote.BOOTING: 'BOOTING'
            , remote.READY: 'READY'
            , remote.ACTIVE: 'ACTIVE'
            , remote.STOPPING: 'STOPPING'
            , remote.FINISHED: 'FINISHED'
            , remote.ERROR: 'ERROR'
            }

def stateToString(state):
    """Convert remote radio state to a string"""
    return state_map[state]

def parseMandatedOutcomes(data):
    """
    Parse JSON data representing mandated outcomes

    Args:
        data (str): The JSON mandated goals

    Returns:
        remote.Goal: A protobuf Goal
    """
    return [parseGoal(g) for g in data]

def parseGoal(data):
    """Parse JSON data representing a mandated outcome goal."""
    goal = remote.Goal()

    goal.goal_type = data['goal_type']
    goal.flow_uid = data['flow_uid']
    parseRequirements(data['requirements'], goal.requirements)

    return goal

def parseRequirements(data, req):
    """Parse JSON data representing the requirements of a mandated outcome goal."""
    fields = ['max_latency_s', 'min_throughput_bps', 'max_packet_drop_rate'
             ,'file_transfer_deadline_s', 'file_size_bytes']

    for f in fields:
        if f in data:
            setattr(req, f, data[f])
