import asyncio
from functools import partial
import io
import logging
import os
import signal
import sys

import daemon
import daemon.pidfile

import dragonradio
import dragonradio.radio
from dragonradio.controller import Controller

def configureLogging(config):
    logger = logging.getLogger()
    logger.setLevel(logging.DEBUG)
    formatter = logging.Formatter('%(asctime)s:%(name)s:%(levelname)s:%(message)s')
    logger.handlers = []

    protobuf_logger = logging.getLogger('protobuf')
    if config.log_protobuf:
        protobuf_logger.setLevel(logging.DEBUG)
    else:
        protobuf_logger.setLevel(logging.INFO)

    if config.foreground:
        # Set up python logger
        sh = logging.StreamHandler()
        sh.setFormatter(formatter)
        sh.setLevel(config.loglevel)
        logger.addHandler(sh)

        (fh, fout, ferr) = (None, None, None)
    else:
        logdir = config.logdir

        # Set up python logger
        fh = logging.FileHandler(os.path.join(logdir, 'dragonradio.log'), mode='a')
        fh.setFormatter(formatter)
        fh.setLevel(config.loglevel)
        logger.addHandler(fh)

        # Open files to log stdout and stderr
        fout = io.open(os.path.join(logdir, 'stdout.log'), 'a')
        ferr = io.open(os.path.join(logdir, 'stderr.log'), 'a')

    return (fh, fout, ferr)

def sighandler(controller, _signum, _frame):
    asyncio.run_coroutine_threadsafe(controller.terminate(), controller.loop)

def run(config):
    # Configure logging
    (fh, fout, ferr) = configureLogging(config)

    # Create the controller
    controller = Controller(config)

    if config.foreground:
        signal.signal(signal.SIGINT, partial(sighandler, controller))
        signal.signal(signal.SIGTERM, partial(sighandler, controller))

        controller.setupRadio(bootstrap=config.bootstrap)
    else:
        # See:
        #   https://www.python.org/dev/peps/pep-3143/
        #   https://dpbl.wordpress.com/2017/02/12/a-tutorial-on-python-daemon/
        with daemon.DaemonContext(files_preserve=[fh.stream],
                                  stdout=fout,
                                  stderr=ferr,
                                  detach_process=True,
                                  prevent_core=False,
                                  pidfile=daemon.pidfile.TimeoutPIDLockFile(config.pidfile),
                                  signal_map={ signal.SIGINT: partial(sighandler, controller)
                                             , signal.SIGTERM: partial(sighandler, controller)
                                             }):
            controller.setupRadio(bootstrap=config.bootstrap)

    return 0

def getPid(config):
    """Get the pid from the pidfile"""
    try:
        with io.open(config.pidfile, 'r') as f:
            return int(f.read().strip())
    except IOError:
        return None

def start(config):
    if not config.foreground:
        pid = getPid(config)
        if pid:
            print('pidfile {} already exists. Daemon already running?\n'.format(config.pidfile), file=sys.stderr)
            return 1

    return run(config)

def stop(config):
    pid = getPid(config)
    if not pid:
        print('pidfile {} does not exist. Daemon not running?\n'.format(config.pidfile), file=sys.stderr)
        return 0

    # Try killing the daemon process
    try:
        os.kill(pid, signal.SIGTERM)
    except OSError as ex:
        print('Could not terminate daemon: {}\n'.format(str(ex)), file=sys.stderr)
        return 1

    return 0

RADIOCONF_PATH = '/root/radio_api/radio.conf'

def main():
    config = dragonradio.radio.Config()
    parser = config.parser()

    sc2 = parser.add_argument_group('SC2 radio options')
    sc2.add_argument('--bootstrap', action='store_true', default=False,
                     help='immediately bootstrap the radio')
    sc2.add_argument('--foreground', action='store_true', dest='foreground',
                     default=False,
                     help='run as a foreground process')
    sc2.add_argument('--colosseum-ini', action='store', dest='colosseum_ini_path',
                     default='/root/radio_api/colosseum_config.ini',
                     help='specify Colosseum ini file')
    sc2.add_argument('--pidfile', action='store', dest='pidfile',
                     default='/var/run/dragonradio.pid',
                     help='specify PID file')

    parser.add_argument('action', choices=['start', 'stop', 'restart', 'status'])

    if os.path.exists(RADIOCONF_PATH):
        config.loadConfig(RADIOCONF_PATH)

    try:
        parser.parse_args(namespace=config)
    except SystemExit as err:
        return err.code

    # pylint: disable=no-member
    config.loadColosseumIni(config.colosseum_ini_path)

    if config.action == 'start':
        return start(config)
    elif config.action == 'stop':
        return stop(config)

if __name__ == '__main__':
    main()
