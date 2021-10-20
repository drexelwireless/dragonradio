#!/usr/bin/env python3
# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
import argparse
import datetime
from functools import reduce
import logging
import pytz

from .logging import LogCollection

UTC = pytz.timezone('UTC')

def add_annotate_args(parser):
    annotate_parser = parser.add_argument_group('Plot annotations')

    annotate_parser.add_argument('--annotate', action='store_const', const=True,
                                 dest='annotate',
                                 default=False,
                                 help='show annotations')
    annotate_parser.add_argument('--no-annotate', action='store_const', const=False,
                                 dest='annotate',
                                 help='do not show annotations')

class Command(object):
    def __init__(self : str=None):
        self.start_time : datetime.datetime = None
        """Override of log start time"""

        self.logs : LogCollection = None
        """Our log collection"""

        self.filters = []
        """DataFrame filters to apply"""

    def reset_filters(self):
        self.filters = []

    def filter_by(self, f):
        self.filters.append(f)

    @property
    def filter(self):
        def compose(f, g):
            return lambda x : f(g(x))

        return reduce(compose, self.filters, lambda x : x)

    def add_arguments(self, parser):
        pass

    def handle(self, parser, args):
        pass

    def run(self, *args, **kwargs):
        parser = argparse.ArgumentParser(*args, **kwargs)

        parser.add_argument('-d', '--debug', action='store_const',
                            const=logging.DEBUG,
                            dest='loglevel',
                            default=logging.WARNING,
                            help='print debugging information')
        parser.add_argument('-v', '--verbose', action='store_const',
                            const=logging.INFO,
                            dest='loglevel',
                            help='be verbose')

        # Logging options
        logging_parser = parser.add_argument_group('Log files')

        logging_parser.add_argument('--start-time', type=float,
                                    default=None,
                                    metavar='SEC',
                                    help='set start time in seconds since the epoch')
        logging_parser.add_argument('--srn-logs', type=str,
                                    default=None,
                                    metavar='DIR',
                                    help='directory where node logs are located')
        logging_parser.add_argument('--srn', action='append', type=int,
                                    dest='srns',
                                    default=None,
                                    metavar='NODE',
                                    help='load log for SRN')
        logging_parser.add_argument('paths',
                                    metavar='LOG',
                                    help='path to radio.h5 log or reservation',
                                    nargs='*')

        # Add additional arguments
        self.add_arguments(parser)

        # Parse arguments
        args = parser.parse_args()

        # Set up logging
        logging.basicConfig(format='%(asctime)s:%(name)s:%(levelname)s:%(message)s',
                            level=args.loglevel)

        # Determine start time
        if args.start_time:
            self.start_time = datetime.datetime.fromtimestamp(args.start_time, UTC)

        # Load logs
        self.logs = LogCollection(start_time=self.start_time)
        self.logs.load(args.paths, srn_logs_path=args.srn_logs, srns=args.srns)

        # Handle arguments
        return self.handle(parser, args)
