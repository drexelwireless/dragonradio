# Copyright 2018-2022 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
from datetime import datetime
from functools import cached_property
import logging
from pathlib import Path
from typing import List, Mapping, Optional, Tuple

import numpy as np
import pandas as pd

from dragonradio.tools.cache import DataFrameCache, cached_dataframe_property

from .logging import ReservationLog
from .scenarios import Scenario

logger = logging.getLogger(__name__)

MP: int = 1
"""Measurement period"""

class Scorer(DataFrameCache):
    """Colosseum scoring"""

    reservation: ReservationLog
    """Reservation to score"""

    scenario: Scenario
    """Scenario being scored"""

    sanity_check: bool = False
    """Perform extra sanity checks"""

    def __init__(self, reservation, **kwargs):
        super().__init__(**kwargs)

        self.reservation = reservation
        self.scenario = Scenario(reservation)

    @property
    def cache_path(self) -> Path:
        """Path to cache holding serialized DataFrames"""
        return self._cache_path / 'scoring' / str(self.reservation_id) / str(self.rf_start_time.timestamp())

    @cached_property
    def reservation_id(self) -> int:
        """Reservation ID"""
        return self.reservation.reservation_id

    @cached_property
    def rf_scenario(self) -> int:
        """RF scenario ID"""
        return self.reservation.rf_scenario

    @cached_property
    def rf_start_time(self) -> datetime:
        """RF scenario start time (seconds since epoch)"""
        return self.reservation.rf_start_time

    @cached_property
    def valid(self) -> bool:
        """Return True if score information is valid."""
        return self.reservation.traffic is not None and not self.reservation.traffic.empty

    @cached_property
    def max_team_scores(self) -> pd.DataFrame:
        """Maximum possible score per team per stage."""
        return self.scenario.mandates.reset_index().\
                groupby(['team', 'stage']).\
                agg({ 'flow_uid': 'count' }).\
                rename(columns={ 'flow_uid': 'max_score' })

    @cached_property
    def max_stage_scores(self) -> List[float]:
        """Maximum possible score per stage."""
        df = self.max_team_scores.reset_index().groupby(['stage']).agg({ 'max_score': 'min' })

        return [df.loc[stage].max_score for stage in self.stages]

    @cached_property
    def stage_scores(self) -> Optional[pd.DataFrame]:
        """Stage scores"""
        df = self.ensemble_mp_score

        if df is None or df.empty:
            return None

        stages = self.stages
        stage_scores = []

        for i in range (0, len(stages)):
            if i == len(stages) - 1:
                stage_scores.append(df.loc[stages[i]//MP:].mp_score.max())
            else:
                stage_scores.append(df.loc[stages[i]//MP:stages[i+1]//MP].mp_score.max())

        return stage_scores

    @cached_property
    def stage_score_intervals(self) -> List[float]:
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
    def max_match_score(self) -> float:
        """Maximum possible match score."""
        return sum(self.max_stage_scores)

    @cached_property
    def final_match_score(self) -> Optional[float]:
        """Final match score"""
        if self.stage_scores is None:
            return None

        return sum(self.stage_scores)

    def phase3_score(self, team : Optional[int]=None) -> Optional[float]:
        """Phase 3 score."""
        if team is None:
            team = self.reservation.our_team

        df = self.score
        if df is not None:
            return df[df.index.get_level_values('team') == team].score.max()
        else:
            return None

    def max_phase3_score(self, team : Optional[int]=None) -> Optional[float]:
        """Maximum possible Phase 3 score."""
        if team is None:
            team = self.reservation.our_team

        df = self.score
        if df is not None:
            return df[df.index.get_level_values('team') == team].max_score.max()
        else:
            return None

    @cached_property
    def match_scores(self) -> Mapping[int, float]:
        """Match scores"""
        df = self.score

        match_scores = {}

        for team in self.reservation.teams:
            try:
                match_scores[team] = float(df.loc[team].score.max())
            except:
                match_scores[team] = 0

        return match_scores

    @cached_property
    def start(self) -> datetime:
        """Start of scoring period (seconds since epoch)"""
        if self.rf_start_time is not None:
            return self.rf_start_time
        else:
            df = self.reservation.traffic

            # Delete flow 0
            df.drop(df.index[df.flow_uid == 0], inplace=True)

            return df.send_time.min() - pd.Timedelta(seconds=self.stages[0])

    @cached_property
    def end(self) -> float:
        """End of scoring period (seconds since epoch)"""
        df = self.reservation.traffic

        return df.send_time.max()

    @cached_property
    def last_mp(self) -> int:
        """Last measurement period"""
        return int((self.end - self.start).total_seconds() / MP) - 1

    @cached_property
    def stages(self) -> List[float]:
        """Stages"""
        return sorted(list(self.scenario.mandates.index.get_level_values('stage').unique()))

    @cached_property
    def stage_end(self) -> List[float]:
        """Map from stage start time to last MP in stage"""
        ends = {}

        for i in range(0, len(self.stages)-1):
            ends[self.stages[i]] = int(self.stages[i+1] // MP) - 1

        ends[self.stages[-1]] = self.last_mp
        logger.info("Stage ends: %s", ends)

        return ends

    @cached_dataframe_property('traffic')
    #@property
    def traffic(self) -> Optional[pd.DataFrame]:
        """Scored traffic with duplicate received packets dropped"""
        df = self.reservation.traffic

        if df is None or df.empty:
            return None

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

        df = df[~df.index.duplicated(keep='first')].copy()

        # Compute stages
        logger.info('Computing stages')

        df['stage'] = np.NaN
        for stage in self.stages:
            df.loc[df.mp>=stage, 'stage'] = stage
        if df.stage.isnull().values.any():
            logger.info('Observed traffic that does not belong to a stage')
            return None
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

        return df

    @cached_dataframe_property('traffic_summary')
    def traffic_summary(self) -> Optional[pd.DataFrame]:
        df = self.traffic
        if df is None:
            return None

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

        return df

    def readIncumbentGates(self) -> List[Tuple[int, bool]]:
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
                with CilReader(self.reservation.srn_pcaps[srn], read_reg=False) as reader:
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

    @cached_dataframe_property('gates')
    def gates(self) -> pd.DataFrame:
        """Gate violations.

        The gate value in an MP is True if gated, i.e., there is a violation,
        False otherwise"""
        logger.info('Calculating gates')

        df = pd.DataFrame(self.readIncumbentGates(),
                          columns=['mp', 'gate']).astype({ 'mp': int , 'gate': float })
        df.set_index(['mp'], inplace=True)
        df.sort_index(inplace=True)

        return df

    @cached_dataframe_property('per_mp_score')
    def per_mp_score(self) -> Optional[pd.DataFrame]:
        """Flow score per measurement period.

        This DataFrame has an index on team, flow_uid, stage, and mp.
        """
        # Get traffic summary and gates
        df = self.traffic_summary

        if df is None or df.empty:
            return None

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

            return df.unstack(['team', 'stage', 'flow_uid']).reindex(range(stage_start, stage_end+1)).fillna(0).stack(['team', 'stage', 'flow_uid'], future_stack=True)

        df = df.groupby(['stage'], as_index=False).apply(fillMissingMPs).droplevel(0)

        # Re-index gates by measurement period and fill forward
        mp_index = df.index.get_level_values('mp').unique()
        gates_df = gates_df.reindex(mp_index, method='ffill')

        # Attach gates
        cols = pd.Index(['gate'])
        df = df.join(gates_df[cols], how='left')

        # Replace NaN gate values with 0
        df.fillna({'gate': 0}, inplace=True)

        # Convert gate column to bool
        df.gate = df.gate.astype(bool)

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

        # Score goals and make sure goal column is int
        df['goal'] = (tp_good | ft_good).astype(int)

        # Now we need to handle MPs with no traffic. We set their score to NaN
        # and then fill forward
        df.loc[df.nbytes_sent == 0, 'goal'] = np.NaN

        df['goal'] = df.groupby(['team', 'flow_uid'], sort=False, group_keys=False)['goal'].apply(lambda x: x.ffill())

        # Set remaining NaN goals to 0
        df.fillna({'goal': 0}, inplace=True)

        # If we are gated, goal is 0
        df.loc[df.gate == True, 'goal'] = 0

        #
        # Calculate achieved duration
        #
        df['achieved_duration'] = df.loc[df['goal'] == 1].groupby(['team', 'flow_uid', (df['goal'] != df['goal'].shift(1)).cumsum()]).cumcount()+1
        df.fillna({'achieved_duration': 0}, inplace=True)

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
    def mp_score(self) -> Optional[pd.DataFrame]:
        """Measurement period scores.

        This DataFrame has an index on team, stage, and mp.
        """
        df = self.per_mp_score

        if df is None or df.empty:
            return None

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
    def ensemble_mp_score(self) -> pd.DataFrame:
        """Ensemble score"""
        df = self.mp_score

        if df is None or df.empty:
            return None

        df = df.groupby(['mp']).agg({ 'mp_score': 'min'
                                    , 'max_mp_score': 'min'
                                    })
        df.sort_index(inplace=True)

        return df

    @cached_dataframe_property('threshold_success')
    def threshold_success(self) -> Optional[pd.DataFrame]:
        """Threshold success"""
        df = self.mp_score
        thresholds_df = self.scenario.environment

        if df is None or thresholds_df is None:
            return None

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
    def ensemble_threshold_success(self) -> Optional[pd.DataFrame]:
        """Ensemble threshold success"""
        df = self.threshold_success

        if df is None:
            return None

        df = df.groupby(['mp']).agg({'threshold_success': 'sum'})
        nteams = len(self.reservation.teams)
        df.threshold_success = (df.threshold_success == nteams).astype(int)
        df.sort_index(inplace=True)

        return df

    @cached_dataframe_property('pe_score')
    def pe_score(self) -> Optional[pd.DataFrame]:
        """Points earned"""
        df = self.threshold_success
        ensemble_success_df = self.ensemble_threshold_success
        mp_ensemble_score_df = self.ensemble_mp_score

        if df is None:
            return None

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
    def score(self) -> Optional[pd.DataFrame]:
        """Phase 3 scores"""
        df = self.pe_score

        if df is None:
            return None

        df['score'] = df.groupby('team').pe_score.cumsum()
        df['max_score'] = df.groupby('team').max_mp_score.cumsum()

        return df

    @cached_property
    def ensemble_score(self) -> Optional[pd.DataFrame]:
        """Phase 3 ensemble score"""
        df = self.ensemble_mp_score

        if df is None:
            return None

        df['score'] = df.mp_score.cumsum()
        df['max_score'] = df.max_mp_score.cumsum()

        return df

    @cached_property
    def flows(self) -> Optional[pd.DataFrame]:
        """Per-flow score information, sorted in descending order by lost points"""
        score_df = self.per_mp_score
        traffic_df = self.traffic

        if score_df is None or traffic_df is None:
            return None

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
    def max_flow_delay(self) -> pd.DataFrame:
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
