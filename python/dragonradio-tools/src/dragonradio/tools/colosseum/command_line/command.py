# Copyright 2018-2022 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>
import argparse
from datetime import datetime, timezone
import logging
from typing import Optional

from ...logging import ReservationLog

class Command(object):
    start_time: Optional[datetime] = None
    """Override of log start time"""

    reservation: ReservationLog
    """Reservation lof"""

    def __init__(self):
        pass

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

        # Reservation options
        reservation_parser = parser.add_argument_group('Reservation')

        reservation_parser.add_argument('--start-time', type=float,
                                        default=None,
                                        metavar='SEC',
                                        help='set start time in seconds since the epoch')
        reservation_parser.add_argument('--srn-logs', type=str,
                                        default=None,
                                        metavar='DIR',
                                        help='directory where node logs are located')

        # Add additional arguments
        self.add_arguments(parser)

        # Add argument for path to log
        reservation_parser.add_argument('path',
                                        metavar='RESERVATION',
                                        help='path to reservation')

        # Parse arguments
        args = parser.parse_args()

        # Set up logging
        logging.basicConfig(format='%(asctime)s:%(name)s:%(levelname)s:%(message)s',
                            level=args.loglevel)

        # Determine start time
        if args.start_time:
            self.start_time = datetime.fromtimestamp(args.start_time, timezone.utc)

        # Open reservation log
        self.reservation = ReservationLog(args.path,
                                          rf_start_time=args.start_time,
                                          srn_logs_path=args.srn_logs)

        # Handle arguments
        return self.handle(parser, args)
