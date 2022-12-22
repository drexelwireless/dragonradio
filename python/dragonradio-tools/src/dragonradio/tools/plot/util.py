"""Plotting utilities"""
from typing import Any, Dict, List, Optional, Tuple, Union
from typing_extensions import TypeAlias

from matplotlib.axes import Axes
from matplotlib.backend_bases import Event
from matplotlib.collections import PathCollection
from matplotlib.figure import Figure
from matplotlib.lines import Line2D
from matplotlib.patches import Rectangle
from matplotlib.text import Annotation
from matplotlib.widgets import CheckButtons

import numpy as np
import pandas as pd
import scipy.signal as signal

# See:
#   http://stanford.edu/~raejoon/blog/2017/05/16/python-recipes-for-cdfs.html
#   https://stackoverflow.com/questions/24575869/read-file-and-plot-cdf-in-python
#   https://stackoverflow.com/questions/3209362/how-to-plot-empirical-cdf-in-matplotlib-in-python/11692365#11692365
def plotCCDF(ax, data):
    """Plot Complementary Cumulative Distribution Function"""
    sorted_data = np.sort(data)
    yvals = np.arange(1, len(sorted_data)+1)/float(len(sorted_data))
    #yvals = np.arange(len(sorted_data))/float(len(sorted_data)-1)
    ax.plot(sorted_data, 1-yvals)
    return sorted_data

def plotFIRCoefficients(ax, taps, title):
    """Plot filter tap coefficients.

    Args:
        taps: Filter taps
        title: Plot title
    """
    ax.plot(taps, 'bo-', linewidth=2)
    ax.set_title('Filter Coefficients %s (%d taps)' % (title, len(taps)))
    ax.grid(True)

def plotFIRResponse(ax, taps, fs, wp=None, ws=None, alpha=1.0, worN=512):
    """Plot filter response for one or more filters.

    Args:
        taps: List of (title, filter tap) pairs
        fs: Sample rate
        wp: Pass-band frequency (optional)
        ws: Stop-band frequency (optional)
        alpha: Transparency for response plots (default 1.0)
    """
    lines = []

    for (h, title) in taps:
        w, h = signal.freqz(h, worN=worN)

        line, = ax.plot(0.5*fs*w/np.pi, 20*np.log10(np.abs(h)), label=title, alpha=alpha)
        lines.append(line)

    if wp is not None:
        ax.axvline(x=0.5*wp, linestyle='dashed', label='Passband')

    if ws is not None:
        ax.axvline(x=0.5*ws, linestyle='dashed', label='Stopband')

    ax.set_xlim(0, 0.5*fs)
    ax.grid(True)
    ax.set_xlabel('Frequency (Hz)')
    ax.set_ylabel('Gain (dB)')
    ax.legend()
    ax.set_title('Frequency Response')

    return lines

class Plot:
    """A matplotlib plot"""
    fig: Figure
    """A matplotlib Figure"""

    ax: Optional[Axes] = None
    """A matplotlib Axis"""

    def __init__(self, fig: Figure, ax: Optional[Axes]=None):
        self.fig = fig
        self.ax = ax

        # This hack ensures that a reference to the Plot object hangs around as
        # long as the figure does. This is necessary for things like
        # AnnotatedPlot to work---for example, otherwise the references to
        # plotted lines get GC'ed, so hovering doesn't work.
        self.fig._plotref = self

    def set_window_title(self, title: str):
        """Set window title.

        Args:
            title (str): Window title.
        """
        if self.fig.canvas.manager is not None:
            self.fig.canvas.manager.set_window_title(title)

class AnnotatedPlot(Plot):
    AnnotationItem: TypeAlias = Union[Line2D, PathCollection, Rectangle]

    AnnotationItemElement: TypeAlias = Tuple[Any, Tuple[float, float], str]

    axes: Dict[Axes, Annotation]
    """Mapping from axis to axis annotation"""

    annotations: Dict[Axes, List[AnnotationItem]]
    """Mapping from axis to plotted lines"""

    annotate: bool = False
    """Should the plot show annotations?"""

    sticky: bool = False
    """Should annotation stick to previous hotspot?"""

    def __init__(self, fig, ax=None, annotate=False, sticky=False):
        super().__init__(fig, ax=ax)

        self.axes = {}
        self.annotations = {}
        self.annotate = annotate
        self.sticky = sticky

    def addAnnotationAxis(self, ax: Axes):
        self.axes[ax] = ax.annotate('',
                                    visible=False,
                                    xy=(0,0),
                                    xycoords='data',
                                    xytext=(20,20),
                                    textcoords='offset pixels',
                                    bbox=dict(boxstyle='round', fc='w'),
                                    arrowprops=dict(arrowstyle='->'))
        self.annotations[ax] = []

    def addAnnotationItem(self, ax: Axes, item: Union[Line2D, PathCollection]):
        self.annotations[ax].append(item)

    def findAnnotation(self, event: Event) -> Optional[Tuple[Annotation, Optional[AnnotationItemElement]]]:
        if event.inaxes in self.axes:
            # Get annotation for this axis
            annot = self.axes[event.inaxes]

            # Find annotation item corresponding to this event
            for item in self.annotations[event.inaxes]:
                if not item.get_visible():
                    continue

                cont, ind = item.contains(event)
                if cont:
                    return (annot, self.indexAnnotationItem(item, ind))

            return (annot, None)

        return None

    def indexAnnotationItem(self, item: AnnotationItem, ind: dict) -> AnnotationItemElement:
        if isinstance(item, Line2D):
            idx = ind['ind'][0]
            if isinstance(item.df, pd.DataFrame):
                idx_item = item.df.iloc[idx]
            else:
                idx_item = item.df

            xy = (item.get_xdata()[idx], item.get_ydata()[idx])
        elif isinstance(item, PathCollection):
            idx = ind['ind'][0]
            idx_item = item.df.iloc[idx]

            xy = item.get_offsets()[idx]
        elif isinstance(item, Rectangle):
            idx_item = item.df

            xy = (item.xy[0] + item.get_width()/2,
                  item.xy[1] + item.get_height()/2)
        else:
            raise ValueError("Line must be either a Line2D or a PathCollection")

        return (idx_item, xy, item.ppr)

    def connect_hover(self):
        """Connect hover event to show annotations"""
        if self.annotate:
            self.fig.canvas.mpl_connect("motion_notify_event", self.hover)

    def connect_pick(self):
        """Connect pick event"""
        self.fig.canvas.mpl_connect("pick_event", self.on_pick)

    def hover(self, event: Event):
        ann = self.findAnnotation(event)

        if ann is None:
            return

        annot, item = ann

        if item is None:
            # If we have no annotation item, hide the annotation
            if not self.sticky:
                annot.set_visible(False)
        else:
            elem, xy, ppr = item

            annot.xy = xy
            annot.set_visible(True)
            annot.set_text(ppr(elem))

        self.fig.canvas.draw_idle()

    def on_pick(self, event: Event):
        self.pick_artist(event.artist)

    def pick_artist(self, artist):
        pass

# See:
#   https://stackoverflow.com/questions/11551049/matplotlib-plot-zooming-with-scroll-wheel
def zoomFactory(fig, ax, base_scale=2.0):
    """Return a function that will zoom a figure on a scroll event"""
    def f(event):
        if event.inaxes == ax:
            # get the current x and y limits
            cur_xlim = ax.get_xlim()
            cur_ylim = ax.get_ylim()
            xdata = event.xdata # get event x location
            ydata = event.ydata # get event y location
            if event.button == 'up':
                # deal with zoom in
                scale_factor = 1/base_scale
            elif event.button == 'down':
                # deal with zoom out
                scale_factor = base_scale
            else:
                # deal with something that should never happen
                scale_factor = 1

            # set new limits
            ax.set_xlim([xdata - (xdata-cur_xlim[0]) / scale_factor,
                         xdata + (cur_xlim[1]-xdata) / scale_factor])
            ax.set_ylim([ydata - (ydata-cur_ylim[0]) / scale_factor,
                         ydata + (cur_ylim[1]-ydata) / scale_factor])
            fig.canvas.draw() # force re-draw

    return f

def addWidget(fig, widget):
    """Add a widget to a figure"""
    if not hasattr(fig, 'widgets'):
        fig.widgets = [widget]
    else:
        fig.widgets.append(widget)

def addCheckboxWidget(fig, lines,
                      ax=None,
                      match_legend=False,
                      label_match_legend=False,
                      left=0.84,
                      width=0.15,
                      height_factor=0.05):
    """Add a checkbox widget to a figure"""
    height = height_factor*len(lines)

    if ax:
        bbox = ax.get_position()

        max_height = bbox.y1 - bbox.y0
        if height > max_height:
            height = max_height

        bottom = bbox.y1 - height
    else:
        bottom = 0.92 - height

    rax = fig.add_axes([left, bottom, width, height])
    labels = [str(line.get_label()) for line in lines]
    visibility = [line.get_visible() for line in lines]
    check = CheckButtons(rax, labels, visibility)

    # Make checkbox label size smaller
    for i, l in enumerate(check.labels):
        l.set_fontsize(9)

    # Set checkbox label color to legend color
    if label_match_legend:
        for i, l in enumerate(check.labels):
            l.set_color(lines[i].get_color())

    # Set checkbox color to legend color
    if match_legend:
        for i, r in enumerate(check.rectangles):
            if isinstance(lines[i], Line2D):
                r.set_facecolor(lines[i].get_color())
            elif isinstance(lines[i], PathCollection):
                r.set_facecolor(lines[i].get_facecolors()[0])

    addWidget(fig, check)

    def func(label):
        index = labels.index(label)
        lines[index].set_visible(not lines[index].get_visible())
        fig.canvas.draw()
        fig.canvas.flush_events()

    check.on_clicked(func)

    return check
