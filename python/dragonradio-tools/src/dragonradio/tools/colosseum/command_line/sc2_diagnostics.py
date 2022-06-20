# Copyright 2018-2022 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
import logging
from typing import Optional
import re
from rich import print

import numpy as np

from dragonradio.tools.colosseum import Scorer
from dragonradio.tools.logging.command_line import Command
from dragonradio.tools.logging.logging import LogCollection

from .command import Command

EXPECTED_EVM = { 9988: [-30, -25, -20, -15, -30] }

class SC2DiagnosticsCommand(Command):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    def add_arguments(self, parser):
        # Dump options
        dump_parser = parser.add_argument_group('Dump options')
        dump_parser.add_argument('--dump-environment', action='store_true',
                                 help='dump environment to CSV')
        dump_parser.add_argument('--dump-mandates', action='store_true',
                                 help='dump mandates to CSV')
        dump_parser.add_argument('--dump-flow-mp-scores', action='store_true',
                                 help='dump per-flow measurement period scores to CSV')
        dump_parser.add_argument('--dump-mp-scores', action='store_true',
                                 help='dump measurement period scores to CSV')
        dump_parser.add_argument('--dump-pe-scores', action='store_true',
                                 help='dump PE scores to CSV')
        dump_parser.add_argument('--dump-scores', action='store_true',
                                 help='dump (cumulative) scores to CSV')
        dump_parser.add_argument('--dump-threshold-success', action='store_true',
                                 help='dump threshold success to CSV')
        dump_parser.add_argument('--dump-ensemble-threshold-success', action='store_true',
                                 help='dump ensemble threshold success to CSV')
        dump_parser.add_argument('--dump-flows', action='store_true',
                                 help='dump flows to CSV')
        dump_parser.add_argument('--dump-flow', type=int,
                                 default=None,
                                 metavar='FLOWUID',
                                 help='dump specific flow to CSV')
        dump_parser.add_argument('--dump-mp', type=int,
                                 default=None,
                                 metavar='MP',
                                 help='dump specific measurement period to CSV')

        # Flow information
        flow_options = parser.add_argument_group('Flow information')
        flow_options.add_argument('--flows', action='store_true',
                                  help='show flows')
        flow_options.add_argument('--problem-flows', action='store_true',
                                  help='show problematic flows')
        flow_options.add_argument('--late-flows', action='store_true',
                                  help='show flows with late packets')
        flow_options.add_argument('--problem-srns', action='store_true',
                                  help='show problematic SRNs')

        # EVM information
        evm_options = parser.add_argument_group('EVM')
        evm_options.add_argument('--check-evm', action='store_true',
                                 default=False,
                                 help='show flows')

    def handle(self, parser, args):
        scorer = Scorer(self.reservation)

        our_srns = sorted(list(self.reservation.srns))

        print("Reservation SRNS:", ", ".join(str(srn) for srn in our_srns))

        #
        # Check for node errors
        #
        if self.reservation.has_crash:
            print("[bold red]The following nodes crashed:",
                  ", ".join(str(srn) for srn in sorted(list(self.reservation.crashed_nodes))))

        if self.reservation.has_traceback:
            print("[bold yellow]The following nodes have Python tracebacks:",
                  ", ".join(str(srn) for srn in sorted(list(self.reservation.traceback_nodes))))

        if self.reservation.has_uhd_error:
            print("[bold red]The following nodes encountered UHD errors:",
                  ", ".join(str(srn) for srn in sorted(list(self.reservation.uhd_error_nodes))))

        #
        # Check node connectivity
        #
        heard = self.reservation.heard
        all_srns = set().union(*heard.values())

        # Check for extra SRNs
        extra_srns = all_srns -  self.reservation.our_srns
        if len(extra_srns) != 0:
            print("[italic red]Observed nodes that were not part of the reservation:",
                  ", ".join(str(srn) for srn in sorted(list(extra_srns))))

        # Check for SRNs that weren't fully connected
        for srn in sorted(list(self.reservation.our_srns)):
            unheard = self.reservation.our_srns - heard[srn] - set([srn])
            if len(unheard) != 0:
                print(f"[italic red]SRN {srn:} did not hear from the following nodes:",
                      ", ".join(str(srn) for srn in sorted(list(unheard))))

        #
        # Check for node events
        #
        logs = LogCollection()
        logs.load(args.path,
                  start_time=args.start_time,
                  srn_logs_path=args.srn_logs)

        r = re.compile(r'^USRP: (RX|TX) error:')
        for srn in our_srns:
            try:
                df = logs[srn].event_cats.events
                df = df[df.category=='USRP']
                idx = df.event.str.match(r)
                if len(df[idx]) > 0:
                    print(f"[italic yellow]Node {srn:} encountered UHD warnings")
            except KeyError:
                logging.error("Could not read logs for SRN %d", srn)

        #
        # Compute EVM
        #
        for srn in our_srns:
            srn_df = logs[srn].recv

            # Compute measurement period for each packet
            srn_df['mp'] = 0

            for i, t in enumerate(scorer.stages):
                srn_df.loc[srn_df.aligned_timestamp >= t, 'mp'] = i+1

            # Look for lack of traffic
            df = srn_df.groupby('mp').agg({'evm': ['count']})

            missing_idx = (df.index > 0) & (df[('evm', 'count')]<1000)
            if missing_idx.any():
                missing = ",".join(str(i) for i in list(df.index[missing_idx]))
                print(f"[italic red]SRN {srn:} missing traffic in following stages: {missing:}")

            if args.check_evm:
                for curhop in srn_df.curhop.unique():
                    df = srn_df.loc[srn_df.curhop == curhop].groupby('mp').agg({'evm': [np.median, np.mean, np.std]})

                    # Look for suspicious EVM levels.
                    #
                    # We see if per-stage EVM levels are within two standard deviations
                    # of the expected EVM
                    if self.reservation.rf_scenario in EXPECTED_EVM:
                        try:
                            expected = EXPECTED_EVM[self.reservation.rf_scenario]
                            if not df.empty:
                                diff = np.abs(df.loc[1:5, ('evm', 'mean')] - expected) >= 3*df.loc[1:5, ('evm', 'std')]
                                if diff.any():
                                    print(f"Suspicious EVM for path {curhop:} -> {srn:}")
                                    print(df.loc[1:5])
                        except:
                            print(f"Could not look for suspicious EVM levels for path {curhop:} -> {srn:}")

        #
        # Check scores
        #
        for offender, point_loss in problem_srns(scorer):
            print("[italic red]SRN {} lost {:.0f} points".format(offender, point_loss))

        print(f"Phase 2 score: {scorer.final_match_score:}/{scorer.max_match_score:}")
        print(f"Phase 3 score: {scorer.phase3_score():g}/{scorer.max_phase3_score():g}")

def show_flows(scorer: Scorer, problem_flows : Optional[bool]=False):
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

def problem_srns(scorer: Scorer):
    point_loss = {}
    """Mapping from node to points lost"""

    offenders = []
    """Offending nodes"""

    # Build initial map from nodes to points lost
    for _, row in scorer.flows.iterrows():
        if row.lost_points != 0:
            point_loss[row.srn_src] = point_loss.get(row.srn_src, 0) + row.lost_points
            point_loss[row.srn_dest] = point_loss.get(row.srn_dest, 0) + row.lost_points

    while len(point_loss) != 0:
        # Pick worst offender
        all_nodes = sorted(point_loss.items(), key=lambda x : x[1], reverse=True)
        offender = all_nodes[0][0]

        # Record offenders
        offenders.append((offender, point_loss[offender]))

        # Remove losses for which worst offenders are responsible
        del point_loss[offender]

        for _, row in scorer.flows.iterrows():
            if row.lost_points != 0:
                if row.srn_src == offender and row.srn_dest in point_loss:
                    point_loss[row.srn_dest] -= row.lost_points

                if row.srn_dest == offender and row.srn_src in point_loss:
                    point_loss[row.srn_src] -= row.lost_points

        # Remove nodes that are not responsible for point losses
        point_loss = {k: v for k, v in point_loss.items() if v > 0}

    return offenders

def sc2_diagnostics():
    cmd = SC2DiagnosticsCommand()
    cmd.run('Provide diagnostics based on SC2 score')
