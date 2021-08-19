import collections
from enum import Enum

import numpy as np
import pandas as pd

import matplotlib as mpl
from matplotlib.axes import Axes
from matplotlib.figure import Figure

from dragonradio.tools.colosseum import ReservationLog
from dragonradio.tools.plot.util import addCheckboxWidget
from dragonradio.tools.plot.util import Plot

class Metric(Enum):
    """A MGEN metric"""
    rate = 'rate'
    count = 'count'
    latency = 'latency'
    interarrival = 'interarrival'
    loss = 'loss'
    late = 'late'

    def __str__(self):
        return str(self.value)

def rate(df: pd.DataFrame, win: float) -> pd.DataFrame:
    """Compute throughput in kbps.

    Args:
        df: A traffic dataframe.
        win: The time window over which to compute rate, in seconds.

    Returns:
        A DataFrame with a rate column giving throughput in kpbs.
    """
    # Size is in bytes, so we divide by the window size to get bytes per sec,
    # the multiply by 8/1024 to get kbps
    return ((df.nbytes.sum() / win)*8/1000).reset_index().rename(columns={'nbytes': 'rate'})

def count(df: pd.DataFrame) -> pd.DataFrame:
    """Compute packet count.

    Args:
        df: A traffic dataframe.

    Returns:
        A DataFrame with a 'count' column giving packet count.
    """
    return df.seq.count().reset_index().rename(columns={'seq': 'count'})

def latency(df: pd.DataFrame) -> pd.DataFrame:
    """Compute mean latency.

    Args:
        df: A traffic dataframe.

    Returns:
        A DataFrame with a 'latency' column giving mean latency.
    """
    return df.latency.mean().reset_index()

def interarrival(df: pd.DataFrame) -> pd.DataFrame:
    """Compute mean interarrival time.

    Args:
        df: A traffic dataframe.

    Returns:
        A DataFrame with an 'interarrival' column giving mean interarrival time.
    """
    return df.interarrival.mean().reset_index()

def loss(df: pd.DataFrame) -> pd.DataFrame:
    """Compute packet loss.

    Args:
        df: A traffic dataframe.

    Returns:
        A DataFrame with a 'loss' column giving fractional packet loss.
    """
    return (1.0 - df.received.sum()/df.received.count()).reset_index().rename(columns={'received': 'loss'})

def late(df: pd.DataFrame) -> pd.DataFrame:
    """Compute number of late packets.

    Args:
        df: A traffic dataframe.

    Returns:
        A DataFrame with a 'late' column giving number of late packets.
    """
    return (df.ontime.count() - df.ontime.sum()).reset_index().rename(columns={'ontime': 'late'})

def window(df: pd.DataFrame, win: float) -> pd.DataFrame:
    """Add windows to a traffic DataFrame.

    The window is in seconds and is a multiple of the argument 'win', so this
    function tags items with a window that is a multiple of win.

    Args:
        df: A traffic dataframe.
        win: A time window, in seconds

    Returns:
        A DataFrame with a 'window' column giving the window in which each
            sample occurred.
    """
    df['window'] = win * (df.time / win).astype(int) + win
    return df

def computeTrafficMetric(df: pd.DataFrame,
                         metric: Metric,
                         win: float,
                         per_flow: bool=True) -> pd.DataFrame:
    """Compute an MGEN traffic metric.

    Args:
        df: A traffic dataframe.
        metric: The metric to compute.
        win: The time sindow (in seconds) over which to calculate the metric
        per_flow: A flag indicating whether or not this metric should be
            computed on a per-flow or aggregate basis.

    Returns:
        A new DataFrame with a column with the specified metric.
    """
    if metric == Metric.loss:
        df['received'] = df.recv_time.notna()
    else:
        df.dropna(subset=['recv_time'], inplace=True)

    df = window(df, win)

    if per_flow:
        grp = df.groupby(['flow_uid', 'window'])
    else:
        grp = df.groupby(['window'])

    if metric == Metric.rate:
        return rate(grp, win)
    elif metric == Metric.count:
        return count(grp)
    elif metric == Metric.latency:
        return latency(grp)
    elif metric == Metric.interarrival:
        return interarrival(grp)
    elif metric == Metric.loss:
        return loss(grp)
    elif metric == Metric.late:
        return late(grp)
    else:
        raise ValueError("Unknown metric '{}'".format(metric))

def stairstep(data: np.ndarray, win: float):
    """Convert "ramped" data to stairsteps.

    This allows us to duplicate what trpr does when it IS NOT given the 'ramp'
    option.
    """
    if len(data) == 0:
        return np.empty((0, 2))

    # The end variable contains the metrics for the endpoints of each window
    end = data

    # Create a copy of the data, shifting the entries back by window seconds.
    # The begin variable contains the metrics for the starting points of each
    # window
    begin = np.copy(end)
    begin[1:,0] = begin[:-1,0]
    begin[0,0] -= win

    # Interleave the two to get "stairsteps"
    data = np.empty((begin.shape[0] + end.shape[0],2), dtype=begin.dtype)

    data[0::2,] = begin
    data[1::2,] = end

    return data

PlotParam = collections.namedtuple('PlotParam', ['title', 'ylabel', 'ylim'])

class TrafficMetricPlot(Plot):
    PLOT_PARAMS = { Metric.rate:         PlotParam('Throughput', 'Throughput (kbps)', {'bottom': 0})
                  , Metric.late:         PlotParam('Late Packets', 'Late Packet Count', {'bottom': 0})
                  , Metric.latency:      PlotParam('Mean Latency', 'Mean Latency (sec)', {'bottom': 0})
                  , Metric.loss:         PlotParam('Packet Loss', 'Packet Loss (fraction)', [0,1])
                  , Metric.count:        PlotParam('Packet Count', 'Packet Count', {'bottom': 0})
                  , Metric.interarrival: PlotParam('Mean Interarrival Time', 'Mean Interarrival Time (sec)', {'bottom': 0})
                  }

    def __init__(self,
                 fig: Figure,
                 ax: Axes,
                 reservation: ReservationLog,
                 df: pd.DataFrame,
                 title: str,
                 metric: Metric,
                 win: float,
                 per_flow: bool=True,
                 legend: bool=False,
                 checkboxes: bool=True):
        """Plot a traffic metric.

        Args:
            fig: A matplotlib figure.
            ax: A matplotlib axis.
            reservation: Reservation.
            df: Traffic data.
            title: Plot title.
            metric: The metric to plot.
            win: The window over which to plot the metric (in seconds).
            per_flow: True if metrics should be plotted per-flow.
            legend: Draw a legend.
            checkboxes: True if the plot should have checkboxes to enable/disable
                individual flows.

        Returns:
            None
        """
        super().__init__(fig, ax)

        df = computeTrafficMetric(df, metric, win, per_flow=per_flow)

        params = self.PLOT_PARAMS[metric]
        lines = []

        if per_flow:
            flows = sorted(df.groupby(['flow_uid']).groups.keys())
            for flow in flows:
                flow_df = df[df.flow_uid == flow]
                data = np.column_stack((flow_df.window.values, flow_df[str(metric)].values))
                data = stairstep(data, win)

                line, = ax.plot(data[:,0], data[:,1], label='Flow {}'.format(flow))
                lines.append(line)
        else:
            data = np.column_stack((df.window.values, df[str(metric)].values))
            data = stairstep(data, win)

            ax.plot(data[:,0], data[:,1])

        ax.set_title(f"{title} ({params.title})")
        ax.set_xlabel('Time (sec)')
        ax.set_ylabel(params.ylabel)
        ax.set_xlim(left=0)
        if type(params.ylim) == dict:
            ax.set_ylim(**params.ylim)
        else:
            ax.set_ylim(params.ylim)
        if legend and len(lines) != 0:
            ax.legend()
        fig.subplots_adjust(right=0.8)

        # Add checkboxes
        if checkboxes and len(lines) != 0:
            addCheckboxWidget(fig, lines, ax=ax, match_legend=True)

        if metric == Metric.rate and not per_flow:
            self.addThroughputMandates(reservation.traffic_scenario)

    THROUGHPUT_MANDATES = { 99771: [(0,   120, 20e3),
                                    (120, 240, 15e3),
                                    (240, 360, 10e3),
                                    (360, 480,  5e3)]
                          , 99801: [(0,   120, 20e3),
                                    (120, 240, 15e3),
                                    (240, 360, 10e3),
                                    (360, 480,  5e3),
                                    (480, 600, 20e3)]
                          , 99840: [(0,   120, 20e3),
                                    (120, 240, 15e3),
                                    (240, 360, 10e3),
                                    (360, 480,  5e3),
                                    (480, 600, 20e3)]
                          , 99880: [(0,   120, 20e3),
                                    (120, 240, 15e3),
                                    (240, 360, 10e3),
                                    (360, 480,  5e3),
                                    (480, 600, 20e3)]
                          }

    def addThroughputMandates(self, scenario_id):
        """Add throughput mandates for scenario"""
        if scenario_id in self.THROUGHPUT_MANDATES:
            mandates = self.THROUGHPUT_MANDATES[scenario_id]
            max_y = 0

            for (x1, x2, y) in mandates:
                self.ax.plot([x1, x2], [y, y], color='r', linestyle='--', linewidth=0.5)
                max_y = max(max_y, y)

            minorLocator = mpl.ticker.MultipleLocator(1e3)
            self.ax.yaxis.set_minor_locator(minorLocator)

            self.ax.set_ylim(top=max_y+6e3)
            self.ax.set_xlim(right=x2)
