# Copyright 2018-2023 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

import argparse

import dragonradio.config

class SC2Config(dragonradio.config.Config):
    bootstrap: bool = False
    "Immediately bootstrap the radio"

    foreground: bool = False
    "Run as a foreground process"

    radioconf_path: str = '/root/radio_api/radio.conf'
    """Path to Colosseum radio API configuration"""

    colosseum_ini_path: str = '/root/radio_api/colosseum_config.ini'
    """Path to Colosseum radio API .ini file"""

    pidfile: str = '/var/run/dragonradio.pid'
    """Path to pid file"""

    action: str
    """Action to take (start, stop, restart, status)"""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    def add_arguments(self, parser: argparse.ArgumentParser):
      super().add_arguments(parser)

      sc2 = parser.add_argument_group('SC2 radio options')
      sc2.add_argument('--bootstrap', action='store_const', const=True,
                       help='immediately bootstrap the radio')
      sc2.add_argument('--foreground', action='store_const', const=True,
                       help='run as a foreground process')
      sc2.add_argument('--colosseum-ini', type=str, action='store',
                       dest='colosseum_ini_path',
                       help='specify Colosseum ini file')
      sc2.add_argument('--pidfile', type=str, action='store',
                       dest='pidfile',
                       help='specify PID file')
