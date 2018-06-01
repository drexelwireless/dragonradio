import argparse
import asyncio
import daemon
import daemon.pidfile
from functools import partial
import io
import libconf
import lockfile
import logging
import netifaces
import os
import random
import signal
import sys

import dragonradio
import dragon.radio
from dragon.controller import Controller

def configureLogging(args, config):
    # Determine directory where we will put logs
    node_id = dragon.radio.getNodeIdFromHostname()
    logdir = os.path.join(config.log_directory, 'node-{:03d}'.format(node_id))
    if not os.path.exists(logdir):
        os.makedirs(logdir)

    logger = logging.getLogger()
    logger.setLevel(logging.DEBUG)
    formatter = logging.Formatter('%(asctime)s:%(name)s:%(levelname)s:%(message)s')

    if args.foreground:
        loglevel = args.loglevel

        sh = logging.StreamHandler()
        sh.setFormatter(formatter)
        sh.setLevel(loglevel)
        logger.addHandler(sh)

        return (None, None, None)
    else:
        loglevel = getattr(logging, config.log_level)

        # Set up python logger
        fh = logging.FileHandler(os.path.join(logdir, 'dragonradio.log'), mode='a')
        fh.setFormatter(formatter)
        fh.setLevel(loglevel)
        logger.addHandler(fh)

        # Open files to log stdout and stderr
        fout = io.open(os.path.join(logdir, 'stdout.log'), 'a')
        ferr = io.open(os.path.join(logdir, 'stderr.log'), 'a')

        return (fh, fout, ferr)

def shutdown(controller, signum, frame):
    controller.stopRadio()

def run(args, config):
    # Configure logging
    (fh, fout, ferr) = configureLogging(args, config)

    # Create the controller
    controller = Controller(args, args.config, args.colosseum_ini)

    if args.foreground:
        signal.signal(signal.SIGINT, partial(shutdown, controller))

        controller.setupRadio(start=args.start)
    else:
        # See:
        #   https://www.python.org/dev/peps/pep-3143/
        #   https://dpbl.wordpress.com/2017/02/12/a-tutorial-on-python-daemon/
        with daemon.DaemonContext(files_preserve=[fh.stream],
                                  stdout=fout,
                                  stderr=ferr,
                                  detach_process=True,
                                  pidfile=daemon.pidfile.TimeoutPIDLockFile(args.pidfile),
                                  signal_map={signal.SIGTERM: partial(shutdown, controller),
                                              signal.SIGTSTP: partial(shutdown, controller)}):
            controller.setupRadio(start=args.start)

    return 0

def getPid(args):
    """Get the pid from the pidfile"""
    try:
        with io.open(args.pidfile, 'r') as f:
            return int(f.read().strip())
    except IOError:
        return None

def start(args, config):
    if not args.foreground:
        pid = getPid(args)
        if pid:
            print('pidfile {} already exists. Daemon already running?\n'.format(args.pidfile), file=sys.stderr)
            return 1

    return run(args, config)

def stop(args, config):
    pid = getPid(args)
    if not pid:
        print('pidfile {} does not exist. Daemon not running?\n'.format(args.pidfile), file=sys.stderr)
        return 0

    # Try killing the daemon process
    try:
        os.kill(pid, signal.SIGTERM)
    except OSError as ex:
        print('Could not terminate daemon: {}\n'.format(str(ex)), file=sys.stderr)
        return 1

    return 0

def main():
    parser = argparse.ArgumentParser(description='Run full-radio.',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    dragon.radio.addArguments(parser, allow_defaults=False)
    parser.add_argument('--foreground', action='store_true', dest='foreground',
                        default=False,
                        help='run as a foreground process')
    parser.add_argument('--config', action='store', dest='config',
                        default='/root/radio_api/radio.conf',
                        help='specify configuration file')
    parser.add_argument('--colosseum-ini', action='store', dest='colosseum_ini',
                        default='/root/radio_api/colosseum_config.ini',
                        help='specify Colosseum ini file')
    parser.add_argument('--pidfile', action='store', dest='pidfile',
                        default='/var/run/dragonradio.pid',
                        help='specify PID file')
    parser.add_argument('-d', '--debug', action='store_const', const=logging.DEBUG,
                        dest='loglevel',
                        default=logging.WARNING,
                        help='print debugging information')
    parser.add_argument('-v', '--verbose', action='store_const', const=logging.INFO,
                        dest='loglevel',
                        help='be verbose')
    parser.add_argument('-l', action='store', dest='logfile',
                        default=None,
                        help='log to file')
    parser.add_argument('--start', action='store_true', default=False,
                        help='immediately start the radio')
    parser.add_argument('action', choices=['start', 'stop', 'restart', 'status'])

    try:
        args = parser.parse_args()
    except SystemExit as ex:
        return ex.code

    # Load configuration. When we create the radio, it will also load the
    # configuration, but we need it here to set up logging info before we create
    # the radio
    with io.open(args.config) as f:
        config = libconf.load(f)

    if args.action == 'start':
        return start(args, config)
    elif args.action == 'stop':
        return stop(args, config)

if __name__ == '__main__':
    main()
