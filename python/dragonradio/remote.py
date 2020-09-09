import functools
import struct
import time

from dragonradio.protobuf import *
from dragonradio.remote_pb2 import *
import dragonradio.remote_pb2 as remote

REMOTE_HOST = '127.0.0.1'
REMOTE_PORT = 8888

class RemoteClient(TCPProtoClient):
    def __init__(self, *args, server_host=REMOTE_HOST, server_port=REMOTE_PORT, **kwargs):
        super(RemoteClient, self).__init__(*args, server_host=server_host, server_port=server_port, **kwargs)

    @rpc(remote.Request, remote.Response)
    def start(self, req, timestamp=time.time()):
        req.timestamp = timestamp
        req.radio_command = remote.START

    @rpc(remote.Request, remote.Response)
    def stop(self, req, timestamp=time.time()):
        req.timestamp = timestamp
        req.radio_command = remote.STOP

    @rpc(remote.Request, remote.Response)
    def status(self, req, timestamp=time.time()):
        req.timestamp = timestamp
        req.radio_command = remote.STATUS

    @rpc(remote.Request, remote.Response)
    def updateMandatedOutcomes(self, req, goals, timestamp=time.time()):
        req.timestamp = timestamp
        req.update_mandated_outcomes.goals = goals

    @rpc(remote.Request, remote.Response)
    def updateEnvironment(self, req, env, timestamp=time.time()):
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
