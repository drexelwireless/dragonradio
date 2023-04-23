# Copyright 2018-2022 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
from functools import cached_property
import importlib_resources
import json
import logging
from pathlib import Path
import os
import re
from typing import FrozenSet, Optional

import pandas as pd

from dragonradio.tools.cache import DataFrameCache

from .logging import ReservationLog

logger = logging.getLogger(__name__)

MP: int = 1
"""Measurement period"""

SCE_QUAL_SCENARIOS: FrozenSet[int] = frozenset([99771, 99801, 99840, 99880])
"""SCE qualification scenarios"""

SCE_CIL_QUAL_SCENARIOS: FrozenSet[int] = frozenset([9990, 9991])
"""SCE CIL qualification scenarios"""

class Scenario(DataFrameCache):
    """Colosseum scenario"""

    reservation: ReservationLog
    """Reservation to score"""

    scenarios_root: Path
    """Root directory for scenarios"""

    def __init__(self,
                 reservation: ReservationLog,
                 scenarios_root: Optional[Path]=None,
                 **kwargs):
        super().__init__(**kwargs)

        if reservation is None:
            raise ValueError("Reservation must not be None")

        self.reservation = reservation

        if scenarios_root is None:
            self.scenarios_root = importlib_resources.files('dragonradio.tools.colosseum.data.scenarios')
        else:
            self.scenarios_root = scenarios_root

    @property
    def cache_path(self):
        """Path to cache holding serialized DataFrames"""
        return self._cache_path / 'scenario' / str(self.rf_scenario)

    @cached_property
    def rf_scenario(self) -> int:
        """RF Scenario ID"""
        return self.reservation.rf_scenario

    @property
    def scenarios_path(self) -> Path:
        return self.scenarios_root / str(self.rf_scenario)

    #@cached_dataframe_property('mandates')
    @cached_property
    def mandates(self) -> pd.DataFrame:
        """DataFrame holding mandates"""
        items = []

        path = self.scenarios_path.joinpath('Mandated_Outcomes')

        for file in path.iterdir():
            m = re.match(r'^Node(\d+)MandatedOutcomes_{}.json$'.format(self.reservation.rf_scenario), file.name)
            if m:
                traffic_id = int(m.group(1))

                if traffic_id not in self.reservation.scenario_to_srn:
                  continue

                srn_id = self.reservation.scenario_to_srn[traffic_id]

                if srn_id not in self.reservation.srn_teams:
                  continue

                team = self.reservation.srn_teams[srn_id]

                with open(os.path.join(path, file), 'r') as f:
                    mandates = json.loads(f.read())

                    for stage in mandates:
                        t = stage['timestamp']

                        for goal in stage['scenario_goals']:
                            # Get flow id
                            flow_uid = goal['flow_uid']

                            # Get goal set
                            goal_set = goal['goal_set']

                            # Get goal type
                            goal_type = goal['goal_type']

                            # Get steady state period
                            hold_period = goal['hold_period']

                            # Get point value
                            point_value = goal.get('point_value', 1)

                            # Get requirements
                            requirements = goal['requirements']

                            # Get max latency
                            max_latency_s = requirements.get('max_latency_s', None)

                            # Get min throughput
                            min_throughput_bps = requirements.get('min_throughput_bps', None)

                            # Get file_transfer_deadline_s
                            file_transfer_deadline_s = requirements.get('file_transfer_deadline_s', None)

                            items.append([t,
                                          team,
                                          flow_uid,
                                          goal_set,
                                          goal_type,
                                          hold_period,
                                          point_value,
                                          max_latency_s,
                                          min_throughput_bps,
                                          file_transfer_deadline_s])

        df = pd.DataFrame(items,
                          columns=['stage',
                                   'team',
                                   'flow_uid',
                                   'goal_set',
                                   'goal_type',
                                   'hold_period',
                                   'point_value',
                                   'max_latency_s',
                                   'min_throughput_bps',
                                   'file_transfer_deadline_s'])

        df = df.astype({ 'stage': int
                       , 'team': int
                       , 'flow_uid': int
                       , 'goal_set': str
                       , 'goal_type': str
                       , 'hold_period': int
                       , 'point_value': int
                       , 'max_latency_s': float
                       , 'min_throughput_bps': float
                       , 'file_transfer_deadline_s': float
                       },
                       copy=False)

        df.set_index(['team', 'stage', 'flow_uid'], inplace=True)

        # Remove duplicates
        # See:
        #   https://stackoverflow.com/questions/13035764/remove-rows-with-duplicate-indices-pandas-dataframe-and-timeseries
        df = df[~df.index.duplicated(keep='first')]

        df.sort_index(inplace=True)

        return df

    #@cached_dataframe_property('environment')
    @cached_property
    def environment(self) -> pd.DataFrame:
        """Scenario environment"""
        items = []

        path = self.scenarios_path.joinpath('Environment')

        for file in path.iterdir():
            m = re.match(r'^Node(\d+)Environment_{}.json$'.format(self.reservation.rf_scenario), file.name)
            if m:
                traffic_id = int(m.group(1))

                if traffic_id not in self.reservation.scenario_to_srn:
                  continue

                srn_id = self.reservation.scenario_to_srn[traffic_id]

                if srn_id not in self.reservation.srn_teams:
                  continue

                team = self.reservation.srn_teams[srn_id]

                with open(os.path.join(path, file), 'r') as f:
                    environment = json.loads(f.read())

                    for stage in environment:
                        t = stage['timestamp']

                        # Get stage number
                        stage_number = stage['stage_number']

                        for env in stage['environment']:
                            # Get center frequency
                            scenario_center_frequency = env['scenario_center_frequency']

                            # Get bandwidth
                            scenario_rf_bandwidth = env['scenario_rf_bandwidth']

                            # Get scoring percent threshold
                            scoring_percent_threshold = env['scoring_percent_threshold']

                            # Get scoring point threshold
                            scoring_point_threshold = env['scoring_point_threshold']

                            items.append([t,
                                          team,
                                          stage_number,
                                          scenario_center_frequency,
                                          scenario_rf_bandwidth,
                                          scoring_percent_threshold,
                                          scoring_point_threshold])

        df = pd.DataFrame(items,
                          columns=['stage',
                                   'team',
                                   'stage_number',
                                   'scenario_center_frequency',
                                   'scenario_rf_bandwidth',
                                   'scoring_percent_threshold',
                                   'scoring_point_threshold'])

        df = df.astype({ 'stage': int
                       , 'team': int
                       , 'stage_number': int
                       , 'scoring_percent_threshold': float
                       , 'scoring_point_threshold': int
                       },
                       copy=False)

        df.scoring_percent_threshold /= 100.0

        df.set_index(['team', 'stage'], inplace=True)

        # Remove duplicates
        # See:
        #   https://stackoverflow.com/questions/13035764/remove-rows-with-duplicate-indices-pandas-dataframe-and-timeseries
        df = df[~df.index.duplicated(keep='first')]

        df.sort_index(inplace=True)

        return df
