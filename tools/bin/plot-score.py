#!/usr/bin/env python3
# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
import argparse
import datetime
import logging
import pytz

import numpy as np
import pandas as pd

import matplotlib.pyplot as plt

from dragonradio.tools.colosseum import ReservationLog, Scorer
import dragonradio.tools.colosseum.scoring
from dragonradio.tools.plot.colosseum import ScorePlot, FlowPlot

UTC = pytz.timezone('UTC')

def main():
    parser = argparse.ArgumentParser(description='Plot a Colosseum reservation score or flow.',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-d', '--debug', action='store_const', const=logging.DEBUG,
                        dest='loglevel',
                        default=logging.WARNING,
                        help='print debugging information')
    parser.add_argument('-v', '--verbose', action='store_const', const=logging.INFO,
                        dest='loglevel',
                        help='be verbose')
    parser.add_argument('--start-time', type=float,
                        default=None,
                        metavar='SEC',
                        help='set RF scenario start time in seconds since the epoch')
    parser.add_argument('--scenarios',
                        type=str,
                        default=None,
                        metavar='DIR',
                        help='specify directory for scenario files')
    parser.add_argument('--srn-logs', type=str,
                        default=None,
                        metavar='DIR',
                        help='directory where node logs are located')

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
    parser.add_argument('--late-flows', action='store_true',
                        help='show flows with late packets')
    parser.add_argument('--plot-flow', type=int,
                        default=None,
                        metavar='FLOWUID',
                        help='plot specific flow')

    parser.add_argument('dir')

    # Parse arguments
    try:
        args = parser.parse_args()
    except SystemExit as ex:
        return ex.code

    logging.basicConfig(format='%(asctime)s:%(name)s:%(levelname)s:%(message)s',
                        level=args.loglevel)

    if args.start_time:
        rf_start_time = datetime.datetime.fromtimestamp(args.start_time, UTC)
    else:
        rf_start_time = None

    if args.scenarios is not None:
        dragonradio.tools.colosseum.scoring.scenarios_path = args.scenarios

    reservation = ReservationLog(args.dir,
                                 rf_start_time=rf_start_time,
                                 srn_logs_path=args.srn_logs,
                                 cache_path='cache')

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
        for _, row in scorer.flows.iterrows():
            if row.points == row.possible_points:
                if args.problem_flows:
                    break

                print("Flow {:d} ({:s}); {} -> {}".\
                    format(row.flow_uid, row.desc, row.srn_src, row.srn_dest))
            else:
                print("Flow {:d} ({:s}); {} -> {}; lost {:.0f} points ({:.02f}%)".\
                    format(row.flow_uid, row.desc,
                           row.srn_src, row.srn_dest,
                           row.lost_points, row.points_fraction))

    if args.plot_flow:
        FlowPlot(reservation, scorer, args.plot_flow)

        plt.show()

    if args.plot_score:
        ScorePlot(reservation, scorer,
                  plot_reported=args.reported,
                  phase3=args.phase3,
                  checkboxes=True,
                  grid=args.grid)

        plt.show()

if __name__ == '__main__':
    main()