# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
import datetime
from functools import cached_property
import json
import logging
import os
import pytz
import re

import pandas as pd

import dragonradio.tools.mgen as mgen
from dragonradio.tools.cache import DataFrameCache, cached_dataframe_property
from dragonradio.tools.colosseum.scoring import MP

logger = logging.getLogger(__name__)

UTC = pytz.timezone('UTC')

class MatchConfig:
    """""Data from a match_conf.json file"""
    def __init__(self, _res, path):
        with open(path, 'r') as f:
            config = json.load(f)

        self.json = config

        self.date_started = datetime.datetime.fromtimestamp(config['date_started'], UTC)

        self.team = config['team']

        self.batch_filename = config['batch_filename']
        """Batch filename"""

        self.match_id = None
        """Match ID"""

        m = re.match(r'Match_(\d+)_', self.batch_filename)
        if m:
            self.match_id = int(m.group(1))

        self.srn_teams = {}
        """Map from SRN to team"""

        self.scenario_to_srn = {}
        """Map from scenario node to SRN"""

        self.srn_to_scenario = {}
        """Map from SRN to scenario node"""

        for (traffic_id, srn_id) in config['node_to_srn_mapping'].items():
            traffic_id = int(traffic_id)

            self.scenario_to_srn[traffic_id] = srn_id
            self.srn_to_scenario[srn_id] = traffic_id

            # Only one team in batch mode. Incumbents have scenario IDs >50.
            if traffic_id <= 50:
                self.srn_teams[srn_id] = 1

class BatchInput:
    """""Data from a batch_input.json or freeplay.json file"""
    def __init__(self, res, path):
        with open(path, 'r') as f:
            config = json.load(f)

        self.json = config

        self.rf_scenario = config['RFScenario']
        """RF scenario ID"""

        self.traffic_scenario = config['TrafficScenario']
        """Traffic scenario ID"""

        self.duration = config['Duration']
        """Scenario duration"""

        self.srn_teams = {}
        """Map from SRN to team"""

        self.slot_teams = {}
        """Map from scenario slot to team"""

        self.images = {}
        """Map from SRN to image"""

        self.modem_config = {}
        """Map from SRN to modem configuration"""

        self.gateways = set()
        """Gateway SRNs"""

        self.scenario_to_srn = {}
        """Map from scenario node to SRN"""

        self.srn_to_scenario = {}
        """Map from SRN to scenario node"""

        teams = set()
        team_slot = 0

        for node in config['NodeData']:
            rf_id = node['RFNode_ID']
            traffic_id = node['TrafficNode']
            if traffic_id != rf_id:
                logger.warning("")

            if 'srn_number' in node:
                srn_id = node['srn_number']
            else:
                srn_id = res.match_config.scenario_to_srn[traffic_id]

            self.scenario_to_srn[traffic_id] = srn_id
            self.srn_to_scenario[srn_id] = traffic_id

            team = None

            if 'team_no' in node:
                team = node['team_no']

            if 'ImageName' in node:
                self.images[srn_id] = node['ImageName']

                m = re.match(r'^Team(\d+)$', node['ImageName'])
                if m:
                    team = int(m.group(1))

            if 'ModemConfig' in node:
                self.modem_config[srn_id] = node['ModemConfig']

            if team is not None:
                self.srn_teams[srn_id] = team

                if team not in teams:
                    teams.add(team)
                    self.slot_teams[team_slot] = team
                    team_slot += 1

            if node['isGateway']:
                self.gateways.add(srn_id)

class CollabInfo:
    def __init__(self, reservation):
        for f in os.listdir(reservation.path):
            m = re.match(r'^[-a-zA-Z0-9_]*-srn(\d*)-RES\d*-colbr(\d*)-\d*-\d*\.pcap$', f)
            if m:
                srn = int(m.group(1))
                collab = int(m.group(2))

                if collab != srn and srn in reservation.srn_teams and reservation.srn_teams[srn] == reservation.our_team:
                    self.collab_server_srn = collab
                    """Collaboration server SRN"""

                    self.collab_server_ip = "172.30.{collab}.{srn}".format(srn=100+srn, collab=100+collab)
                    """Collaboration server IP address"""

                    self.collab_log = os.path.join(reservation.path, f)
                    """Collaboration server log"""

                    return

        raise ValueError("Cannot find collaboration server")

class TrafficLogs:
    def __init__(self, _res, path):
        self.path = path
        """Path to directory containign traffic logs"""

        self.send_traffic_logs = {}
        """Map from scenario (src, dest) pairs to a send traffic log"""

        self.listen_traffic_logs = {}
        """Map from scenario (src, dest) pairs to a listen traffic log"""

        for f in os.listdir(path):
            m = re.match(r'^(send|listen)_SENDNODE-(\d+)_RECNODE-(\d+)_.*.drc$', f)
            if m:
                full_path = os.path.join(path, f)
                (ty, src, dest ) = m.groups()
                src = int(src)
                dest = int(dest)

                if ty == 'send':
                    self.send_traffic_logs[(src, dest)] = full_path
                else:
                    self.listen_traffic_logs[(src, dest)] = full_path

class ReservationLog(DataFrameCache):
    """A Colosseum reservation log"""
    def __init__(self, path,
                 rf_start_time=None,
                 srn_logs_path=None,
                 cache_path=None):
        """A Colosseum reservation log.

        Args:
            path: Path to log directory.
            rf_start_time: RF scenario start time or None.
            srn_logs_path: Path to SRN log directory or None.
            cache_path: Path to root directory for DataFrame cache.

        Returns:
            None
        """
        super().__init__(cache_path=cache_path)

        self.path = os.path.realpath(path)
        """Directory containing Colosseum logs"""

        self.srn_logs_path = self.path
        """Directory containing SRN logs"""

        if srn_logs_path is not None:
            self.srn_logs_path = os.path.realpath(srn_logs_path)

        self._rf_start_time = rf_start_time
        """RF scenario start time specified to init"""

        self.sanity_check = False
        """Perform extra sanity checks"""

    @property
    def cache_path(self):
        """Path to cache holding serialized DataFrames"""
        return os.path.join(self._cache_path, 'reservation', str(self.reservation_id))

    @property
    def inputs_path(self):
        return os.path.join(self.path, 'Inputs')

    @property
    def match_config_path(self):
        return os.path.join(self.inputs_path, 'match_conf.json')

    @property
    def batch_input_path(self):
        return os.path.join(self.inputs_path, 'batch_input.json')

    @property
    def freeplay_path(self):
        return os.path.join(self.inputs_path, 'freeplay.json')

    @property
    def traffic_logs_path(self):
        return os.path.join(self.path, 'traffic_logs')

    @cached_property
    def reservation_info(self):
        """Reservation type and ID"""
        m = re.match(r'^FREEPLAY-RESERVATION-(\d+)$', os.path.basename(self.path))
        if m:
            return ('Freeplay', int(m.group(1)))

        m = re.match(r'^RESERVATION-(\d+)$', os.path.basename(self.path))
        if m:
            return ('Batch', int(m.group(1)))

        m = re.match(r'^MATCH-\d+-RES-(\d+)$', os.path.basename(self.path))
        if m:
            return ('Match', int(m.group(1)))

        m = re.match(r'^scrimmage(\d+)-.*-(\d+)$', os.path.basename(self.path))
        if m:
            return ('Scrimmage {}'.format(m.groups(1)), int(m.group(2)))

        raise Exception('Cannot determine reservation ID: %s' % self.path)

    @cached_property
    def reservation_type(self):
        return self.reservation_info[0]

    @cached_property
    def reservation_id(self):
        """Reservation ID"""
        return self.reservation_info[1]

    @cached_property
    def match_config(self):
        """Match configuration"""
        return MatchConfig(self, self.match_config_path)

    @cached_property
    def match_start_time(self):
        """Match start time (seconds since epoch)"""
        return self.match_config.date_started

    @cached_property
    def match_id(self):
        """Match ID"""
        return self.match_config.match_id

    @cached_property
    def batch_input(self):
        """Batch input"""
        if os.path.exists(self.batch_input_path):
            return BatchInput(self, self.batch_input_path)
        else:
            return BatchInput(self, self.freeplay_path)

    @cached_property
    def rf_scenario(self):
        """RF Scenario ID"""
        return self.batch_input.rf_scenario

    @cached_property
    def traffic_scenario(self):
        """Traffic Scenario ID"""
        return self.batch_input.traffic_scenario

    @cached_property
    def rf_start_time(self):
        """RF scenario start time (seconds since epoch)"""
        # Prefer RF scenario start time specified to init
        if self._rf_start_time is not None:
            return self._rf_start_time

        path = os.path.join(self.inputs_path, 'rf_start_time.json')
        if os.path.exists(path):
            with open(path, 'r') as f:
                return datetime.datetime.fromtimestamp(json.load(f), UTC)

    @cached_property
    def images(self):
        """Map from SRN to image"""
        return self.batch_input.images

    @cached_property
    def gateways(self):
        """Gateways"""
        return self.batch_input.gateways

    @cached_property
    def scenario_to_srn(self):
        """Map from scenario node to SRN"""
        if os.path.exists(self.match_config_path):
            return self.match_config.scenario_to_srn
        else:
            return self.batch_input.scenario_to_srn

    @cached_property
    def srn_to_scenario(self):
        """Map from SRN to scenario node"""
        if os.path.exists(self.match_config_path):
            return self.match_config.srn_to_scenario
        else:
            return self.batch_input.srn_to_scenario

    @cached_property
    def teams(self):
        """Teams"""
        return frozenset(self.srn_teams.values())

    @cached_property
    def srn_teams(self):
        """Map from SRN to team"""
        if os.path.exists(self.match_config_path):
            return self.match_config.srn_teams
        else:
            return self.batch_input.srn_teams

    @cached_property
    def srn_logs(self):
        """Map from SRN to node log directory"""
        logs = {}

        if os.path.exists(self.srn_logs_path):
            for f in os.listdir(self.srn_logs_path):
                m = re.match(r'^.*-srn(\d+)-RES{}$'.format(self.reservation_id), f)
                if m:
                    srn = int(m.group(1))
                    if srn in self.srn_to_scenario:
                        logs[srn] = os.path.join(self.srn_logs_path, f)

        return logs

    @cached_property
    def srn_pcaps(self):
        """Map from SRN to PCAP file"""
        pcaps = {}

        if os.path.exists(self.srn_logs_path):
            for f in os.listdir(self.srn_logs_path):
                m = re.match(r'^.*-srn(\d+)-.*.pcap$'.format(self.reservation_id), f)
                if m:
                    srn = int(m.group(1))
                    if srn in self.srn_to_scenario:
                        pcaps[srn] = os.path.join(self.srn_logs_path, f)

        return pcaps

    @cached_property
    def collab_info(self):
        """Collaboration info"""
        return CollabInfo(self)

    @property
    def collab_server_srn(self):
        """Collaboration server SRN"""
        return self.collab_info.collab_server_srn

    @property
    def collab_server_ip(self):
        """Collaboration server IP address"""
        return self.collab_info.collab_server_ip

    @property
    def collab_log(self):
        """Collaboration server log"""
        return self.collab_info.collab_log

    @cached_property
    def our_team(self):
        """Our team number"""
        if os.path.exists(self.srn_logs_path):
            for dirname in os.listdir(self.srn_logs_path):
                m = re.match(r'^.*-srn(\d+)-RES{}$'.format(self.reservation_id), dirname)
                if m:
                    srn_id = int(m.group(1))

                    if os.path.exists(self.dragonLogPath(dirname, srn_id)):
                        if srn_id in self.srn_teams:
                            return self.srn_teams[srn_id]

        logger.debug('Cannot determine our team number for reservation %d', self.reservation_id)

    @cached_property
    def our_srns(self):
        """Our team's SRNs"""
        return [srn for (srn, team) in self.srn_teams.items() if team == self.our_team]

    @cached_property
    def our_gateway(self):
        """Our team's gateway"""
        for srn in self.gateways:
            if srn in self.srn_teams and self.srn_teams[srn] == self.our_team:
                return srn

        return None

    @cached_property
    def reported_scores(self):
        """Reported scores"""
        gateway = self.our_gateway

        if gateway is None:
            return None

        path = os.path.join(self.srn_logs[gateway],
                            'node-{:03d}'.format(gateway),
                            'score_reported.csv')

        if os.path.exists(path):
            return pd.read_csv(path)
        else:
            return None

    @cached_property
    def image_name(self):
        """DragonRadio image used"""
        # Look in images UNLESS this is a match, in which case the image names
        # are not useful
        if self.match_id is None:
            return self.images[self.our_srns[0]]

        # Otherwise, look at a dragonradio log
        if len(self.our_srns) != 0:
            srn = self.our_srns[0]
            for dirname in os.listdir(self.srn_logs_path):
                m = re.match(r'^.*-srn{}-RES{}$'.format(srn, self.reservation_id), dirname)
                if m:
                    dragonlog_path = self.dragonLogPath(dirname, srn)
                    if os.path.isfile(dragonlog_path):
                        with open(dragonlog_path, 'r') as f:
                            text = f.read()
                        m = re.search(r'radio:INFO:Radio version: (.*)$', text, re.M)
                        if m:
                            return os.path.basename(m.group(1))

        return None

    def dragonLogPath(self, dirname, srn_id):
        """Get path to dragonradio.log given log sub-directory and SRN"""
        return self.srnLogPath(dirname, srn_id, 'dragonradio.log')

    def stderrLogPath(self, dirname, srn_id):
        """Get path to stderr.log given log sub-directory and SRN"""
        return self.srnLogPath(dirname, srn_id, 'stderr.log')

    def srnLogPath(self, dirname, srn_id, logname):
        return os.path.join(self.srn_logs_path, dirname, f'node-{srn_id:03d}', logname)

    @cached_dataframe_property('heard')
    def heard(self):
        """Nodes heard by each SRN"""
        heard = {}

        for srn in self.our_srns:
            heard[srn] = set()

            for dirname in os.listdir(self.srn_logs_path):
                m = re.match(r'^.*-srn{}-RES{}$'.format(srn, self.reservation_id), dirname)
                if m:
                    dragonlog_path = self.dragonLogPath(dirname, srn)
                    if os.path.isfile(dragonlog_path):

                        with open(dragonlog_path, 'r') as f:
                            text = f.read()

                        for m in re.finditer(r'Adding node (\d+)$', text, re.M):
                            heard[srn].add(int(m.group(1)))

        all_nodes = sorted(set().union(*heard.values()))

        items = []

        for srn in sorted(self.our_srns):
            items.append([srn] + [n in heard[srn] for n in all_nodes])

        df = pd.DataFrame(items,
                          columns=["SRN"] + [str(n) for n in all_nodes])

        return df

    @cached_property
    def traffic_logs(self):
        """Traffic logs"""
        return TrafficLogs(self, self.traffic_logs_path)

    def _fixTrafficDataFrame(self, df):
        columns = ['frag', 'tos', 'src_port', 'dest_ip', 'dest_port']
        if 'src_ip' in df:
            columns.append('src_ip')

        df.drop(columns=columns, inplace=True)
        df.rename(columns={'flow': 'flow_uid', 'size': 'nbytes'}, inplace=True)
        df.set_index(['flow_uid', 'seq'], inplace=True)

    @cached_dataframe_property('traffic')
    def traffic(self):
        """Reservation traffic"""
        dataframes = []

        for (src, dest) in self.traffic_logs.send_traffic_logs:
            logger.info('Loading traffic from %d to %d', src, dest)

            # Load send MGEN log
            send_arr = mgen.parseSend(self.traffic_logs.send_traffic_logs[(src, dest)])
            send_df = pd.DataFrame(send_arr).astype({ 'timestamp': 'datetime64[ns, UTC]'},
                                                    copy=False)

            self._fixTrafficDataFrame(send_df)

            send_df.rename(columns={'timestamp': 'send_time'}, inplace=True)

            # Load corresponding listen MGEN log
            recv_arr = mgen.parseRecv(self.traffic_logs.listen_traffic_logs[(src, dest)])
            recv_df = pd.DataFrame(recv_arr).astype({ 'timestamp': 'datetime64[ns, UTC]'
                                                    , 'sent': 'datetime64[ns, UTC]'
                                                    },
                                                    copy=False)

            self._fixTrafficDataFrame(recv_df)

            recv_df.rename(columns={ 'timestamp': 'recv_time'
                                   , 'sent': 'send_time'},
                           inplace=True)

            logger.info('Joining traffic from %d to %d', src, dest)

            # Join tables on flow and sequence number
            if self.sanity_check:
                df = send_df.join(recv_df, how='outer', rsuffix='_outer').reset_index()

                # Check that send timestamps match if present
                idx = df.send_time_outer.notna()
                if not df.send_time[idx].equals(df.send_time_outer[idx]):
                    logger.warning("Send timestamps don't match for traffic from %d to %d",
                                src, dest)

                # Check that packet sizes match if present
                idx = df.nbytes_outer.notna()
                if not df.nbytes[idx].equals(df.nbytes_outer[idx]):
                    logger.warning("Packet sizes don't match for traffic from %d to %d",
                                src, dest)

                # Drop redundant outer columns
                df.drop(columns=['send_time_outer', 'nbytes_outer'], inplace=True)
            else:
                cols = pd.Index(['recv_time'])
                df = send_df.join(recv_df[cols], how='outer', rsuffix='_outer').reset_index()

            logger.info('Computing statistics for traffic from %d to %d', src, dest)

            # Add time column, offset from RF scenario start
            df['time'] = (df.send_time - self.rf_start_time).dt.total_seconds()

            # Compute measurement period
            df['mp'] = (df.time // MP).astype(int, copy=False)

            # Compute latency
            df['latency'] = (df.recv_time - df.send_time).dt.total_seconds()

            # Compute interarrival time
            df['interarrival'] = df.recv_time.diff().dt.total_seconds().astype(float)
            mask = df.flow_uid != df.flow_uid.shift(1)
            df.loc[mask, 'interarrival'] = 0

            # Add source, destination, and team information
            srn_src = self.scenario_to_srn[src]
            srn_dest = self.scenario_to_srn[dest]

            df['traffic_src'] = src
            df['traffic_dest'] = dest
            df['srn_src'] = srn_src
            df['srn_dest'] = srn_dest
            df['team'] = self.srn_teams[srn_src]

            dataframes.append(df)

        logger.info('Concatenating traffic data frames')
        return pd.concat(dataframes)
