"""Plotting utilities"""
from matplotlib.collections import PathCollection
from matplotlib.lines import Line2D
from matplotlib.widgets import CheckButtons

import numpy as np
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

def plotFIRResponse(ax, taps, fs, wp=None, ws=None, alpha=1.0):
    """Plot filter response for one or more filters.

    Args:
        taps: List of (title, filter tap) pairs
        fs: Sample rate
        wp: Pass-band frequency (optional)
        ws: Stop-band frequency (optional)
        alpha: Transparency for response plots (default 1.0)
    """
    for (h, title) in taps:
        w, h = signal.freqz(h, worN=8000)

        ax.plot(0.5*fs*w/np.pi, 20*np.log10(np.abs(h)), label=title, alpha=alpha)

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

class Plot:
    """A matplotlib plot"""
    def __init__(self, fig, ax=None):
        self.fig = fig
        """A matplotlib Figure"""

        self.ax = ax
        """A matplotlib Axis"""

        # This hack ensures that a reference to the Plot object hangs around as
        # long as the figure does. This is necessary for things like
        # AnnotatedPlot to work---for example, otherwise the references to
        # plotted lines get GC'ed, so hovering doesn't work.
        self.fig._plotref = self

class AnnotatedPlot(Plot):
    def __init__(self, fig, ax=None, sticky=False):
        super().__init__(fig, ax=ax)

        self.annotations = {}
        """Mapping from axis to axis annotation"""

        self.lines = {}
        """Mapping from axis to plotted lines"""

        self.sticky = sticky
        """Should annotation stick to previous hotspot?"""

    def addAnnotation(self, ax):
        self.annotations[ax] = ax.annotate('',
                                           visible=False,
                                           xy=(0,0),
                                           xycoords='data',
                                           xytext=(20,20),
                                           textcoords='offset pixels',
                                           bbox=dict(boxstyle='round', fc='w'),
                                           arrowprops=dict(arrowstyle='->'))
        self.lines[ax] = []

    def addLine(self, ax, line):
        self.lines[ax].append(line)

    def hover(self, event):
        if event.inaxes in self.annotations:
            annot = self.annotations[event.inaxes]
            for line in self.lines[event.inaxes]:
                cont, ind = line.contains(event)
                if cont:
                    self.updateAnnotation(annot, line, ind)
                    self.fig.canvas.draw_idle()
                    return

            if not self.sticky:
                annot.set_visible(False)
                self.fig.canvas.draw_idle()

    def updateAnnotation(self, annot, line, ind):
        # Data point index
        idx = ind['ind'][0]

        if isinstance(line, Line2D):
            annot.xy = (line.get_xdata()[idx], line.get_ydata()[idx])
        elif isinstance(line, PathCollection):
            annot.xy = line.get_offsets()[idx]
        else:
            raise ValueError("Line must be either a Line2D or a PathCollection")

        annot.set_visible(True)
        annot.set_text(line.ppr(line.df.iloc[idx]))

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
