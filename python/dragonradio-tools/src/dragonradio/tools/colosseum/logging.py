# Copyright 2018-2022 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
from datetime import datetime, timezone
from enum import Enum
from functools import cached_property
import json
import logging
import os
from pathlib import Path
import re
from typing import Dict, FrozenSet, List, Mapping, MutableMapping, Optional, Set, Tuple

import pandas as pd

import dragonradio.tools.mgen as mgen
from dragonradio.tools.cache import DataFrameCache, cached_dataframe_property

logger = logging.getLogger(__name__)

MP: int = 1
"""Measurement period"""

class MatchConfig:
    """Data from a match_conf.json file"""

    reservation: 'ReservationLog'
    """Reservation"""

    path: Path
    """Path to match_conf.json"""

    date_started: datetime
    """Match start time"""

    team: str
    """Team name"""

    batch_filename: str
    """Batch filename"""

    match_id: Optional[int] = None
    """Match ID"""

    scenario_to_srn: MutableMapping[int, int]
    """Map from scenario node to SRN"""

    srn_to_scenario: MutableMapping[int, int]
    """Map from SRN to scenario node"""

    def __init__(self, reservation: 'ReservationLog', path: Path):
        self.reservation = reservation
        self.path = path

        with open(path, 'r') as f:
            config = json.load(f)

        self.date_started = datetime.fromtimestamp(config['date_started'], timezone.utc)

        self.team = config['team']

        self.batch_filename = config['batch_filename']

        self.match_id = None

        m = re.match(r'Match_(\d+)_', self.batch_filename)
        if m:
            self.match_id = int(m.group(1))

        self.scenario_to_srn = {}
        self.srn_to_scenario = {}

        for (traffic_id, srn_id) in config['node_to_srn_mapping'].items():
            traffic_id = int(traffic_id)

            self.scenario_to_srn[traffic_id] = srn_id
            self.srn_to_scenario[srn_id] = traffic_id

class BatchInput:
    """""Data from a batch_input.json or freeplay.json file"""

    reservation: 'ReservationLog'
    """Reservation"""

    path: Path
    """Path to batch_input.json (or match .json file)"""

    rf_scenario: int
    """RF scenario ID"""

    traffic_scenario: int
    """Traffic scenario ID"""

    duration: int
    """Scenario duration (sec)"""

    teams: List[int]
    """Teams in order of appearance"""

    srn_teams: MutableMapping[int, int]
    """Map from SRN to team"""

    images: MutableMapping[int, str]
    """Map from SRN to image"""

    modem_config: MutableMapping[int, str]
    """Map from SRN to modem configuration"""

    srns: Set[int]
    """SRNs"""

    gateways: Set[int]
    """Gateway SRNs"""

    scenario_to_srn: MutableMapping[int, int]
    """Map from scenario node to SRN"""

    srn_to_scenario: MutableMapping[int, int]
    """Map from SRN to scenario node"""

    def __init__(self, res, path):
        self.reservation = res
        self.path = path
        self.teams = []
        self.srn_teams = {}
        self.images = {}
        self.modem_config = {}
        self.srns = set()
        self.gateways = set()
        self.scenario_to_srn = {}
        self.srn_to_scenario = {}

        with open(path, 'r') as f:
            config = json.load(f)

        self.rf_scenario = config['RFScenario']
        self.traffic_scenario = config['TrafficScenario']
        self.duration = config['Duration']

        for node in config['NodeData']:
            rf_id = node['RFNode_ID']

            # A match batch file has a TrafficNode_ID attribute, but a normal
            # batch file has a TrafficNode attribute.
            if 'TrafficNode_ID' in node:
                traffic_id = node['TrafficNode_ID']
            else:
                traffic_id = node['TrafficNode']

            if traffic_id != rf_id:
                logger.warning("Traffic node ID does not match RF node ID")

            if 'srn_number' in node:
                srn_id = node['srn_number']
            else:
                srn_id = res.match_config.scenario_to_srn[traffic_id]

            self.srns.add(srn_id)

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

            if node['isGateway']:
                self.gateways.add(srn_id)

            if team is not None:
                self.srn_teams[srn_id] = team

                if team not in self.teams:
                    self.teams.append(team)
            # Only one team in batch mode, and incumbents have scenario IDs >50.
            elif traffic_id <= 50:
                self.srn_teams[srn_id] = 1

                if 1 not in self.teams:
                    self.teams.append(1)

class CollabInfo:
    """Collaboration information"""

    collab_server_srn: Optional[int] = None
    """Collaboration server SRN"""

    collab_server_ip: Optional[str] = None
    """Collaboration server IP address"""

    collab_log: Optional[Path] = None
    """Collaboration server log"""

    def __init__(self, reservation: 'ReservationLog'):
        if reservation.srn_teams is not None:
            for f in reservation.path.iterdir():
                m = re.match(r'^[-a-zA-Z0-9_]*-srn(\d*)-RES\d*-colbr(\d*)-\d*-\d*\.pcap$', f.name)
                if m:
                    srn = int(m.group(1))
                    collab_srn = int(m.group(2))

                    if collab_srn != srn \
                        and srn in reservation.srn_teams \
                        and reservation.srn_teams[srn] == reservation.our_team:
                        self.collab_server_srn = collab_srn
                        self.collab_server_ip = "172.30.{collab_srn}.{srn}".format(srn=100+srn, collab_srn=100+collab_srn)
                        self.collab_log = reservation.path / f

                        return

class TrafficLogs:
    path: Path
    """Path to directory containing traffic logs"""

    send_traffic_logs: MutableMapping[Tuple[int, int], str] = {}
    """Map from scenario (src, dest) pairs to a send traffic log"""

    listen_traffic_logs: MutableMapping[Tuple[int, int], str] = {}
    """Map from scenario (src, dest) pairs to a listen traffic log"""

    def __init__(self, reservation: 'ReservationLog', path: Path):
        self.path = path

        if path.exists():
            for f in path.iterdir():
                m = re.match(r'^(send|listen)_SENDNODE-(\d+)_RECNODE-(\d+)_.*.drc$', f.name)
                if m:
                    full_path = os.path.join(path, f)
                    (ty, src, dest ) = m.groups()
                    src = int(src)
                    dest = int(dest)

                    if ty == 'send':
                        self.send_traffic_logs[(src, dest)] = full_path
                    else:
                        self.listen_traffic_logs[(src, dest)] = full_path

class ReservationStatus(Enum):
    pending = 'Pending'
    active = 'Active'
    complete = 'Complete'
    error = 'Error'

class ReservationLog(DataFrameCache):
    """A Colosseum reservation log"""

    path: Path
    """Directory containing Colosseum logs"""

    srn_logs_path: Path
    """Directory containing SRN logs"""

    _rf_start_time: Optional[datetime]
    """RF scenario start time given to init"""

    sanity_check: bool = False
    """Perform extra sanity checks"""

    def __init__(self,
                 path: Path,
                 rf_start_time: Optional[datetime]=None,
                 srn_logs_path: Optional[Path]=None,
                 **kwargs):
        """A Colosseum reservation log.

        Args:
            path (Path): Path to log directory.
            rf_start_time (Optional[float]): RF scenario start time or None.
            srn_logs_path (Optional[Path]): Path to SRN log directory or None.
        """
        super().__init__(**kwargs)

        if path is None:
            self.path = path
        else:
            self.path = Path(path).resolve()

        if srn_logs_path is None:
            self.srn_logs_path = self.path
        else:
            self.srn_logs_path = Path(srn_logs_path).resolve()

        self._rf_start_time = rf_start_time

    @property
    def status(self) -> ReservationStatus:
        """Determine reservation status"""
        # Look for .pcap files that contain CIL packets. These seem to be the
        # last files to appear when a reservation is over, so if they're
        # present, the reservation should be complete.  Except... reservations
        # that error out seem not to get a rf_start_time.json file, so if there
        # is no RF start time, set the status to 'Error'.
        has_pcap = any(fname.name.endswith('.pcap') for fname in self.path.iterdir())

        if has_pcap:
            if self.rf_start_time is None:
                return ReservationStatus.error
            else:
                return ReservationStatus.complete
        else:
            return ReservationStatus.active

    @property
    def is_complete(self) -> bool:
        """Is reservation complete?"""
        return self.status == ReservationStatus.complete

    @property
    def cache_path(self) -> Path:
        """Path to cache holding serialized DataFrames"""
        return self._cache_path / 'reservation' / str(self.reservation_id)

    @property
    def inputs_path(self) -> Path:
        return self.path / 'Inputs'

    @property
    def match_config_path(self) -> Path:
        return self.inputs_path / 'match_conf.json'

    @property
    def batch_input_path(self) -> Path:
        return self.inputs_path / 'batch_input.json'

    @property
    def freeplay_path(self) -> Path:
        return self.inputs_path / 'freeplay.json'

    @property
    def traffic_logs_path(self) -> Path:
        return self.path / 'traffic_logs'

    @classmethod
    def get_reservation_info(cls, path: Path) -> Tuple[str, int]:
        """Reservation type and ID"""
        m = re.match(r'^FREEPLAY-RESERVATION-(\d+)$', path.name)
        if m:
            return ('Freeplay', int(m.group(1)))

        m = re.match(r'^RESERVATION-(\d+)$', path.name)
        if m:
            return ('Batch', int(m.group(1)))

        m = re.match(r'^MATCH-\d+-RES-(\d+)$', path.name)
        if m:
            return ('Match', int(m.group(1)))

        m = re.match(r'^scrimmage(\d+)-.*-(\d+)$', path.name)
        if m:
            return ('Scrimmage {}'.format(m.groups(1)), int(m.group(2)))

        raise ValueError('Cannot determine reservation ID: %s' % path)

    @cached_property
    def reservation_info(self) -> Tuple[str, int]:
        """Reservation type and ID"""
        return self.get_reservation_info(self.path)

    @cached_property
    def reservation_type(self) -> str:
        return self.reservation_info[0]

    @cached_property
    def reservation_id(self) -> int:
        """Reservation ID"""
        return self.reservation_info[1]

    @cached_property
    def match_config(self) -> MatchConfig:
        """Match configuration"""
        if os.path.exists(self.match_config_path):
            return MatchConfig(self, self.match_config_path)

        raise RuntimeError('Cannot find match configuration for %s' % self.path)

    @cached_property
    def match_start_time(self) -> datetime:
        """Match start time (seconds since epoch)"""
        return self.match_config.date_started

    @cached_property
    def match_id(self) -> Optional[int]:
        """Match ID"""
        return self.match_config.match_id

    @cached_property
    def batch_input(self) -> BatchInput:
        """Batch input"""
        if os.path.exists(self.batch_input_path):
            return BatchInput(self, self.batch_input_path)
        elif os.path.exists(self.freeplay_path):
            return BatchInput(self, self.freeplay_path)

        raise RuntimeError('Cannot find batch configuration for %s' % self.path)

    @cached_property
    def rf_scenario(self) -> int:
        """RF Scenario ID"""
        return self.batch_input.rf_scenario

    @cached_property
    def traffic_scenario(self) -> int:
        """Traffic Scenario ID"""
        return self.batch_input.traffic_scenario

    @cached_property
    def start_time(self) -> Optional[datetime]:
        """Reservation start time (seconds since epoch)"""
        return self.match_config.date_started

    @cached_property
    def rf_start_time(self) -> Optional[datetime]:
        """RF scenario start time (seconds since epoch)"""
        # Prefer RF scenario start time specified to init
        if self._rf_start_time is not None:
            return self._rf_start_time

        path = os.path.join(self.inputs_path, 'rf_start_time.json')
        if os.path.exists(path):
            with open(path, 'r') as f:
                return datetime.fromtimestamp(json.load(f), timezone.utc)

        return None

    @cached_property
    def images(self) -> Mapping[int, str]:
        """Map from SRN to image"""
        return self.batch_input.images

    @cached_property
    def srns(self) -> Set[int]:
        """SRNs"""
        return self.batch_input.srns

    @cached_property
    def gateways(self) -> Set[int]:
        """Gateways"""
        return self.batch_input.gateways

    @cached_property
    def scenario_to_srn(self) -> Mapping[int, int]:
        """Map from scenario node to SRN"""
        if os.path.exists(self.match_config_path):
            return self.match_config.scenario_to_srn
        else:
            return self.batch_input.scenario_to_srn

    @cached_property
    def srn_to_scenario(self) -> Mapping[int, int]:
        """Map from SRN to scenario node"""
        if os.path.exists(self.match_config_path):
            return self.match_config.srn_to_scenario
        else:
            return self.batch_input.srn_to_scenario

    @cached_property
    def teams(self) -> List[int]:
        """Teams in order of appearance in batch configuration"""
        if self.match_config is not None:
            batch_path = self.path / self.match_config.batch_filename

            if batch_path.exists():
                batch = BatchInput(self, batch_path)
                return batch.teams

        return self.batch_input.teams

    @cached_property
    def srn_teams(self) -> Mapping[int, int]:
        """Map from SRN to team"""

        if self.match_config is not None:
            batch_path = self.path / self.match_config.batch_filename

            if batch_path.exists():
                batch = BatchInput(self, batch_path)
                return batch.srn_teams

        if self.batch_input is not None:
            return self.batch_input.srn_teams

        return {}

    @cached_property
    def srn_logs(self) -> Mapping[int, Path]:
        """Map from SRN to node log directory"""
        logs = {}

        if os.path.exists(self.srn_logs_path):
            for f in os.listdir(self.srn_logs_path):
                m = re.match(r'^.*-srn(\d+)-RES{}$'.format(self.reservation_id), f)
                if m:
                    srn = int(m.group(1))
                    if srn in self.srn_to_scenario:
                        logs[srn] = self.srn_logs_path / f

        return logs

    @cached_property
    def srn_pcaps(self) -> Mapping[int, str]:
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
    def observer_logs(self) -> List[str]:
        if not os.path.exists(self.srn_logs_path):
            return []

        return [filename for filename in os.listdir(self.srn_logs_path) if 'observer' in filename]

    @cached_property
    def collab_info(self) -> CollabInfo:
        """Collaboration info"""
        return CollabInfo(self)

    @property
    def collab_server_srn(self) -> Optional[int]:
        return self.collab_info.collab_server_srn

    @property
    def collab_server_ip(self) -> Optional[str]:
        """Collaboration server IP address"""
        return self.collab_info.collab_server_ip

    @property
    def collab_log(self) -> Optional[Path]:
        """Collaboration server log"""
        return self.collab_info.collab_log

    @cached_property
    def our_team(self) -> Optional[int]:
        """Our team number"""
        if os.path.exists(self.srn_logs_path):
            for dirname in self.srn_logs_path.iterdir():
                m = re.match(r'^.*-srn(\d+)-RES{}$'.format(self.reservation_id), dirname.name)
                if m:
                    srn_id = int(m.group(1))

                    if os.path.exists(self.dragonLogPath(dirname, srn_id)):
                        if srn_id in self.srn_teams:
                            return self.srn_teams[srn_id]

        logger.debug('Cannot determine our team number for reservation %d', self.reservation_id)
        return None

    @cached_property
    def our_srns(self) -> Set[int]:
        """Our team's SRNs"""
        return set(srn for (srn, team) in self.srn_teams.items() if team == self.our_team)

    @cached_property
    def our_gateway(self) -> Optional[int]:
        """Our team's gateway"""
        for srn in self.gateways:
            if srn in self.srn_teams and self.srn_teams[srn] == self.our_team:
                return srn

        return None

    @cached_property
    def reported_scores(self) -> Optional[pd.DataFrame]:
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
        # Otherwise, look at a dragonradio log
        if len(self.our_srns) != 0:
            # Get one of our SRNs
            some_srn = next(iter(self.our_srns))

            # Look in images UNLESS this is a match, in which case the image names
            # are not useful
            if self.match_id is None:
                return self.images[some_srn]

            for dirname in os.listdir(self.srn_logs_path):
                m = re.match(r'^.*-srn{}-RES{}$'.format(some_srn, self.reservation_id), dirname)
                if m:
                    dragonlog_path = self.dragonLogPath(dirname, some_srn)
                    if os.path.isfile(dragonlog_path):
                        with open(dragonlog_path, 'r') as f:
                            text = f.read()
                        m = re.search(r'radio:INFO:Radio version: (.*)$', text, re.M)
                        if m:
                            return os.path.basename(m.group(1))

        return None

    def dragonLogPath(self, dirname: Path, srn_id: int) -> Path:
        """Get path to dragonradio.log given log sub-directory and SRN"""
        return self.srnLogPath(dirname, srn_id, 'dragonradio.log')

    def stderrLogPath(self, dirname: Path, srn_id: int) -> Path:
        """Get path to stderr.log given log sub-directory and SRN"""
        return self.srnLogPath(dirname, srn_id, 'stderr.log')

    def srnLogPath(self, dirname: Path, srn_id: int, logname: str) -> Path:
        return self.srn_logs_path / dirname / f'node-{srn_id:03d}' / logname

    @cached_property
    def crashed_nodes(self) -> Set[int]:
        """Determine which nodes have crashed, if any"""
        nodes = set()

        for srn in self.our_srns:
            for path in self.srn_logs_path.iterdir():
                m = re.match(r'^.*-srn{}-RES{}$'.format(srn, self.reservation_id), path.name)
                if m:
                    core_path = path / 'cores'
                    if core_path.is_dir and len(list(core_path.iterdir())) != 0:
                        nodes.add(srn)

        return nodes

    @cached_property
    def traceback_nodes(self) -> Set[int]:
        """Determine which nodes have Python tracebacks, if any"""
        nodes = set()

        for srn in self.our_srns:
            for path in self.srn_logs_path.iterdir():
                m = re.match(r'^.*-srn{}-RES{}$'.format(srn, self.reservation_id), path.name)
                if m:
                    dragonlog_path = self.dragonLogPath(path, srn)

                    if dragonlog_path.is_file():
                        with dragonlog_path.open('r') as f:
                            text = f.read()

                        m = re.search(r'Traceback', text, re.M)
                        if m:
                            nodes.add(srn)

        return nodes

    @cached_property
    def uhd_error_nodes(self) -> Set[int]:
        """Determine which nodes have Python UHD errors, if any"""
        nodes = set()

        for srn in self.our_srns:
            for path in self.srn_logs_path.iterdir():
                m = re.match(r'^.*-srn{}-RES{}$'.format(srn, self.reservation_id), path.name)
                if m:
                    dragonlog_path = self.stderrLogPath(path, srn)

                    if dragonlog_path.is_file():
                        with dragonlog_path.open('r', errors='ignore') as f:
                            text = f.read()

                        m = re.search(r'UHD Error', text, re.M)
                        if m:
                            nodes.add(srn)

        return nodes

    @cached_property
    def has_crash(self) -> bool:
        """Flag indicating whether or not a node crashed"""
        return len(self.crashed_nodes) != 0

    @cached_property
    def has_traceback(self) -> bool:
        """Flag indicating whether or not a node has a python Traceback"""
        return len(self.traceback_nodes) != 0

    @cached_property
    def has_uhd_error(self) -> bool:
        """Flag indicating whether or not a node has a UHD error"""
        return len(self.uhd_error_nodes) != 0

    @cached_property
    def heard(self) -> pd.DataFrame:
        """SRN connectivity"""
        heard: Dict[int, Set[int]] = {}

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

        return heard

    @cached_dataframe_property('heard_df')
    def heard_df(self) -> Optional[pd.DataFrame]:
        """DataFrame representation of connectivity matrix"""
        all_nodes = sorted(set().union(*self.heard.values()))
        items = []

        for srn in sorted(self.our_srns):
            items.append([srn] + [n in self.heard[srn] for n in all_nodes])

        return pd.DataFrame(items, columns=["SRN"] + [str(n) for n in all_nodes])

    @cached_property
    def traffic_logs(self) -> TrafficLogs:
        """Traffic logs"""
        return TrafficLogs(self, self.traffic_logs_path)

    def _fixTrafficDataFrame(self, df: pd.DataFrame) -> None:
        columns = ['frag', 'tos', 'src_port', 'dest_ip', 'dest_port']
        if 'src_ip' in df:
            columns.append('src_ip')

        df.drop(columns=columns, inplace=True)
        df.rename(columns={'flow': 'flow_uid', 'size': 'nbytes'}, inplace=True)
        df.set_index(['flow_uid', 'seq'], inplace=True)

    @cached_dataframe_property('traffic')
    def traffic(self) -> Optional[pd.DataFrame]:
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
            if (src, dest) not in self.traffic_logs.listen_traffic_logs:
                continue

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

            # Join tables on flow and sequence number. We use a left join since
            # it is possible for packets to be reported received without being
            # reported sent...
            if self.sanity_check:
                df = send_df.join(recv_df, how='left', rsuffix='_outer').reset_index()

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
                df = send_df.join(recv_df[cols], how='left', rsuffix='_outer').reset_index()

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

        if len(dataframes) == 0:
            return None

        logger.info('Concatenating traffic data frames')
        df = pd.concat(dataframes)

        # Somehow, sometimes the flow_uid and seq columns end up having type
        # 'float'. Fix that.
        df = df.astype({'flow_uid': int, 'seq': int}, copy=False)

        return df
