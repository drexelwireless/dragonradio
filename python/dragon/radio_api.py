import functools
import struct

from dragon.protobuf import *
import dragon.dragonradio_pb2 as internal

RADIO_API_HOST = '127.0.0.1'
RADIO_API_PORT = 8888

class RadioAPIClient(TCPProtoClient):
    def __init__(self, *args, server_host=RADIO_API_HOST, server_port=RADIO_API_PORT, **kwargs):
        super(RadioAPIClient, self).__init__(*args, server_host=server_host, server_port=server_port, **kwargs)

    @rpc(internal.Request, internal.Response)
    def start(self, req):
        req.radio_command = internal.START

    @rpc(internal.Request, internal.Response)
    def stop(self, req):
        req.radio_command = internal.STOP

    @rpc(internal.Request, internal.Response)
    def status(self, req):
        req.radio_command = internal.STATUS

    @rpc(internal.Request, internal.Response)
    def updateMandatedOutcomes(self, req, goals):
        req.update_mandated_outcomes.goals.extend(goals)

    @rpc(internal.Request, internal.Response)
    def updateMandatedOutcomesJson(self, req, goals):
        req.update_mandated_outcomes_json.goals = goals

    @rpc(internal.Request, internal.Response)
    def updateEnvironmentJson(self, req, env):
        req.update_environment_json.environment = env

state_map = { internal.OFF: 'OFF'
            , internal.BOOTING: 'BOOTING'
            , internal.READY: 'READY'
            , internal.ACTIVE: 'ACTIVE'
            , internal.STOPPING: 'STOPPING'
            , internal.FINISHED: 'FINISHED'
            , internal.ERROR: 'ERROR'
            }

def stateToString(state):
    return state_map[state]

def parseMandatedOutcomes(data):
    """
    Parse JSON data representing mandated outcomes

    Args:
        data (str): The JSON mandated goals

    Returns:
        internal.Goal: A protobuf Goal
    """
    return [parseGoal(g) for g in data]

def parseGoal(data):
    """Parse JSON data representing a mandated outcome goal."""
    goal = internal.Goal()

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
