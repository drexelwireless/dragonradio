from collections import namedtuple
import functools
import logging
import math
import numpy as np
import os
import pandas as pd
import queue
import threading
import time

logger = logging.getLogger('scoring')

FlowStats = namedtuple('FlowStats', ['flow_uid', 'src', 'dest', 'first_mp', 'npackets', 'nbytes'])

MandatePerformance = namedtuple('MandatePerformance', ['scalar_performance', 'radio_ids', 'flow_id', 'hold_period', 'achieved_duration', 'point_value'])

class Scorer:
    def __init__(self, config):
        self.config = config
        """Our Config"""

        self.scenario_start_time = None
        """RF scenario start time"""

        self.lock = threading.Lock()
        """Lock on scoring data"""

        self.q = None
        """Queue for scoring work"""

        self.thread = None
        """Thread performing scoring work"""

        self.score = None
        """DataFrame containing scoring information"""

        self.stage = 0
        """Current scenario stage"""

        self.stage_timestamp = None
        """Timestamp of current scenario stage"""

        self.stage_timestamps = {}
        """Stages and their timestamps"""

        self.mandated_flows = set()
        """The set of mandated flows"""

        self.flow_links = {}
        """Link, i.e., source/destination pair, for each flow"""

        self.stats_max_mp = {}
        """Maximum MP for which stats have been received from each SRN"""

    def timeToMP(self, t, closest=False):
        """Convert time (in seconds since the epoch) to a measurement period"""
        if closest:
            return int(round(t - self.scenario_start_time) / self.config.measurement_period)
        else:
            return int((t - self.scenario_start_time) / self.config.measurement_period)

    def currentMP(self):
        """Current measurement period"""
        return self.timeToMP(time.time())

    def start(self):
        """Start scorer.

        This starts any threads needed for score computation.
        """
        self.lock = threading.Lock()

        self.q = queue.Queue()

        self.thread = threading.Thread(target=self.runScoring)
        self.thread.daemon = True
        self.thread.start()

    def stop(self):
        """Finish all scoring tasks"""
        # Get current mp
        mp = self.currentMP()

        # Finish all scoring tasks
        self.join()

        # Delete score entries for all future measurement periods
        with self.lock:
            if self.score is not None:
                self.score = self.score[self.score.index.get_level_values('mp') <= mp]

    def runScoring(self):
        while True:
            node_id, timestamp, stats, sent = self.q.get()
            try:
                with self.lock:
                    for flow in stats:
                        self.__updateFlowStatistics(node_id, timestamp, flow, sent=sent, recv=not sent)
            except:
                logger.exception('Exception in runScoring')
            self.q.task_done()

    def join(self):
        """Drain the task queue"""
        if self.q:
            logger.info('Waiting for scoring tasks to drain (%d tasks remaining)...', self.q.qsize())
            self.q.join()
            logger.info('Scoring tasks finished')

    def getMPStage(self, mp):
        """Return stage that given measurement period belongs to"""
        for stage in range (1, self.stage+1):
            if mp < self.stage_timestamps[stage]:
                return stage-1

        return self.stage

    def dumpScores(self, final=False):
        """Dump current scoring data to log."""
        config = self.config

        try:
            with self.lock:
                if final:
                    filename = 'score_final.csv'
                else:
                    filename = 'score_stage_{:02}.csv'.format(self.stage)

                if self.score is not None:
                    self.score.to_csv(os.path.join(config.logdir, filename))
        except:
            logger.exception('Exception when dumping scores')

    def updateGoals(self, goals, timestamp, max_stage_mps=15*60):
        """Update mandated goals.

        Arguments:
            goals: mandated outcome goals in JSON format
            timestamp: timestamp of goal update
            max_stage_mps: max number of measurement periods in a stage
        """
        with self.lock:
            #
            # Calculate current stage
            #
            self.stage += 1
            self.stage_timestamp = self.timeToMP(timestamp, closest=True)
            self.stage_timestamps[self.stage] = self.stage_timestamp

            logger.info('Scenario stage timestamp = %d', self.stage_timestamp)

            #
            # Build DataFrame with space for new scores
            #
            self.mandated_flows = set()

            items = []

            for goal in goals:
                flow_uid = goal['flow_uid']
                point_value = goal.get('point_value', 1)
                hold_period = goal['hold_period']
                max_latency_s = goal['requirements'].get('max_latency_s', None)
                min_throughput_bps = goal['requirements'].get('min_throughput_bps', None)
                file_transfer_deadline_s = goal['requirements'].get('file_transfer_deadline_s', None)

                self.mandated_flows.add(flow_uid)

                if self.stage == 1:
                    start = 0
                else:
                    start = self.stage_timestamp

                for mp in range(start, start + max_stage_mps):
                    items.append((flow_uid, mp, self.stage, 0, 0, 0, 0, 0, 0, 0, 0, point_value, hold_period, max_latency_s, min_throughput_bps, file_transfer_deadline_s))

            df = pd.DataFrame(items,
                              columns=['flow_uid',
                                       'mp',
                                       'stage',
                                       'npackets_sent',
                                       'nbytes_sent',
                                       'update_timestamp_sent',
                                       'npackets_recv',
                                       'nbytes_recv',
                                       'update_timestamp_recv',
                                       'goal',
                                       'goal_stable',
                                       'point_value',
                                       'hold_period',
                                       'max_latency_s',
                                       'min_throughput_bps',
                                       'file_transfer_deadline_s'])

            #
            # Merge old scores with new scorees
            #
            if self.score is not None:
                df = pd.concat([self.score[self.score.index.get_level_values('mp') < self.stage_timestamp].reset_index(), df], sort=False)

            #
            # Create index on flow, stage, and mp
            #
            df.set_index(['flow_uid', 'mp'], inplace=True)
            df.sort_index(inplace=True)

            self.score = df

    def updateScore(self):
        """Update score based on collected metrics."""
        config = self.config

        with self.lock:
            if self.score is None:
                return

            self.score = scoreGoals(self.score)

    def updateMandatedOutcomes(self, mp, mandated_outcomes):
        """Update mandated outcomes with scoring data from given measurement period"""
        config = self.config

        with self.lock:
            now = self.currentMP()

            mandates_achieved = 0
            total_score_achieved = 0

            for mandate in mandated_outcomes.values():
                if mandate.flow_uid in self.flow_links:
                    (src, dest) = self.flow_links[mandate.flow_uid]
                    mandate.radio_ids = [src, dest]
                else:
                    mandate.radio_ids = []

                try:
                    df = self.score.loc[(mandate.flow_uid, mp)]
                    mandate.achieved_duration = int(df.achieved_duration)

                    if mandate.achieved_duration >= mandate.hold_period:
                        mandates_achieved += 1
                        total_score_achieved += mandate.point_value
                except:
                    logger.info('Could not index stats: (%s, %s)',
                        mandate.flow_uid,
                        mp)

            # Log data used to generate report
            if config.log_scoring:
                filename = 'score_now_{:03d}_mp_{:03d}_achieved_{:d}_score_{:d}.csv'.format(now, mp, mandates_achieved, total_score_achieved)

                self.score.to_csv(os.path.join(config.logdir, filename))

    def updateSourceStats(self, node_id, timestamp, sources):
        if self.q:
            self.q.put((node_id, timestamp, sources, True))

    def updateSinkStats(self, node_id, timestamp, sinks):
        if self.q:
            self.q.put((node_id, timestamp, sinks, False))

    def __updateFlowStatistics(self, node_id, timestamp, flow, sent=False, recv=False):
        # Skip recording statistics if we don't have mandates yet
        if self.score is None or flow.flow_uid not in self.score.index.get_level_values('flow_uid'):
            return

        # Determine statistics column suffix
        if sent:
            col_sfx = 'sent'
        elif recv:
            col_sfx = 'recv'
        else:
            raise Exception('updateFlowStatistics: must specify sent or recv')

        # Record link between nodes
        self.flow_links[flow.flow_uid] = (flow.src, flow.dest)

        # Determine maximum MP for which we received statistics
        max_mp = flow.first_mp + len(flow.npackets) - 1

        # Record maximum MP
        self.stats_max_mp[node_id] = max(self.stats_max_mp.get(node_id, 0), max_mp)

        # Record flow statistics
        logger.info("Updating flow statistics %s: node=%d; flow=%d; timestamp=%f; current mp=%d; first_mp=%d; max_mp=%d; npackets=%s; nbytes=%s",
            col_sfx,
            node_id,
            flow.flow_uid,
            timestamp,
            self.currentMP(),
            flow.first_mp,
            max_mp,
            flow.npackets,
            flow.nbytes)

        for i in range(0, len(flow.npackets)):
            mp = flow.first_mp + i

            try:
                if flow.npackets[i] > 0 and self.score.loc[(flow.flow_uid, mp), 'update_timestamp_' + col_sfx] < timestamp:
                    self.score.loc[(flow.flow_uid, mp), 'npackets_' + col_sfx] = flow.npackets[i]
                    self.score.loc[(flow.flow_uid, mp), 'nbytes_' + col_sfx] = flow.nbytes[i]
                    self.score.loc[(flow.flow_uid, mp), 'update_timestamp_' + col_sfx] = timestamp
            except:
                logger.info('Cannot access score index (%s, %s)', flow.flow_uid, mp)

def mkFlowStats(flowperf, low_mp, max_mp):
    """Construct a FlowStats object from a FlowPerformance object"""
    flow_uid = flowperf.flow_uid
    src = flowperf.src
    dest = flowperf.dest

    first_mp = min(low_mp, flowperf.low_mp)

    npackets = [mp.npackets for mp in flowperf.stats[first_mp:max_mp+1]]
    nbytes = [mp.nbytes for mp in flowperf.stats[first_mp:max_mp+1]]

    return FlowStats(flow_uid, src, dest, first_mp, npackets, nbytes)

def nonzeroFlowStats(flowperf):
    """Return True if stats are non-zero, False otherwise"""
    return any(x != 0 for x in flowperf.npackets)

def scoreGoals(df):
    """Score a DataFrame containing goals.

    Args:
        df: The data frame to score

    Returns:
        A scored DataFrame.
    """
    # Select good throughput mandates
    tp_good = df.max_latency_s.notna() & \
              (df.nbytes_sent > 0) & \
              ((df.nbytes_recv*8 >= df.min_throughput_bps) | \
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

    df['goal'] = df.groupby(['flow_uid'], sort=False)['goal'].apply(lambda x: x.ffill())

    # Set remaining NaN goals to 0
    df['goal'].fillna(0, inplace=True)

    # Calculate achieved duration
    df['achieved_duration'] = df.loc[df['goal'] == 1].groupby(['flow_uid', (df['goal'] != df['goal'].shift(1)).cumsum()]).cumcount()+1
    df['achieved_duration'].fillna(0, inplace=True)

    # Calculate stable goals
    df['goal_stable'] = df['achieved_duration'] >= df['hold_period']

    # Calculate MP scores
    df['mp_score'] = df.goal_stable * df.point_value

    return df
