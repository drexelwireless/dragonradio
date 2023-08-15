# Copyright 2018-2023 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

import argparse
import asyncio
import json
import logging
import os
from pathlib import Path
import psutil

import dragonradio.remote.client as remote

class Command:
    description: str
    """Command line description"""

    timestamp: float
    """Time at which this process was created"""

    client: remote.RemoteClient
    """Remote Dragonradio API client"""

    def __init__(self, description: str='Interact with dragonradio.'):
        self.description = description

    def add_arguments(self, parser: argparse.ArgumentParser):
        subparsers = parser.add_subparsers()
        subparsers.required = True
        subparsers.dest = 'command'

        # Start radio
        start_parser = subparsers.add_parser('start', help='Start radio')
        start_parser.set_defaults(func=self.start)

        # Stop radio
        stop_parser = subparsers.add_parser('stop', help='Stop radio')
        stop_parser.set_defaults(func=self.stop)

        # Get radio status
        status_parser = subparsers.add_parser('status', help='Get radio status')
        status_parser.set_defaults(func=self.status)

        # Update mandated outcomes
        update_outcomes_parser = subparsers.add_parser('update-outcomes', help='Update mandated outcomes')
        update_outcomes_parser.add_argument('mandates', action='store', type=Path,
                                            nargs='?',
                                            default=Path('/root/radio_api/mandated_outcomes.json'),
                                            metavar='PATH',
                                            help='mandates (JSON)')
        update_outcomes_parser.set_defaults(func=self.update_outcomes)

        # Update environment
        update_environment_parser = subparsers.add_parser('update-environment', help='Update environment')
        update_environment_parser.add_argument('environment', action='store', type=Path,
                                               nargs='?',
                                               default=Path('/root/radio_api/environment.json'),
                                               metavar='PATH',
                                               help='environment (JSON)')
        update_environment_parser.set_defaults(func=self.update_environment)

    def handle(self, parser: argparse.ArgumentParser, args: argparse.Namespace) -> int:
        if not hasattr(args, 'func'):
            parser.error('Command not specified')

        loop = asyncio.get_event_loop()

        p = psutil.Process(os.getpid())

        self.timestamp = p.create_time()

        self.client = remote.RemoteClient(loop=loop)

        with self.client:
            result = args.func(args)

        loop.close()

        return result

    def run(self) -> int:
        parser = argparse.ArgumentParser(description=self.description,
                                         formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        parser.add_argument('-d', '--debug', action='store_const', const=logging.DEBUG,
                            dest='loglevel',
                            default=logging.WARNING,
                            help='print debugging information')
        parser.add_argument('-v', '--verbose', action='store_const', const=logging.INFO,
                            dest='loglevel',
                            help='be verbose')
        parser.add_argument('-n', '--dry-run', action='store_true',
                            default=False,
                            help='dry run')

        self.add_arguments(parser)

        try:
            args = parser.parse_args()
        except SystemExit as ex:
            return ex.code

        logging.basicConfig(format='%(asctime)s:%(name)s:%(levelname)s:%(message)s',
                            level=args.loglevel)

        return self.handle(parser, args)

    def start(self, args: argparse.Namespace) -> int:
        self.client.start(timestamp=self.timestamp)

        return 0

    def stop(self, args: argparse.Namespace) -> int:
        self.client.stop(timestamp=self.timestamp)

        return 0

    def status(self, args: argparse.Namespace) -> int:
        data = {}

        try:
            resp = self.client.status(timestamp=self.timestamp)

            data['STATE'] = remote.State.Name(resp.status.state)
            data['INFO'] = resp.status.info
        except Exception as err:
            data['STATE'] = 'OFF'
            data['INFO'] = str(err)

        print(json.dumps(data))

        return 0

    def update_outcomes(self, args: argparse.Namespace) -> int:
        with args.mandates.open('r') as f:
            goals = f.read()

        self.client.updateMandatedOutcomes(goals, timestamp=self.timestamp)

        return 0

    def update_environment(self, args: argparse.Namespace) -> int:
        with args.environment.open('r') as f:
            environment = f.read()

        self.client.updateEnvironment(environment, timestamp=self.timestamp)

        return 0

def client():
    command = Command()
    command.run()
