# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
import datetime
from functools import cached_property
import importlib.resources
import json
import logging
import os
import pytz
import re

import numpy as np
import pandas as pd

from dragonradio.tools.cache import DataFrameCache, cached_dataframe_property

logger = logging.getLogger(__name__)

MP = 1
"""Measurement period"""

scenarios_path = None
"""Path to scenarios"""

class Scenario(DataFrameCache):
    """Colosseum scenario"""
    def __init__(self, reservation, cache_path=None):
        super().__init__(cache_path=cache_path)

        self.reservation = reservation
        """Reservation to score"""

        self.rf_scenario = reservation.rf_scenario
        """RF Scenario ID"""

    @property
    def cache_path(self):
        """Path to cache holding serialized DataFrames"""
        return os.path.join(self._cache_path,
                            'scenario',
                            str(self.rf_scenario))

    @property
    def scenario_path(self):
        if scenarios_path is None or not os.path.isdir(scenarios_path):
            raise ValueError("Path to scenarios not specified")

        return os.path.join(scenarios_path, str(self.rf_scenario))

    #@cached_dataframe_property('mandates')
    @cached_property
    def mandates(self):
        """DataFrame holding mandates"""
        items = []

        path = os.path.join(self.scenario_path, 'Mandated_Outcomes')

        for file in os.listdir(path):
            m = re.match(r'^Node(\d+)MandatedOutcomes_{}.json$'.format(self.reservation.rf_scenario), file)
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
    def environment(self):
        """Scenario environment"""
        items = []

        path = os.path.join(self.scenario_path, 'Environment')

        for file in os.listdir(path):
            m = re.match(r'^Node(\d+)Environment_{}.json$'.format(self.reservation.rf_scenario), file)
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

class Scorer(DataFrameCache):
    """Colosseum scoring"""
    def __init__(self, reservation, cache_path=None):
        super().__init__(cache_path=cache_path)

        self.reservation = reservation
        """Reservation to score"""

        self.sanity_check = False
        """Perform extra sanity checks"""

        self.scenario = Scenario(reservation)
        """Scenario"""

    @property
    def cache_path(self):
        """Path to cache holding serialized DataFrames"""
        return os.path.join(self._cache_path,
                            'scoring',
                            str(self.reservation_id),
                            str(self.rf_start_time.timestamp()))

    @cached_property
    def reservation_id(self):
        """Reservation ID"""
        return self.reservation.reservation_id

    @cached_property
    def rf_scenario(self):
        """RF scenario ID"""
        return self.reservation.rf_scenario

    @cached_property
    def rf_start_time(self):
        """RF scenario start time (seconds since epoch)"""
        return self.reservation.rf_start_time

    @cached_property
    def max_team_scores(self):
        """Maximum possible score per team per stage."""
        return self.scenario.mandates.reset_index().\
                groupby(['team', 'stage']).\
                agg({ 'flow_uid': 'count' }).\
                rename(columns={ 'flow_uid': 'max_score' })

    @cached_property
    def max_stage_scores(self):
        """Maximum possible score per stage."""
        df = self.max_team_scores.reset_index().groupby(['stage']).agg({ 'max_score': 'min' })

        return [df.loc[stage].max_score for stage in self.stages]

    @cached_property
    def stage_scores(self):
        """Stage scores"""
        df = self.ensemble_mp_score

        stages = self.stages
        stage_scores = []

        for i in range (0, len(stages)):
            if i == len(stages) - 1:
                stage_scores.append(df.loc[stages[i]//MP:].mp_score.max())
            else:
                stage_scores.append(df.loc[stages[i]//MP:stages[i+1]//MP].mp_score.max())

        return stage_scores

    @cached_property
    def stage_score_intervals(self):
        """The earliest scoring interval in which the max score was obtained"""
        df = self.ensemble_mp_score

        stages = self.stages
        stage_score_intervals = []

        for i in range (0, len(stages)):
            if i == len(stages) - 1:
                stage_score_intervals.append(df.loc[stages[i]//MP:].mp_score.idxmax() - stages[i])
            else:
                stage_score_intervals.append(df.loc[stages[i]//MP:stages[i+1]//MP].mp_score.idxmax() - stages[i])

        return stage_score_intervals

    @cached_property
    def max_match_score(self):
        """Maximum possible match score."""
        return sum(self.max_stage_scores)

    @cached_property
    def final_match_score(self):
        """Final match score"""
        return sum(self.stage_scores)

    @cached_property
    def match_scores(self):
        """Match scores"""
        df = self.score

        match_scores = {}

        for team in self.reservation.srn_teams.values():
            try:
                match_scores[team] = float(df.loc[team].score.max())
            except:
                match_scores[team] = 0

        return match_scores

    @cached_property
    def start(self):
        """Start of scoring period (seconds since epoch)"""
        if self.rf_start_time is not None:
            return self.rf_start_time
        else:
            df = self.reservation.traffic

            # Delete flow 0
            df.drop(df.index[df.flow_uid == 0], inplace=True)

            return df.send_time.min() - pd.Timedelta(seconds=self.stages[0])

    @cached_property
    def end(self):
      """End of scoring period (seconds since epoch)"""
      df = self.reservation.traffic

      return df.send_time.max()

    @cached_property
    def last_mp(self):
        """Last measurement period"""
        return int((self.end - self.start).total_seconds() / MP) - 1

    @cached_property
    def stages(self):
        """Stages"""
        return sorted(list(self.scenario.mandates.index.get_level_values('stage').unique()))

    @cached_property
    def stage_end(self):
        """Map from stage start time to last MP in stage"""
        ends = {}

        for i in range(0, len(self.stages)-1):
            ends[self.stages[i]] = int(self.stages[i+1] // MP) - 1

        ends[self.stages[-1]] = self.last_mp
        logger.info("Stage ends: %s", ends)

        return ends

    @cached_dataframe_property('traffic')
    #@property
    def traffic(self):
        """Scored traffic with duplicate received packets dropped"""
        df = self.reservation.traffic

        # Delete flow 0
        df.drop(df.index[df.flow_uid == 0], inplace=True)

        # Drop duplicate received packets. When we reset the index, the order of
        # rows stays the same, so we can safely drop duplicates, keeping the
        # first
        # Compute stages
        logger.info('Dropping duplicated packets')
        df.set_index(['team', 'flow_uid', 'seq', 'recv_time'], inplace=True)
        df.sort_index(inplace=True)
        df.reset_index(inplace=True)
        df.set_index(['team', 'flow_uid', 'seq'], inplace=True)

        df = df[~df.index.duplicated(keep='first')]

        # Compute stages
        logger.info('Computing stages')

        df['stage'] = np.NaN
        for stage in self.stages:
            df.loc[df.mp>=stage, 'stage'] = stage
        df.stage = df.stage.astype(int)

        # Set index on team and flow_uid
        logger.info('Setting index on team and flow_uid')

        df.reset_index(inplace=True)
        df.set_index(['team', 'stage', 'flow_uid'], inplace=True)
        df.sort_index(inplace=True)

        #
        # Attach mandates to flows
        #
        logger.info('Joining traffic and mandate data')

        cols = pd.Index(['point_value',
                         'hold_period',
                         'max_latency_s',
                         'min_throughput_bps',
                         'file_transfer_deadline_s'])

        mandates = self.scenario.mandates[cols]

        df = df.join(mandates, how='left', rsuffix='_outer')

        #
        # Apply latency mandates to determine which packets were on time
        #
        logger.info('Calculating on-time packets')

        # Select on-time throughput mandates
        tp_good = df.recv_time.notna() & \
                  df.max_latency_s.notna() & \
                  (df.latency < df.max_latency_s)

        # Select on-time file transfers mandates
        ft_good = df.recv_time.notna() & \
                  df.file_transfer_deadline_s.notna() & \
                  (df.latency < df.file_transfer_deadline_s)

        # Determine which packets were on time
        df['ontime'] = (tp_good | ft_good).astype(int)

        df.to_csv('traffic.csv')
        return df

    @cached_dataframe_property('traffic_summary')
    def traffic_summary(self):
        df = self.traffic

        # Add delivered column for delivered bytes
        logger.info('Calculating delivered bytes')

        df['delivered'] = df['nbytes']

        df.loc[df.ontime == 0, 'delivered'] = 0

        # Summarize number of bytes and number of packets delivered
        logger.info('Calculating aggregate offered and delivered bytes and packets')

        grp = df.groupby(['team', 'stage', 'flow_uid', 'mp'])
        df = grp.agg({ 'nbytes': 'sum'
                     , 'delivered': 'sum'
                     , 'ontime': ['count', 'sum']
                     })

        # Rename columns
        df.columns = ['_'.join(col).rstrip('_') for col in df.columns.values]

        df.rename(columns={ 'nbytes_sum': 'nbytes_sent'
                          , 'delivered_sum': 'nbytes_recv'
                          , 'ontime_count': 'npackets_sent'
                          , 'ontime_sum': 'npackets_recv'
                          }, inplace=True)

        df.to_csv('traffic_summary.csv')
        return df

    def readIncumbentGates(self):
        try:
            from ciltool import CilReader, cil_pb2
        except:
            return []

        def set_timestamp(self, ts):
            self.seconds = int(ts)
            self.picoseconds = int(ts % 1 * 1e12)

        def get_timestamp(self):
            return self.seconds + self.picoseconds*1e-12

        cil_pb2.TimeStamp.set_timestamp = set_timestamp
        cil_pb2.TimeStamp.get_timestamp = get_timestamp

        # RF scenario start time
        start_time = self.reservation.rf_start_time.timestamp()

        # Gates
        items = []

        # Measurement periods we've seen (we see all CIL messages to all teams,
        # so we see multiple identical reports)
        seen = set()

        for srn, image in self.reservation.images.items():
            if 'incumbent' in image:
                with CilReader(self.reservation.node_pcaps[srn], read_reg=False) as reader:
                    while True:
                        message = reader.read()
                        if message is None:
                            break

                        if 'cil_message' in message:
                            msg = message['cil_message']
                            if msg.WhichOneof('payload') == 'incumbent_notify':
                                if msg.incumbent_notify.WhichOneof('payload') == 'data':
                                    info = msg.incumbent_notify.data
                                elif msg.incumbent_notify.WhichOneof('payload') == 'data_active':
                                    info = msg.incumbent_notify.data_active

                                if info.msg_type == cil_pb2.IncumbentActiveInfo.VIOLATION:
                                    mp = int((info.report_time.get_timestamp() - start_time) // MP)
                                    if mp not in seen:
                                        seen.add(mp)
                                        items.append((mp, info.threshold_exceeded))

        return items

    @property
    def gates(self):
        """Gate violations.

        The gate value in an MP is True if gated, i.e., there is a violation,
        False otherwise"""
        logger.info('Calculating gates')

        df = pd.DataFrame(self.readIncumbentGates(),
                          columns=['mp', 'gate']).astype({ 'mp': int , 'gate': np.bool })
        df.set_index(['mp'], inplace=True)
        df.sort_index(inplace=True)

        return df

    @cached_dataframe_property('per_mp_score')
    def per_mp_score(self):
        """Flow score per measurement period.

        This DataFrame has an index on team, flow_uid, stage, and mp.
        """
        # Get traffic summary and gates
        df = self.traffic_summary
        gates_df = self.gates

        #
        # Sort by team, flow_uid, stage, and mp
        #
        df.reset_index(inplace=True)
        df.set_index(['team', 'flow_uid', 'stage', 'mp'], inplace=True)
        df.sort_index(inplace=True)

        #
        # Fill in missing measurement periods
        #
        logger.info('Filling in missing measurement periods')

        def fillMissingMPs(df):
            stage = df.name
            stage_start = int(stage // MP)
            stage_end = self.stage_end[stage]

            return df.unstack(['team', 'stage', 'flow_uid']).reindex(range(stage_start, stage_end+1)).fillna(0).stack(['team', 'stage', 'flow_uid'])

        df = df.groupby(['stage'], as_index=False).apply(fillMissingMPs).droplevel(0)

        # Re-index gates by measurement period and fill forward
        mp_index = df.index.get_level_values('mp').unique()
        gates_df = gates_df.reindex(mp_index, method='ffill')

        # Attach gates
        cols = pd.Index(['gate'])
        df = df.join(gates_df[cols], how='left')

        #
        # Attach mandates to flows
        #
        logger.info('Joining traffic and mandate data')

        # Get rid of index on mp
        df.reset_index(inplace=True)
        df.set_index(['team', 'stage', 'flow_uid'], inplace=True)
        df.sort_index(inplace=True)

        cols = pd.Index(['point_value',
                         'hold_period',
                         'max_latency_s',
                         'min_throughput_bps',
                         'file_transfer_deadline_s'])

        df = df.join(self.scenario.mandates[cols], how='left', rsuffix='_outer')

        # Set index. We need to sort in this order so that when we group by team
        # and flow_uid, all entries will be sorted by order of increasing mp.
        # This is necessary for the calculation of achieved_duration to be
        # correct across stage boundaries.
        df.reset_index(inplace=True)
        df.set_index(['team', 'flow_uid', 'stage', 'mp'], inplace=True)
        df.sort_index(inplace=True)

        #
        # Calculate per-MP score based on mandates
        #
        logger.info('Calculating individual mandates met per MP')

        df['throughput_bps'] = df.nbytes_recv*8

        # Select good throughput mandates
        tp_good = df.max_latency_s.notna() & \
                  (df.nbytes_sent > 0) & \
                  ((df.throughput_bps >= df.min_throughput_bps) | \
                   (df.nbytes_recv == df.nbytes_sent))

        # Select good file transfers mandates
        ft_good = df.file_transfer_deadline_s.notna() & \
                  (df.npackets_sent > 0) & \
                  ((df.npackets_recv/df.npackets_sent) >= 0.9)

        # Score goals
        df['goal'] = tp_good | ft_good

        # Now we need to handle MPs with no traffic. We set their score to NaN
        # and then fill forward
        df.loc[df.nbytes_sent == 0, 'goal'] = np.NaN

        df['goal'] = df.groupby(['team', 'flow_uid'], sort=False)['goal'].apply(lambda x: x.ffill())

        # Set remaining NaN goals to 0
        df['goal'].fillna(0, inplace=True)

        # If we are gated, goal is 0
        df.loc[df.gate == True, 'goal'] = 0

        #
        # Calculate achieved duration
        #
        df['achieved_duration'] = df.loc[df['goal'] == 1].groupby(['team', 'flow_uid', (df['goal'] != df['goal'].shift(1)).cumsum()]).cumcount()+1
        df['achieved_duration'].fillna(0, inplace=True)

        #
        # Calculate stable goals
        #
        df['goal_stable'] = df['achieved_duration'] >= df['hold_period']

        #
        # Calculate MP scores
        #
        df['mp_score'] = df.goal_stable * df.point_value

        #
        # Calculate maximum measurement period score.
        #
        # The 'active' column indicates when a flow is active. Some flows---in
        # particular file transfers---don't start transmitting at the beginning
        # of a stage.
        #
        df['active'] = (df.nbytes_sent > 0).groupby(['team', 'flow_uid']).cumsum() > 0
        df['max_achieved_duration'] =  df['active'].groupby(['team', 'flow_uid']).cumsum()
        df['max_mp_score'] = df.point_value*(df['max_achieved_duration'] >= df['hold_period'])

        #
        # Set index
        #
        df.reset_index(inplace=True)
        df.set_index(['team', 'flow_uid', 'stage', 'mp'], inplace=True)
        df.sort_index(inplace=True)

        return df

    @cached_dataframe_property('mp_score')
    def mp_score(self):
        """Measurement period scores.

        This DataFrame has an index on team, stage, and mp.
        """
        df = self.per_mp_score

        #
        # Calculate aggregate per-MP score
        #
        logger.info('Calculating aggregate measurement period scores')

        df = df.groupby(['team', 'stage', 'mp']).\
            agg({ 'mp_score': 'sum'
                , 'point_value': 'sum'
                , 'max_mp_score': 'sum'
                })

        df['relative_mp_score'] = df.mp_score / df.point_value

        df.mp_score = df.mp_score.astype(int)
        df.relative_mp_score = df.relative_mp_score.astype(float)

        return df

    @cached_dataframe_property('ensemble_mp_score')
    def ensemble_mp_score(self):
        """Ensemble score"""
        df = self.mp_score

        df = df.groupby(['mp']).agg({ 'mp_score': 'min'
                                    , 'max_mp_score': 'min'
                                    })
        df.sort_index(inplace=True)

        return df

    @cached_dataframe_property('threshold_success')
    def threshold_success(self):
        """Threshold success"""
        df = self.mp_score
        thresholds_df = self.scenario.environment

        # Attach thresholds to MP scores
        logger.info('Joining thresholds and MP scores')

        cols = pd.Index(['scoring_percent_threshold',
                         'scoring_point_threshold'])
        df = df.join(thresholds_df[cols], how='left', rsuffix='_outer')

        # Determine if thresholds met
        threshold_met = (df.mp_score >= df.scoring_point_threshold) | \
                        (df.relative_mp_score >= df.scoring_percent_threshold)
        df['threshold_success'] = threshold_met.astype(int)

        return df

    @cached_dataframe_property('ensemble_threshold_success')
    def ensemble_threshold_success(self):
        """Ensemble threshold success"""
        df = self.threshold_success

        df = df.groupby(['mp']).agg({'threshold_success': 'sum'})
        nteams = len(set(self.reservation.srn_teams.values()))
        df.threshold_success = (df.threshold_success == nteams).astype(int)
        df.sort_index(inplace=True)

        return df

    @cached_dataframe_property('pe_score')
    def pe_score(self):
        """Points earned"""
        df = self.threshold_success
        ensemble_success_df = self.ensemble_threshold_success
        mp_ensemble_score_df = self.ensemble_mp_score

        # Join with ensemble success
        cols = pd.Index(['threshold_success'])
        df = df.join(ensemble_success_df[cols], how='left', rsuffix='_ensemble')

        # Join with ensemble score
        cols = pd.Index(['mp_score'])
        df = df.join(mp_ensemble_score_df[cols], how='left', rsuffix='_ensemble')

        df['pe_score'] = (df.threshold_success_ensemble == 1).astype(int)*df.mp_score + \
                         (df.threshold_success_ensemble == 0).astype(int)*df.mp_score_ensemble

        return df

    @cached_property
    def score(self):
        """Phase 3 scores"""
        df = self.pe_score
        df['score'] = df.groupby('team').pe_score.cumsum()
        df['max_score'] = df.groupby('team').max_mp_score.cumsum()

        return df

    @cached_property
    def ensemble_score(self):
        """Phase 3 ensemble score"""
        df = self.ensemble_mp_score
        df['score'] = df.mp_score.cumsum()
        df['max_score'] = df.max_mp_score.cumsum()

        return df

    @cached_property
    def flows(self):
        """Per-flow score information, sorted in descending order by lost points"""
        score_df = self.per_mp_score
        traffic_df = self.traffic

        flows = score_df.index.get_level_values('flow_uid').unique()

        items = []

        for flow_uid in flows:
            # Get points for flow
            df_flow = score_df.xs(flow_uid, level='flow_uid')
            points = df_flow.mp_score.sum()
            possible_points = df_flow.max_mp_score.sum()

            # Get flow description
            mandate = self.scenario.mandates.xs(flow_uid, level='flow_uid').iloc[0]
            file_transfer = not pd.isna(mandate.file_transfer_deadline_s)
            if file_transfer:
                desc = "{:s} (bulk)".format(mandate.goal_type)
            else:
                desc = mandate.goal_type

            # Get source and destination info
            df = traffic_df.xs(flow_uid, level='flow_uid')
            srn_src = df.srn_src.iloc[0]
            srn_dest = df.srn_dest.iloc[0]
            traffic_src = df.traffic_src.iloc[0]
            traffic_dest = df.traffic_dest.iloc[0]

            items.append((flow_uid,
                          points,
                          possible_points,
                          desc,
                          srn_src,
                          srn_dest,
                          traffic_src,
                          traffic_dest))

        df = pd.DataFrame(items,
                          columns=['flow_uid',
                                   'points',
                                   'possible_points',
                                   'desc',
                                   'srn_src',
                                   'srn_dest',
                                   'traffic_src',
                                   'traffic_dest'])

        df['lost_points'] = df.possible_points - df.points
        df['points_fraction'] = 100*df.points / df.possible_points
        return df.sort_values(by=['lost_points', 'flow_uid'],
                              ascending=[False, True])

    @property
    def max_flow_delay(self):
        """Maxmim delay per flow"""
        df = self.traffic

        df['delay'] = np.nan

        # Get index specifying received packets with a mandated latency
        tp = df.recv_time.notna() & df.max_latency_s.notna()
        df.loc[tp,'delay'] = df[tp].latency - df[tp].max_latency_s

        # Select on-time file transfers mandates
        ft = df.recv_time.notna() & df.file_transfer_deadline_s.notna()
        df.loc[ft,'delay'] = df[ft].latency - df[ft].file_transfer_deadline_s

        return df.groupby(['flow_uid']).\
            agg({'delay': 'max'}).\
            sort_values(by=['delay'], ascending=False)
