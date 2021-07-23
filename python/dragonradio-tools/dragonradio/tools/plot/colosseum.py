"""Plot Colosseum scores"""
import itertools
from typing import Optional

import pandas as pd

from matplotlib.axes import Axes
from matplotlib.figure import Figure
import matplotlib.pyplot as plt
from matplotlib.ticker import MaxNLocator
import matplotlib.transforms as transforms

from dragonradio.tools.colosseum import ReservationLog, Scorer
from dragonradio.tools.plot.util import addCheckboxWidget
from dragonradio.tools.plot.util import Plot, AnnotatedPlot

def plotStages(ax: Axes,
               scorer: Scorer,
               color='r',
               linestyle='--',
               linewidth=1,
               stage_scores: bool=False):
    """Plot scenario stages, optionally with per-stage scores.

    Args:
        ax: matplotlib Axes on which to plot stages
        scorer: Colosseum scorer
        color: Color of vertical stage separator
        linestyle: Style of vertical stage separator
        linewidth: Width of vertical stage separator
        stage_scores: True if stage scores should be plotted.
    """
    trans = transforms.blended_transform_factory(ax.transData, ax.transAxes)

    for i in range (0, len(scorer.stages)):
        ax.axvline(scorer.stages[i], color=color, linestyle=linestyle, linewidth=linewidth)
        if stage_scores:
            score = '{}/{}'.format(scorer.stage_scores[i], scorer.max_stage_scores[i])
            ax.text(scorer.stages[i],
                    0.95,
                    score,
                    size=12,
                    fontweight='black',
                    transform=trans)

def plotIndicator(ax: Axes,
                  df: pd.DataFrame,
                  col: str,
                  alpha=0.1,
                  color='red'):
    """Plot a Boolean indicator"""
    changes = df[df[col] != df[col].shift(1)].index
    for i, mp in enumerate(changes):
        if df.loc[mp].all():
            if i == len(changes)-1:
                ax.axvspan(changes[i],
                           df.index.tolist()[-1],
                           alpha=alpha,
                           color=color)
            else:
                ax.axvspan(changes[i],
                           changes[i+1],
                           alpha=alpha,
                           color=color)

class ScorePlot(Plot):
    """Colosseum reservation score plot"""
    def __init__(self,
                 reservation: ReservationLog,
                 scorer: Scorer,
                 fig: Optional[Figure] = None,
                 ax: Optional[Axes] = None,
                 plot_reported=False,
                 phase3=False,
                 checkboxes=True,
                 grid=False,
                 alpha=1.0):
        """Plot SC2 score.

        Args:
            reservation: A ReservationLog.
            scorer: A Scorer.
            fig: A matplotlib Figure.
            ax: A matplotlib Axes.
            plot_reported: True if reported scores should be plotted.
            phase3: True if Phase 3 score should be plotted (default is Phase 2).
            checkboxes: True if the plot should have checkboxes to enable/disable
                individual scores.
            grid: True if grid should be displayed.
            alpha: opacity value for lines
        """
        if fig is None:
            fig = plt.figure()

        if ax is None:
            ax = fig.add_subplot(1,1,1)

        super().__init__(fig, ax)

        self.reservation = reservation
        """Colosseum reservation"""

        self.scorer = scorer
        """Colosseum scorer"""

        self.color = itertools.cycle(plt.rcParams['axes.prop_cycle'].by_key()['color'])
        """Plot colors"""

        self.marker = itertools.cycle(('.', '+', 'o', 'x'))
        """Plot markers"""

        self.lines = []
        """Plotted lines"""

        self.plot_reported = plot_reported
        """Plot reported scores"""

        self.phase3 = phase3
        """Plot SC2 Phase 3 score (default is Phase 2)"""

        self.checkboxes = checkboxes
        """True if the plot should have checkboxes to enable/disable individual scores."""

        self.grid = grid
        """True if grid should be displayed."""

        self.alpha = alpha
        """Opacity value for lines."""

        self.plot()

    def addLine(self, line):
        self.lines.append(line)

    def plotTeamScore(self, team, score_col):
        """Plot team scores"""
        df = getattr(self.scorer, score_col)
        try:
            df_team = df.loc[team]
        except:
            #logging.exception("Could not get scores for team %d", team)
            return

        if team == self.reservation.our_team:
            base_label = "DragonRadio (Team {})".format(team)
        else:
            base_label = "Team {}".format(team)

        if self.phase3:
            label = base_label + " ({}/{})".format(df_team.score.max(),
                                                   int(df_team.max_score.max()))
        else:
            label = base_label

        line, = self.ax.plot(df_team.index.get_level_values('mp'),
                             df_team[score_col],
                             color=next(self.color),
                             marker=next(self.marker),
                             label=label,
                             alpha=self.alpha)
        self.addLine(line)

    def plotTeamScores(self):
        """Plot team scores"""
        if self.phase3:
            score_col = 'score'
        else:
            score_col = 'mp_score'

        for team in self.reservation.teams:
            self.plotTeamScore(team, score_col=score_col)

    def plotGate(self):
        plotIndicator(self.ax,
                      self.scorer.gates,
                      'gate',
                      alpha=0.1,
                      color='red')

    def plotThresholdSuccess(self):
        plotIndicator(self.ax,
                      self.scorer.ensemble_threshold_success,
                      'threshold_success',
                      alpha=0.1,
                      color='green')

    def plotEnsembleScore(self):
        if self.phase3:
            df = self.scorer.ensemble_score
            y = df.score
        else:
            df = self.scorer.ensemble_mp_score
            y = df.mp_score

        x = df.index.get_level_values('mp')

        line, = self.ax.plot(x, y,
                             color=next(self.color),
                             marker=next(self.marker),
                             label="Ensemble",
                             alpha=self.alpha)
        self.addLine(line)

    def plotReportedScore(self):
        df = self.reservation.reported_scores
        line = self.ax.scatter(df.mp,
                               df.mandates_achieved,
                               color=next(self.color),
                               marker=next(self.marker),
                               label="Gatway (reported)",
                               alpha=self.alpha)
        self.addLine(line)

    def plot(self):
        self.ax.set_xlabel('Measurement Period')
        self.ax.set_ylabel('Score')
        self.ax.yaxis.set_major_locator(MaxNLocator(integer=True))

        # Plot gate and threshold success
        self.plotGate()
        self.plotThresholdSuccess()

        # Plot team scores
        self.plotTeamScores()

        # Plot ensemble score
        self.plotEnsembleScore()

        # Plot vertical lines separating stages along with per-stage scores
        plotStages(self.ax, self.scorer, stage_scores=True)

        # Plot total score
        score = '{}/{}'.format(self.scorer.final_match_score, self.scorer.max_match_score)
        trans = transforms.blended_transform_factory(self.ax.transData, self.ax.transAxes)
        self.ax.text(self.scorer.stages[-1], 0.90, score, size=12, fontweight='black', transform=trans)

        # Plot score reported by gateway
        if not self.phase3 and self.plot_reported:
            self.plotReportedScore()

        # Set title
        self.ax.set_title('Reservation {} (Score {}/{})'.format(self.reservation.reservation_id, self.scorer.final_match_score, self.scorer.max_match_score))

        # Finish plot
        if self.grid:
            self.ax.minorticks_on()
            self.ax.grid(which='major', linestyle='-', linewidth='0.5', color='black')
            self.ax.grid(which='minor', linestyle=':', linewidth='0.5', color='black')

        self.fig.tight_layout()

        if self.checkboxes:
            self.fig.subplots_adjust(right=0.83)
            addCheckboxWidget(self.fig, self.lines, match_legend=True)
        else:
            self.ax.legend()

        self.set_window_title('Reservation {}'.format(self.reservation.reservation_id))

def pprMgen(pkt):
    return "seq={seq}".format(seq=pkt.seq)

class FlowPlot(AnnotatedPlot):
    """Colosseum reservation flow plot."""
    def __init__(self,
                 reservation: ReservationLog,
                 scorer: Scorer,
                 flow_uid: int,
                 fig: Optional[Figure]=None,
                 **kwargs):
        """Plot a flow.

        Args:
            reservation: A ReservationLog.
            scorer: A Scorer.
            flow_uid: Flow to plot.
            fig: A matplotlib Figure.
        """
        if fig is None:
            fig = plt.figure()

        super().__init__(fig, None, **kwargs)

        self.reservation = reservation
        """Colosseum reservation"""

        self.scorer = scorer
        """Colosseum scorer"""

        self.flow_uid = flow_uid
        """Flow UID"""

        # Compute actual received time of packets, i.e., time since scenario
        # start
        df = scorer.traffic

        start = pd.Timestamp(reservation.rf_start_time, unit='s')
        df['actual_recv_time'] = (df.recv_time - start).dt.total_seconds()
        df = scorer.traffic.xs(self.flow_uid, level='flow_uid')

        self.df = df
        """Traffic DataFrame"""

        self.srn_dest = df.srn_src.iloc[0]
        """Flow SRN source"""

        self.srn_dest = df.srn_dest.iloc[0]
        """Flow SRN dest"""

        self.traffic_dest = df.traffic_src.iloc[0]
        """Flow traffic source"""

        self.traffic_dest = df.traffic_dest.iloc[0]
        """Flow traffic dest"""

        # Get flow mandate
        self.mandate = scorer.scenario.mandates.xs(self.flow_uid, level='flow_uid').iloc[0]

        # Traffic indices for on time/received packets
        self.not_received = pd.isna(df.recv_time)
        self.received = ~self.not_received
        self.ontime = (df.ontime == 1)
        self.not_ontime = self.received & (~self.ontime)

        self.plotSent()
        self.plotListen()
        self.plotLatency()
        self.plotGoalRegions()

        # Add checkbox widgets
        self.fig.tight_layout()
        self.fig.subplots_adjust(right=0.83)
        addCheckboxWidget(self.fig, self.send_lines, ax=self.send_ax, match_legend=True)
        addCheckboxWidget(self.fig, self.recv_lines, ax=self.listen_ax, match_legend=True)
        addCheckboxWidget(self.fig, self.lat_lines, ax=self.latency_ax, match_legend=True)

        # Set title
        self.set_window_title('Reservation {}, Flow {}'.format(reservation.reservation_id, flow_uid))

        # Connect annotations
        self.connect_hover()

    def plotTraffic(self, ax, lines, xcol, ycol, idx, color=None, s=None, label=None):
        line = ax.scatter(self.df[idx][xcol],
                          self.df[idx][ycol],
                          color=color,
                          s=s,
                          label=label)
        line.df = self.df[idx]
        line.ppr = pprMgen
        self.addLine(ax, line)
        lines.append(line)

    def plotSent(self):
        self.send_ax = self.fig.add_subplot(311)
        self.addAnnotation(self.send_ax)

        self.send_lines = []

        params = [ (self.ontime,       'k', 10, 'on time')
                 , (self.not_ontime,   'r', 10, 'late')
                 , (self.not_received, 'b', 10, 'not received')
                 ]

        for (idx, color, s, label) in params:
            self.plotTraffic(self.send_ax,
                             self.send_lines,
                             'time',
                             'seq',
                             idx,
                             color=color,
                             s=s,
                             label=label)

        self.send_ax.yaxis.set_major_locator(MaxNLocator(integer=True))

        self.send_ax.set_title('MGEN sent')
        self.send_ax.set_xlabel('Time (sec)')
        self.send_ax.set_ylabel('MGEN seqno')

    def plotListen(self):
        self.listen_ax = self.fig.add_subplot(312,
                                              sharex=self.send_ax,
                                              sharey=self.send_ax)
        self.addAnnotation(self.listen_ax)

        self.recv_lines = []

        params = [ (self.ontime,       'actual_recv_time', 'k', 10, 'on time')
                 , (self.not_ontime,   'actual_recv_time', 'r', 10, 'late')
                 , (self.not_received, 'time',             'b', 10, 'not received')
                 ]

        for (idx, xcol, color, s, label) in params:
            self.plotTraffic(self.listen_ax,
                             self.recv_lines,
                             xcol,
                             'seq',
                             idx,
                             color=color,
                             s=s,
                             label=label)

        self.listen_ax.set_title('MGEN received')
        self.listen_ax.set_xlabel('Time (sec)')
        self.listen_ax.set_ylabel('MGEN seqno')

    def plotLatency(self):
        self.latency_ax = self.fig.add_subplot(313,
                                               sharex=self.send_ax)
        self.addAnnotation(self.latency_ax)

        self.lat_lines = []

        params = [ (self.ontime,     'k', 10, 'on time')
                 , (self.not_ontime, 'r', 10, 'late')
                 ]

        for (idx, color, s, label) in params:
            self.plotTraffic(self.latency_ax,
                             self.lat_lines,
                             'time',
                             'latency',
                             idx,
                             color=color,
                             s=s,
                             label=label)

        # Plot maximum latency
        if pd.isna(self.mandate.max_latency_s):
            y = self.mandate.file_transfer_deadline_s
        else:
            y = self.mandate.max_latency_s

        self.latency_ax.axhline(y,
                                color='r',
                                linestyle='--',
                                linewidth=1)

        self.latency_ax.set_title('Latency')
        self.latency_ax.set_xlabel('Time (sec)')
        self.latency_ax.set_ylabel('Latency (s)')

    def plotGoalRegions(self):
        df = self.scorer.per_mp_score.xs(self.flow_uid, level='flow_uid').reset_index('mp')
        df = df.set_index('mp')
        df['not_goal'] = df.active & (~(df.goal == 1))

        df_active = df[['active']]
        df_goal = df[['goal']]
        df_not_goal = df[['not_goal']]
        df_goal_stable = df[['goal_stable']]

        for ax in [self.send_ax, self.listen_ax]:
            plotIndicator(ax, df_not_goal, 'not_goal', alpha=0.1, color='r')
            plotIndicator(ax, df_goal_stable, 'goal_stable', alpha=0.1, color='g')
