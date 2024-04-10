# Copyright 2018-2023 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

import argparse
import io
import logging
import os
from pathlib import Path
import signal
import sys
from typing import Optional

import daemon
import daemon.pidfile

from dragonradio.controller import Controller

from .config import SC2Config

class Command:
    description: str
    """Command line description"""

    config: SC2Config
    """SC2 configuration"""

    def __init__(self, description: str='Run SC2 variant of dragonradio.', config: Optional[SC2Config]=None):
        self.description = description

        if config is None:
            self.config = SC2Config()
        else:
            self.config = config

    def add_arguments(self, parser: argparse.ArgumentParser):
        parser.add_argument('action', choices=['start', 'stop', 'restart', 'status'])

    def handle(self, parser: argparse.ArgumentParser, args: SC2Config) -> int:
        if args.action == 'start':
            return self.start()
        elif args.action == 'stop':
            return self.stop()
        elif args.action == 'restart':
            return self.restart()
        elif args.action == 'status':
            return self.status()
        else:
            return 1

    def run(self) -> int:
        parser = self.config.parser(self.description)

        self.add_arguments(parser)

        # Read radio configuration first. This ensures options in the radio
        # configuration can be overridden on the command line.
        try:
            if os.path.exists(self.config.radioconf_path):
                self.config.load(Path(self.config.radioconf_path))
        except IOError:
            logging.debug("Could not load %s", self.config.radioconf_path)

        # Parse configuration options
        try:
            args = parser.parse_args(namespace=self.config)
        except SystemExit as err:
            if isinstance(err.code, int):
                return err.code
            else:
                return 1

        # Load Colosseum radio API .ini last since it overrides any command-line
        # options.
        try:
            if os.path.exists(self.config.colosseum_ini_path):
                self.config.load(Path(self.config.colosseum_ini_path))
        except IOError:
            logging.debug("Could not load %s", self.config.colosseum_ini_path)

        return self.handle(parser, args)

    def get_pid(self) -> Optional[int]:
        """Get radio pid from the pidfile"""
        try:
            with open(self.config.pidfile, 'r') as f:
                return int(f.read().strip())
        except IOError:
            return None

    def configure_logging(self) -> logging.Handler:
        """Configure Python logging"""
        logger = logging.getLogger()
        logger.setLevel(logging.DEBUG)
        formatter = logging.Formatter('%(asctime)s:%(name)s:%(levelname)s:%(message)s')
        logger.handlers = []

        protobuf_logger = logging.getLogger('protobuf')
        if self.config.log_protobuf:
            protobuf_logger.setLevel(logging.DEBUG)
        else:
            protobuf_logger.setLevel(logging.INFO)

        handler: logging.Handler

        if self.config.foreground:
            handler = logging.StreamHandler()
        else:
            if self.config.logdir is None:
                raise ValueError('A log directory must be specified when running as a daemon. See the -l option.')

            handler = logging.FileHandler(Path(self.config.logdir) / 'dragonradio.log', mode='a')

        handler.setFormatter(formatter)
        handler.setLevel(self.config.loglevel)
        logger.addHandler(handler)

        return handler

    def mkController(self) -> Controller:
        """Create a Controller object"""
        return Controller(self.config)

    def start(self) -> int:
        """Start the radio"""
        if not self.config.foreground:
            pid = self.get_pid()
            if pid is not None:
                try:
                    os.kill(pid, 0)
                except OSError:
                    # pid doesn't exist
                    os.unlink(self.config.pidfile)
                else:
                    # pid exists
                    print(f'PID in {self.config.pidfile:} exists. Daemon already running?', file=sys.stderr)
                    return 1

        # Configure logging
        handler = self.configure_logging()

        # Create the controller
        controller = self.mkController()

        if self.config.foreground:
            controller.setupRadio(bootstrap=self.config.bootstrap)
        else:
            assert self.config.logdir is not None
            assert isinstance(handler, logging.FileHandler)

            # Open files to log stdout and stderr
            fout = io.open(Path(self.config.logdir) / 'stdout.log', 'a')
            ferr = io.open(Path(self.config.logdir) / 'stderr.log', 'a')

            # See:
            #   https://www.python.org/dev/peps/pep-3143/
            #   https://dpbl.wordpress.com/2017/02/12/a-tutorial-on-python-daemon/
            with daemon.DaemonContext(files_preserve=[handler.stream],
                                      stdout=fout,
                                      stderr=ferr,
                                      detach_process=True,
                                      prevent_core=False,
                                      pidfile=daemon.pidfile.TimeoutPIDLockFile(self.config.pidfile)):
                controller.setupRadio(bootstrap=self.config.bootstrap)

        return 0


    def stop(self) -> int:
        """Stop the radio"""
        pid = self.get_pid()
        if pid is None:
            print(f'pidfile {self.config.pidfile:} does not exist. Daemon not running?', file=sys.stderr)
            return 1

        # Try killing the daemon process
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError as ex:
            print(f'Could not terminate daemon: {str(ex):}', file=sys.stderr)
            return 1
        finally:
            os.unlink(self.config.pidfile)

        return 0

    def restart(self) -> int:
        self.stop()
        return self.start()

    def status(self) -> int:
        return 0
