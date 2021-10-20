# Copyright 2018-2021 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
import matplotlib as mp
import matplotlib.pyplot as plt

from dragonradio.tools.colosseum import Scorer
from dragonradio.tools.logging.command_line import Command
from dragonradio.tools.plot.colosseum import ScorePlot, FlowPlot

class PlotScoreCommand(Command):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    def add_arguments(self, parser):
        parser.add_argument('--dump-environment', action='store_true',
                            help='dump environment to CSV')
        parser.add_argument('--dump-mandates', action='store_true',
                            help='dump mandates to CSV')
        parser.add_argument('--dump-flow-mp-scores', action='store_true',
                            help='dump per-flow measurement period scores to CSV')
        parser.add_argument('--dump-mp-scores', action='store_true',
                            help='dump measurement period scores to CSV')
        parser.add_argument('--dump-pe-scores', action='store_true',
                            help='dump PE scores to CSV')
        parser.add_argument('--dump-scores', action='store_true',
                            help='dump (cumulative) scores to CSV')
        parser.add_argument('--dump-threshold-success', action='store_true',
                            help='dump threshold success to CSV')
        parser.add_argument('--dump-ensemble-threshold-success', action='store_true',
                            help='dump ensemble threshold success to CSV')
        parser.add_argument('--dump-flows', action='store_true',
                            help='dump flows')
        parser.add_argument('--dump-flow', type=int,
                            default=None,
                            metavar='FLOWUID',
                            help='dump specific flow')
        parser.add_argument('--dump-mp', type=int,
                            default=None,
                            metavar='MP',
                            help='dump specific measurement period')

        parser.add_argument('--plot-score', action='store_true',
                            help='plot score')
        parser.add_argument('--grid', action='store_true',
                            help='turn on plot grid')
        parser.add_argument('--reported', action='store_true',
                            help='plot score reported by gateway')
        parser.add_argument('--phase3', action='store_true',
                            help='plot Phase 3 score')

        parser.add_argument('--flows', action='store_true',
                            help='show flows')
        parser.add_argument('--problem-flows', action='store_true',
                            help='show problematic flows')
        parser.add_argument('--problem-srns', action='store_true',
                            help='show problematic SRNs')
        parser.add_argument('--late-flows', action='store_true',
                            help='show flows with late packets')
        parser.add_argument('--plot-flow', type=int,
                            default=None,
                            metavar='FLOWUID',
                            help='plot specific flow')

        parser.add_argument('--annotate', action='store_const', const=True,
                            dest='annotate',
                            default=False,
                            help='show annotations')
        parser.add_argument('--no-annotate', action='store_const', const=False,
                            dest='annotate',
                            help='do not show annotations')

    def handle(self, parser, args):
        reservation = self.logs.reservation
        scorer = Scorer(reservation, cache_path='cache')

        if args.dump_environment:
            scorer.scenario.mandates.to_csv('{}-environment.csv'.format(scorer.rf_scenario))

        if args.dump_mandates:
            scorer.scenario.mandates.to_csv('{}-mandates.csv'.format(scorer.rf_scenario))

        if args.dump_flow_mp_scores:
            scorer.per_mp_score.to_csv('{}-flow-mp-scores.csv'.format(reservation.reservation_id))

        if args.dump_mp_scores:
            scorer.mp_score.to_csv('{}-mp-scores.csv'.format(reservation.reservation_id))

        if args.dump_pe_scores:
            scorer.pe_score.to_csv('{}-pe-scores.csv'.format(reservation.reservation_id))

        if args.dump_scores:
            scorer.score.to_csv('{}-scores.csv'.format(reservation.reservation_id))

        if args.dump_threshold_success:
            scorer.threshold_success.to_csv('{}-threshold-success.csv'.format(reservation.reservation_id))

        if args.dump_ensemble_threshold_success:
            scorer.ensemble_threshold_success.to_csv('{}-ensemble-threshold-success.csv'.format(reservation.reservation_id))

        if args.dump_flows:
            scorer.flows.to_csv('{}-flows.csv'.format(reservation.reservation_id))

        if args.dump_flow:
            df = scorer.traffic

            df = df[df.index.get_level_values('flow_uid') == args.dump_flow]

            df.to_csv('{}-flow-{}.csv'.format(reservation.reservation_id, args.dump_flow))

        if args.dump_mp:
            df = scorer.goal_stable

            df['mp_score'] = df.goal_stable * df.point_value

            df_mp = df[df.index.get_level_values('mp') == args.dump_mp]
            df_mp.to_csv('{}-mp-{:d}.csv'.format(reservation.reservation_id, args.dump_mp))

            print(df_mp['mp_score'].sum())

        if args.late_flows:
            for flow_uid, row in scorer.max_flow_delay.iterrows():
                max_delay = row.delay
                if max_delay <= 0:
                    break

                print("Flow {:d} {:f}".format(flow_uid, max_delay))

        if args.flows or args.problem_flows:
            show_flows(scorer, problem_flows=args.problem_flows)

        if args.problem_srns:
            show_problem_srns(scorer)

        if args.plot_flow:
            FlowPlot(reservation, scorer, args.plot_flow, annotate=args.annotate)

            plt.show()

        if args.plot_score:
            ScorePlot(reservation, scorer,
                      plot_reported=args.reported,
                      phase3=args.phase3,
                      checkboxes=True,
                      grid=args.grid)

            plt.show()

def show_flows(scorer, problem_flows=False):
    for _, row in scorer.flows.iterrows():
        if row.points == row.possible_points:
            if problem_flows:
                break

            print("Flow {:d} ({:s}); {} -> {}".\
                format(row.flow_uid, row.desc, row.srn_src, row.srn_dest))
        else:
            print("Flow {:d} ({:s}); {} -> {}; lost {:.0f} points ({:.02f}%)".\
                format(row.flow_uid, row.desc,
                    row.srn_src, row.srn_dest,
                    row.lost_points, row.points_fraction))

def show_problem_srns(scorer):
    nodes = {}

    # Build initial map from nodes to points lost
    for _, row in scorer.flows.iterrows():
        if row.lost_points != 0:
            nodes[row.srn_src] = nodes.get(row.srn_src, 0) + row.lost_points
            nodes[row.srn_dest] = nodes.get(row.srn_dest, 0) + row.lost_points

    while len(nodes) != 0:
        # Pick worst offender
        all_nodes = sorted(nodes.items(), key=lambda x : x[1], reverse=True)
        offender = all_nodes[0][0]

        # Display worst offenders
        print("SRN {} lost {:.0f} points".format(offender, nodes[offender]))

        # Remove losses for which worst offenders are responsible
        del nodes[offender]

        for _, row in scorer.flows.iterrows():
            if row.lost_points != 0:
                if row.srn_src == offender and row.srn_dest in nodes:
                    nodes[row.srn_dest] -= row.lost_points

                if row.srn_dest == offender and row.srn_src in nodes:
                    nodes[row.srn_src] -= row.lost_points

        # Remove nodes that are not responsible for point losses
        nodes = {k: v for k, v in nodes.items() if v > 0}

def plot_score():
    mp.use('GTK3Agg')

    cmd = PlotScoreCommand()
    cmd.run('Plot Colosseum scores and flows')
