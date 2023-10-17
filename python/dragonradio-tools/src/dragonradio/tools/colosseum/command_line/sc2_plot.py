# Copyright 2018-2023 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
import matplotlib as mp
import matplotlib.pyplot as plt

from dragonradio.tools.colosseum import Scorer
from dragonradio.tools.logging.command_line import add_annotate_args, Command
from dragonradio.tools.colosseum.plot import ScorePlot, FlowPlot

class SC2PlotCommand(Command):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    def add_arguments(self, parser):
        score_parser = parser.add_argument_group('Score plotting')
        score_parser.add_argument('--plot-score', action='store_true',
                                  help='plot score')
        score_parser.add_argument('--grid', action='store_true',
                                  help='turn on plot grid')
        score_parser.add_argument('--reported', action='store_true',
                                  help='plot score reported by gateway')
        score_parser.add_argument('--phase3', action='store_true',
                                  help='plot Phase 3 score')
        score_parser.add_argument('--plot-flow', type=int,
                                  default=None,
                                  metavar='FLOWUID',
                                  help='plot specific flow')

        add_annotate_args(parser)

    def handle(self, parser, args):
        reservation = self.logs.reservation
        scorer = Scorer(reservation)

        if args.plot_flow:
            FlowPlot(reservation, scorer, args.plot_flow,
                     annotate=args.annotate,
                     sticky=args.sticky)

            plt.show()

        if args.plot_score:
            ScorePlot(reservation, scorer,
                      plot_reported=args.reported,
                      phase3=args.phase3,
                      checkboxes=True,
                      grid=args.grid)

            plt.show()

def sc2_plot():
    mp.use('GTK3Agg')

    cmd = SC2PlotCommand()
    cmd.run('Plot Colosseum scores and flows')
