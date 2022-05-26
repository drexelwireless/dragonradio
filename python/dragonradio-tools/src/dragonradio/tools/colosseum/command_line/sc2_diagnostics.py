# Copyright 2018-2022 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
from typing import Optional
from rich import print

from dragonradio.tools.colosseum import Scorer
from dragonradio.tools.logging.command_line import Command

from .command import Command

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

    def handle(self, parser, args):
        print("Reservation SRNS:",
              ", ".join(str(srn) for srn in sorted(list(self.reservation.srns))))

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

        scorer = Scorer(self.reservation)

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
