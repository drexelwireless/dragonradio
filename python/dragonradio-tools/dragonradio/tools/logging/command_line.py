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
        parser.add_argument('--start-time', type=float,
                            default=None,
                            metavar='SEC',
                            help='set start time in seconds since the epoch')
        parser.add_argument('--srn-logs', type=str,
                            default=None,
                            metavar='DIR',
                            help='directory where node logs are located')
        parser.add_argument('--srn', action='append', type=int,
                            dest='srns',
                            default=None,
                            metavar='NODE',
                            help='load log for SRN')
        parser.add_argument('paths', nargs='*')

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
