import argparse
import asyncio
import configparser
from fractions import Fraction
import io
import libconf
import logging
import math
import numpy as np
import os
from pprint import pformat
import platform
import random
import re
import scipy.signal as signal
import scipy.stats as stats
import sys

import dragonradio
from dragonradio import Channel, Channels, MCS, TXParams, TXParamsVector
from dragon.signal import *

logger = logging.getLogger('radio')

def fail(msg):
    print(msg, file=sys.stderr)
    sys.exit(1)

def getNodeIdFromHostname():
    m = re.search(r'([0-9]{1,3})$', platform.node())
    if m:
        return int(m.group(1))
    else:
        logger.warning('Cannot determine node id from hostname')
        return None

class ExtendAction(argparse.Action):
    def __init__(self, option_strings, *args, **kwargs):
        super(ExtendAction, self).__init__(option_strings=option_strings,
                                           nargs=0,
                                           *args, **kwargs)

    def __call__(self, parser, namespace, values, option_string=None):
        items = getattr(namespace, self.dest) or []
        items.extend(self.const)
        setattr(namespace, self.dest, items)

class LogLevelAction(argparse.Action):
    def __init__(self, option_strings, *args, **kwargs):
        super(LogLevelAction, self).__init__(option_strings=option_strings,
                                             nargs=0,
                                             *args, **kwargs)

    def __call__(self, parser, namespace, values, option_string=None):
        setattr(namespace, self.dest, self.const)

        if self.const <= logging.INFO:
            namespace.verbose = True
        else:
            namespace.verbose = False

        if self.const <= logging.DEBUG:
            namespace.debug = True
        else:
            namespace.debug = False

class LoadConfigAction(argparse.Action):
    def __init__(self, option_strings, *args, **kwargs):
        super(LoadConfigAction, self).__init__(option_strings=option_strings,
                                               *args, **kwargs)

    def __call__(self, parser, namespace, values, option_string=None):
        namespace.loadConfig(values)

class Config(object):
    def __init__(self):
        # Set some default values
        self.loglevel = logging.WARNING
        self.verbose = False
        self.debug = False
        self.node_id = getNodeIdFromHostname()

        # Log parameters
        self.log_directory = None
        self.log_sources = []
        self.log_interfaces = []
        self.log_invalid_headers = False
        self.log_snapshots = False
        self.log_protobuf = False
        self.log_scoring = False
        self.compress_interface_logs = False
        # This is the actual path to the log directory
        self.logdir_ = None

        # USRP settings
        self.addr = ''
        self.rx_antenna = 'RX2'
        self.tx_antenna = 'TX/RX'

        # Frequency and bandwidth
        # Default frequency in the Colosseum is 1GHz
        self.frequency = 1e9
        self.bandwidth = 5e6
        self.max_bandwidth = 10e6
        self.rx_oversample_factor = 1.0
        self.tx_oversample_factor = 1.0
        self.channel_bandwidth = 1e6
        self.channel_guard_bandwidth = 0
        self.edge_guard_bandwidth = None
        self.maximize_channel_guard_bandwidth = True

        # TX/RX gain parameters
        self.tx_gain = 25
        self.rx_gain = 25
        self.soft_tx_gain = -8
        self.auto_soft_tx_gain = None
        self.auto_soft_tx_gain_clip_frac = 1.0

        # PHY parameters
        self.phy = 'ofdm'
        self.min_packet_size = 0
        self.num_modulation_threads = 4
        self.num_demodulation_threads = 16
        self.max_channels = 10
        self.tx_upsample = True

        # General liquid modulation options
        self.check = 'crc32'
        self.fec0 = 'rs8'
        self.fec1 = 'none'
        self.ms = 'qpsk'

        # Header liquid modulation options
        self.header_check = 'crc32'
        self.header_fec0 = 'secded7264'
        self.header_fec1 = 'h84'
        self.header_ms = 'bpsk'

        # Broadcast liquid modulation options
        self.broadcast_check = 'crc32'
        self.broadcast_fec0 = 'none'
        self.broadcast_fec1 = 'v27'
        self.broadcast_ms = 'bpsk'

        # Soft decoding options
        self.soft_header = True
        self.soft_payload = False

        # OFDM parameters
        self.M = 48
        self.cp_len = 6
        self.taper_len = 4

        # Channelizer parameters
        self.channelizer = 'freqdomain'
        self.channelizer_enforce_ordering = False

        # Synthesizer parameters
        self.synthesizer = 'timedomain'

        # MAC parameters
        self.slot_size = .035
        self.guard_size = .01
        self.demod_overlap_size = .005
        self.premod_slots = 2.0
        self.aloha_prob = .1
        self.slot_modulate_time = 30e-3
        self.slot_send_time = 10e-3
        self.fdma = False
        self.tx_channel = None
        self.superslots = False
        self.neighbor_discovery_period = 12

        # ARQ options
        self.arq = False
        self.arq_window = 1024
        self.arq_enforce_ordering = False
        self.arq_max_retransmissions = None
        self.arq_ack_delay = 100e-3
        self.arq_retransmission_delay = 500e-3
        self.arq_explicit_nak_win = 10
        self.arq_explicit_nak_win_duration = 0.1
        self.arq_selective_ack = True
        self.arq_selective_ack_feedback_delay = 0.300
        self.arq_mcu = 100
        self.arq_move_along = True
        self.arq_broadcast_gain_db = 0.0
        self.arq_ack_gain_db = 0.0

        # AMC options
        self.amc = False
        self.amc_table = None

        self.amc_short_per_nslots = 2
        self.amc_long_per_nslots = 8
        self.amc_mcsidx_init = 0
        self.amc_mcsidx_up_per_threshold = 0.04
        self.amc_mcsidx_down_per_threshold = 0.10
        self.amc_mcsidx_alpha = 0.5
        self.amc_mcsidx_prob_floor = 0.1

        # Snapshot options
        self.snapshot_period = 0
        self.snapshot_duration = 0.5

        # Network options
        self.mtu = 1500
        self.queue = 'fifo'

        # Neighbor discover options
        # discovery_hello_interval is how often we send HELLO packets during
        # discovery, and standard_hello_interval is how often we send HELLO
        # packets during the rest of the run
        self.discovery_hello_interval = 1.0
        self.standard_hello_interval = 60.0
        self.timestamp_delay = 100e-3

        # Clock synchronization
        self.clock_sync_interval = 10.0
        self.clock_gpsdo = False

        # Measurement options
        self.measurement_period = 1.0

        # Scoring options
        self.max_performance_age = 8.0
        """Performance reports may be from a measurement period no older than
        this many seconds"""

        self.stats_ignore_window = self.measurement_period + 0.5
        """Ignore flow statistics during this (most recent) time window"""

        # Internal agent options
        self.status_update_period = 5

        # Collaboration server options
        self.force_gateway = False
        self.collab_server_ip = None
        self.collab_server_port = 5556
        self.collab_client_port = 5557
        self.collab_peer_port = 5558

    def __str__(self):
        return pformat(self.__dict__)

    @property
    def logdir(self):
        if self.logdir_:
            return self.logdir_

        if self.log_directory == None:
            return None

        logdir = os.path.join(self.log_directory, 'node-{:03d}'.format(self.node_id))
        if not os.path.exists(logdir):
            os.makedirs(logdir)

        self.logdir_ = os.path.abspath(logdir)
        return self.logdir_

    def get_log_level(self):
        return logging.getLevelName(self.loglevel)

    def set_log_level(self, level):
        self.loglevel = getattr(logging, level)

    log_level = property(get_log_level, set_log_level)

    def mergeConfig(self, dict):
        for key in dict:
            setattr(self, key, dict[key])

    def loadConfig(self, path):
        """
        Load configuration parameters from a radio.conf file in libconf format.
        """
        try:
            with io.open(path) as f:
                self.mergeConfig(libconf.load(f))
            logger.info("Loaded radio config '%s'", path)
        except:
            logger.exception("Cannot load radio config '%s'", path)

    def loadColosseumIni(self, path):
        """
        Load configuration parameters from a colosseum_config.ini file.
        """
        try:
            with open(path, 'r') as f:
                logging.debug("Read colosseum.ini '%s':\n%s", path, f.read())
        except:
            logging.exception("Cannot open colosseum_config.ini '%s'", path)

        try:
            config = configparser.ConfigParser()
            config.read(path)

            if 'COLLABORATION' in config:
                for key in config['COLLABORATION']:
                    setattr(self, key, config['COLLABORATION'][key])

            if 'RF' in config:
                for key in config['RF']:
                    setattr(self, key, float(config['RF'][key]))

            logger.info("Loaded colosseum_config.ini '%s'", path)
        except:
            logger.exception('Cannot load colosseum_config.ini')

    def addArguments(self, parser, allow_defaults=True):
        """
        Populate an ArgumentParser with arguments corresponding to configuration
        parameters.
        """
        def enumHelp(cls):
            return ', '.join(sorted(cls.__members__.keys()))

        # Debugging
        parser.add_argument('-d', '--debug', action=LogLevelAction, const=logging.DEBUG,
                            dest='loglevel',
                            help='print debugging information')
        parser.add_argument('-v', '--verbose', action=LogLevelAction, const=logging.INFO,
                            dest='loglevel',
                            help='be verbose')
        parser.add_argument('--verbose-packet-trace', action='store_const', const=True,
                            dest='verbose_packet_trace',
                            help='show trace of packets written to network')

        # Node ID
        parser.add_argument('-i', action='store', type=int, dest='node_id',
                            help='set node ID')

        # Load configuration file
        parser.add_argument('--config', action=LoadConfigAction,
                            help='specify configuration file')

        # Log parameters
        parser.add_argument('-l', action='store',
                            dest='log_directory',
                            help='specify directory for log files')
        parser.add_argument('--log-iq', action=ExtendAction, const=['log_slots', 'log_recv_data', 'log_sent_data'],
                            dest='log_sources',
                            help='log IQ data')
        parser.add_argument('--log-invalid-headers', action='store_const', const=True,
                            dest='log_invalid_headers',
                            help='log packets with invalid headers')
        parser.add_argument('--log-snapshots', action='store_const', const=True,
                            dest='log_snapshots',
                            help='log snapshots')
        parser.add_argument('--log-protobuf', action='store_const', const=True,
                            dest='log_protobuf',
                            help='log protobuf')

        # USRP settings
        parser.add_argument('--addr', action='store',
                            dest='addr',
                            help='specify device address')
        parser.add_argument('--rx-antenna', action='store',
                            dest='rx_antenna',
                            help='set RX antenna')
        parser.add_argument('--tx-antenna', action='store',
                            dest='tx_antenna',
                            help='set TX antenna')

        # Frequency and bandwidth
        parser.add_argument('-f', '--frequency', action='store', type=float,
                            dest='frequency',
                            help='set center frequency (Hz)')
        parser.add_argument('-b', '--bandwidth', action='store', type=float,
                            dest='bandwidth',
                            help='set bandwidth (Hz)')
        parser.add_argument('--max-bandwidth', action='store', type=float,
                            dest='max_bandwidth',
                            help='set maximum bandwidth (Hz)')
        parser.add_argument('--rx-oversample', action='store', type=float,
                            dest='rx_oversample_factor',
                            help='set RX oversample factor')
        parser.add_argument('--tx-oversample', action='store', type=float,
                            dest='tx_oversample_factor',
                            help='set TX oversample factor')
        parser.add_argument('--channel-bandwidth', action='store', type=float,
                            dest='channel_bandwidth',
                            help='set channel bandwidth (Hz)')
        parser.add_argument('--channel-guard-bandwidth', action='store', type=float,
                            dest='channel_guard_bandwidth',
                            help='set channel guard bandwidth (Hz)')
        parser.add_argument('--edge-guard-bandwidth', action='store', type=float,
                            dest='edge_guard_bandwidth',
                            help='set spectrum edge guard bandwidth (Hz)')
        parser.add_argument('--maximize-channel-guard-bandwidth', action='store_const', const=True,
                            dest='maximize_channel_guard_bandwidth',
                            help='maximize channel guard bandwidth')
        parser.add_argument('--no-maximize-channel-guard-bandwidth', action='store_const', const=False,
                            dest='maximize_channel_guard_bandwidth',
                            help='don\'t maximize channel guard bandwidth')

        # Gain-related options
        parser.add_argument('-G', '--tx-gain', action='store', type=float,
                            dest='tx_gain',
                            help='set UHD TX gain (dB)')
        parser.add_argument('-R', '--rx-gain', action='store', type=float,
                            dest='rx_gain',
                            help='set UHD RX gain (dB)')
        parser.add_argument('-g', '--soft-tx-gain', action='store', type=float,
                            dest='soft_tx_gain',
                            help='set soft TX gain (dB)')
        parser.add_argument('--auto-soft-tx-gain', action='store', type=int,
                            dest='auto_soft_tx_gain',
                            help='automatically choose soft TX gain to attain 0dBFS')
        parser.add_argument('--auto-soft-tx-gain-clip-frac', action='store', type=float,
                            dest='auto_soft_tx_gain_clip_frac',
                            help='clip fraction for automatic soft TX gain')

        # PHY parameters
        parser.add_argument('--phy', action='store',
                            choices=['flexframe', 'newflexframe', 'ofdm'],
                            dest='phy',
                            help='set PHY')
        parser.add_argument('--min-packet-size', action='store', type=int,
                            dest='min_packet_size',
                            help='set minimum packet size (in bytes)')
        parser.add_argument('--max-channels', action='store', type=int,
                            dest='max_channels',
                            help='set maximum number of channels')
        parser.add_argument('--tx-upsample', action='store_const', const=True,
                            dest='tx_upsample',
                            help='use software upsampler on TX')
        parser.add_argument('--no-tx-upsample', action='store_const', const=False,
                            dest='tx_upsample',
                            help='use USRP\'s hardware upsampler on TX')

        # General liquid modulation options
        parser.add_argument('-r', '--check',
                            action='store', type=dragonradio.CRCScheme,
                            dest='check',
                            help='set data validity check: ' + enumHelp(dragonradio.CRCScheme))
        parser.add_argument('-c', '--fec0',
                            action='store', type=dragonradio.FECScheme,
                            dest='fec0',
                            help='set inner FEC: ' + enumHelp(dragonradio.FECScheme))
        parser.add_argument('-k', '--fec1',
                            action='store', type=dragonradio.FECScheme,
                            dest='fec1',
                            help='set outer FEC: ' + enumHelp(dragonradio.FECScheme))
        parser.add_argument('-m', '--mod',
                            action='store', type=dragonradio.ModulationScheme,
                            dest='ms',
                            help='set modulation scheme: ' + enumHelp(dragonradio.ModulationScheme))

        # Soft decoding options
        parser.add_argument('--soft-header', action='store_const', const=True,
                            dest='soft_header',
                            help='use soft decoding for header')
        parser.add_argument('--soft-payload', action='store_const', const=True,
                            dest='soft_payload',
                            help='use soft decoding for payload')

        # OFDM-specific options
        parser.add_argument('-M', '--subcarriers', action='store', type=int,
                            dest='M',
                            help='set number of OFDM subcarriers')
        parser.add_argument('-C', '--cp', action='store', type=int,
                            dest='cp_len',
                            help='set OFDM cyclic prefix length')
        parser.add_argument('-T', '--taper', action='store', type=int,
                            dest='taper_len',
                            help='set OFDM taper length')

        # Channelizer parameters
        parser.add_argument('--channelizer', action='store',
                            choices=['freqdomain', 'timedomain', 'overlap'],
                            dest='channelizer',
                            help='set channelization algorithm')
        parser.add_argument('--channelizer-enforce-ordering', action='store_const', const=True,
                            dest='channelizer_enforce_ordering',
                            help='enforce packet order when demodulating in channelizer')

        # Synthesizer parameters
        parser.add_argument('--synthesizer', action='store',
                            choices=['timedomain'],
                            dest='synthesizer',
                            help='set synthesizer algorithm')

        # MAC parameters
        parser.add_argument('--slot-size', action='store', type=float,
                            dest='slot_size',
                            help='set MAC slot size (sec)')
        parser.add_argument('--guard-size', action='store', type=float,
                            dest='guard_size',
                            help='set MAC guard interval (sec)')
        parser.add_argument('--demod-overlap-size', action='store', type=float,
                            dest='demod_overlap_size',
                            help='set demodulation overlap interval (sec)')
        parser.add_argument('--premod-slots', action='store', type=float,
                            dest='premod_slots',
                            help='set number of slots to pre-modulate')
        parser.add_argument('--fdma', action='store_const', const=True,
                            dest='fdma',
                            help='use FDMA instead of TDMA')
        parser.add_argument('--tx-channel', action='store', type=int,
                            dest='tx_channel',
                            help='set explicit channel to use with FDMA')
        parser.add_argument('--superslots', action='store_const', const=True,
                            dest='superslots',
                            help='use TDMA superslots')

        # ARQ options
        parser.add_argument('--arq', action='store_const', const=True,
                            dest='arq',
                            help='enable ARQ')
        parser.add_argument('--no-arq', action='store_const', const=False,
                            dest='arq',
                            help='disable ARQ')
        parser.add_argument('--arq-window', action='store', type=int,
                            dest='arq_window',
                            help='set ARQ window size')
        parser.add_argument('--arq-enforce-ordering', action='store_const', const=True,
                            dest='arq_enforce_ordering',
                            help='enforce packet order when performing ARQ')
        parser.add_argument('--max-retransmissions', action='store', type=int,
                            dest='arq_max_retransmissions',
                            help='set maximum number of retransmission attempts')
        parser.add_argument('--explicit-nak-window', action='store', type=int,
                            dest='arq_explicit_nak_win',
                            help='set explicit NAK window size')
        parser.add_argument('--explicit-nak-window-duration', action='store', type=float,
                            dest='arq_explicit_nak_win_duration',
                            help='set explicit NAK window duration (sec)')
        parser.add_argument('--selective-ack', action='store_const', const=True,
                            dest='arq_selective_ack',
                            help='send selective ACK\'s')
        parser.add_argument('--no-selective-ack', action='store_const', const=False,
                            dest='arq_selective_ack',
                            help='do not send selective ACK\'s')

        # AMC options
        parser.add_argument('--amc', action='store_const', const=True,
                            dest='amc',
                            help='enable AMC')
        parser.add_argument('--no-amc', action='store_const', const=False,
                            dest='amc',
                            help='disable AMC')
        parser.add_argument('--short-per-nslots', action='store', type=int,
                            dest='amc_short_per_nslots',
                            help='set number of TX slots worth of packets we use to calculate short-term PER')
        parser.add_argument('--long-per-nslots', action='store', type=int,
                            dest='amc_long_per_nslots',
                            help='set number of TX slots worth of packets we use to calculate long-term PER')
        parser.add_argument('--mcsidx-up-per-threshold', action='store', type=float,
                            dest='amc_mcsidx_up_per_threshold',
                            help='set PER threshold for increasing modulation level')
        parser.add_argument('--mcsidx-down-per-threshold', action='store', type=float,
                            dest='amc_mcsidx_down_per_threshold',
                            help='set PER threshold for decreasing modulation level')
        parser.add_argument('--mcsidx-alpha', action='store', type=float,
                            dest='amc_mcsidx_alpha',
                            help='set decay factor for learning MCS transition probabilities')
        parser.add_argument('--mcsidx-prob-floor', action='store', type=float,
                            dest='amc_mcsidx_prob_floor',
                            help='set minimum MCS transition probability')

        # Snapshot options
        parser.add_argument('--snapshot-period', action='store', type=float,
                dest='snapshot_period',
                help='set snapshot period (sec)')
        parser.add_argument('--snapshot-duration', action='store', type=float,
                dest='snapshot_duration',
                help='set snapshot duration (sec)')

        # Network options
        parser.add_argument('--mtu', action='store', type=int,
                            dest='mtu',
                            help='set Maximum Transmission Unit (bytes)')
        parser.add_argument('--queue', action='store',
                            choices=['fifo', 'lifo', 'smartlifo'],
                            dest='queue',
                            help='set network queuing algorithm')
        parser.add_argument('--fifo', action='store_const', const='fifo',
                            dest='queue',
                            help='use FIFO network queue algorithm')
        parser.add_argument('--lifo', action='store_const', const='lifo',
                            dest='queue',
                            help='use LIFO network queue algorithm')

        # Collaboration server options
        parser.add_argument('--force-gateway', action='store_const', const=True,
                            dest='force_gateway',
                            help='force this node to act as a gateway')
        parser.add_argument('--collab-server-ip', action='store', type=str,
                            dest='collab_server_ip',
                            help='set collaboration server IP address')

        # Set defaults
        defaults = {}

        for act in parser._actions:
            dest = act.dest
            if hasattr(self, dest):
                defaults[dest] = getattr(self, dest)

        parser.set_defaults(**defaults)

class Radio(object):
    def __init__(self, config):
        self.config = config
        """Config object for radio"""

        self.node_id = config.node_id
        """This node's ID"""

        self.logger = None
        """Our DragonRadio logger"""

        self.lock = asyncio.Lock()
        """Lock protecting radio configuration"""

        logger.info('Radio version: %s', dragonradio.version)
        logger.info('Radio configuration:\n%s', str(config))

        # Copy configuration settings to the C++ RadioConfig object
        for attr in ['verbose', 'debug',
                     'amc_short_per_nslots', 'amc_long_per_nslots',
                     'timestamp_delay',
                     'mtu',
                     'arq_ack_delay', 'arq_retransmission_delay',
                     'slot_modulate_time', 'slot_send_time',
                     'verbose_packet_trace']:
            if hasattr(config, attr):
                setattr(dragonradio.rc, attr, getattr(config, attr))

        # Add global work queue workers
        dragonradio.work_queue.addThreads(1)

        # Set default TX channel
        self.tx_channel = Channel(0.0, 0.0)

        # Create the USRP
        self.usrp = dragonradio.USRP(config.addr,
                                     self.frequency,
                                     config.tx_antenna,
                                     config.rx_antenna,
                                     config.tx_gain,
                                     config.rx_gain)

        # Create the logger *after* we create the USRP so that we have a global
        # clock
        logdir = config.logdir
        if logdir:
            path = self.getRadioLogPath()

            self.logger = dragonradio.Logger(path)
            self.logger.setAttribute('version', dragonradio.version)
            self.logger.setAttribute('node_id', self.node_id)
            self.logger.setAttribute('soft_tx_gain', config.soft_tx_gain)
            self.logger.setAttribute('tx_gain', config.tx_gain)
            self.logger.setAttribute('rx_gain', config.rx_gain)
            self.logger.setAttribute('M', config.M)
            self.logger.setAttribute('cp_len', config.cp_len)
            self.logger.setAttribute('taper_len', config.taper_len)

            if hasattr(config, 'log_sources'):
                for source in config.log_sources:
                    setattr(self.logger, source, True)

            dragonradio.Logger.singleton = self.logger

        #
        # Configure snapshots
        #
        if config.snapshot_period != 0:
            self.snapshot_collector = dragonradio.SnapshotCollector()
        else:
            self.snapshot_collector = None

        #
        # Configure the PHY
        #
        header_mcs = MCS(config.header_check,
                         config.header_fec0,
                         config.header_fec1,
                         config.header_ms)

        if config.phy == 'flexframe':
            self.phy = dragonradio.FlexFrame(self.snapshot_collector,
                                             self.node_id,
                                             header_mcs,
                                             config.soft_header,
                                             config.soft_payload,
                                             config.min_packet_size)
        elif config.phy == 'newflexframe':
            self.phy = dragonradio.NewFlexFrame(self.snapshot_collector,
                                                self.node_id,
                                                header_mcs,
                                                config.soft_header,
                                                config.soft_payload,
                                                config.min_packet_size)
        elif config.phy == 'ofdm':
            self.phy = dragonradio.OFDM(self.snapshot_collector,
                                        self.node_id,
                                        header_mcs,
                                        config.soft_header,
                                        config.soft_payload,
                                        config.min_packet_size,
                                        config.M,
                                        config.cp_len,
                                        config.taper_len)
        else:
            fail('Bad PHY: {}'.format(config.phy))

        #
        # Configure the MAC
        #
        # We start out without a MAC
        #
        self.mac = None
        """The radio's MAC"""

        #
        # Create tun/tap interface and net neighborhood
        #
        self.tuntap = dragonradio.TunTap('tap0', False, self.config.mtu, self.node_id)

        self.net = dragonradio.Net(self.tuntap, self.node_id)

        #
        # Set up TX params for network
        #
        if config.amc and config.amc_table:
            tx_params = []
            for (crc, fec0, fec1, ms) in config.amc_table:
                tx_params.append(TXParams(MCS(crc, fec0, fec1, ms)))
        else:
            tx_params = [TXParams(MCS(config.check, config.fec0, config.fec1, config.ms))]

        for p in tx_params:
            self.configTXParamsSoftGain(p)

        self.net.tx_params = TXParamsVector(tx_params)

        #
        # Configure TX/RX rates and channels
        #
        self.configRatesAndChannels()

        #
        # Configure the channelization
        #
        if config.channelizer == 'overlap':
            self.channelizer = dragonradio.OverlapTDChannelizer(self.net,
                                                                self.phy,
                                                                self.usrp.rx_rate,
                                                                self.channels,
                                                                config.num_demodulation_threads)

            self.channelizer.enforce_ordering = config.channelizer_enforce_ordering
        elif config.channelizer == 'timedomain':
            self.channelizer = dragonradio.TDChannelizer(self.net,
                                                         self.phy,
                                                         self.usrp.rx_rate,
                                                         self.channels,
                                                         config.num_demodulation_threads)
        else:
            self.channelizer = dragonradio.FDChannelizer(self.net,
                                                         self.phy,
                                                         self.usrp.rx_rate,
                                                         self.channels,
                                                         config.num_demodulation_threads)

        self.synthesizer = dragonradio.TDSynthesizer(self.net,
                                                     self.phy,
                                                     self.usrp.tx_rate,
                                                     self.tx_channel,
                                                     config.num_modulation_threads)

        # Configure channelization taps
        self.configureChannelizationTaps()

        #
        # Configure the controller
        #
        if config.arq:
            self.controller = dragonradio.SmartController(self.net,
                                                          self.phy,
                                                          config.arq_window,
                                                          config.arq_window,
                                                          config.amc_mcsidx_init,
                                                          config.amc_mcsidx_up_per_threshold,
                                                          config.amc_mcsidx_down_per_threshold,
                                                          config.amc_mcsidx_alpha,
                                                          config.amc_mcsidx_prob_floor)

            self.configSmartControllerSlotSize()

            self.controller.max_retransmissions = config.arq_max_retransmissions
            self.controller.enforce_ordering = config.arq_enforce_ordering
            self.controller.mcu = config.arq_mcu
            self.controller.move_along = config.arq_move_along

            self.controller.broadcast_gain.dB = config.arq_broadcast_gain_db
            self.controller.ack_gain.dB = config.arq_ack_gain_db

            #
            # Configure broadcast MCS
            #
            mcs = self.controller.broadcast_tx_params.mcs
            mcs.check = config.broadcast_check
            mcs.fec0 = config.broadcast_fec0
            mcs.fec1 = config.broadcast_fec1
            mcs.ms = config.broadcast_ms

            self.configTXParamsSoftGain(self.controller.broadcast_tx_params)

            #
            # Configure NAK's
            #
            self.controller.explicit_nak_window = config.arq_explicit_nak_win
            self.controller.explicit_nak_window_duration = config.arq_explicit_nak_win_duration
            self.controller.selective_ack = config.arq_selective_ack
            self.controller.selective_ack_feedback_delay = config.arq_selective_ack_feedback_delay
        else:
            self.controller = dragonradio.DummyController(self.net)

        #
        # Create flow performance measurement component
        #
        self.flowperf = dragonradio.FlowPerformance(config.measurement_period)

        #
        # Configure packet path from demodulator to tun/tap
        # Right now, the path is direct:
        #   demodulator -> controller -> FlowPerformance.radio -> tun/tap
        #
        self.channelizer.source >> self.controller.radio_in

        self.controller.radio_out >> self.flowperf.radio_in

        self.flowperf.radio_out >> self.tuntap.sink

        #
        # Configure packet path from tun/tap to the modulator
        # The path is:
        #   tun/tap -> NetFilter -> FlowPerformance.net -> NetFirewall -> NetQueue -> controller -> modulator
        #
        self.netfilter = dragonradio.NetFilter(self.net)
        self.netfirewall = dragonradio.NetFirewall()

        if config.queue == 'fifo':
            self.netq = dragonradio.NetFIFO()
        elif config.queue == 'lifo':
            self.netq = dragonradio.NetLIFO()
        else:
            self.netq = dragonradio.NetSmartLIFO()

        self.tuntap.source >> self.netfilter.input

        self.netfilter.output >> self.flowperf.net_in

        self.flowperf.net_out >> self.netfirewall.input

        self.netfirewall.output >> self.netq.push

        self.netq.pop >> self.controller.net_in

        self.controller.net_out >> self.synthesizer.sink

        #
        # If we are using a SmartController, tell it about the network queue is
        # so that it can add high-priority packets.
        #
        if config.arq:
            self.controller.net_queue = self.netq

    def __del__(self):
        if self.logger:
            self.logger.close()

    def configTXParamsSoftGain(self, tx_params):
        config = self.config

        tx_params.soft_tx_gain_0dBFS = config.soft_tx_gain
        if config.auto_soft_tx_gain != None:
            tx_params.recalc0dBFSEstimate(config.auto_soft_tx_gain)
            tx_params.auto_soft_tx_gain_clip_frac = config.auto_soft_tx_gain_clip_frac

    def setTXParams(self, crc, fec0, fec1, ms, g, clip=0.999):
        config = self.config

        tx_params = TXParams(MCS(crc, fec0, fec1, ms))

        if g == 'auto':
            tx_params.soft_tx_gain_0dBFS = config.soft_tx_gain
            tx_params.recalc0dBFSEstimate(100)
            tx_params.auto_soft_tx_gain_clip_frac = clip
        else:
            tx_params.soft_tx_gain_0dBFS = g

        self.net.tx_params = TXParamsVector([tx_params])

    def configRatesAndChannels(self):
        """
        Configure USRP and PHY rates as well as channels.

        This will set our channels member variable as well as set rates on the
        USRP and PHY based on the current configuration's bandwidth and center
        frequency. It *will not* update the modulator/demodulator, MAC, or
        controller.
        """
        config = self.config

        #
        # Configure bandwidth, channels, and sampling rate. We MUST do this
        # before creating the modulator and demodulator so we know at what rate
        # we must resample.
        #
        bandwidth = self.bandwidth
        cbw = self.channel_bandwidth
        cgbw = self.channel_guard_bandwidth
        egbw = self.edge_guard_bandwidth

        # We space channels so that there is edge_guard_bandwidth on each end
        # and at least channel_guard_bandwidth between channels. For n channels,
        # we therefore have n+1 guards, so:
        #    n*cbw + 2*egbw + (n-1)*cgbw <= bandwidth
        # => n <= 1 + (bandwidth - cbw - 2*egbw) / (cbw + cgbw)
        n = 1 + int((bandwidth-cbw-2*egbw)/(cbw+cgbw))

        if n < 1:
            print("No channels (bandwidth={:g}; channel bandwidth={:g}; channel guard={:g}; edge guard={:g})".format(bandwidth, cbw, cgbw, egbw),
                  file=sys.stderr)
            sys.exit(1)

        # We use the leftover space to space channels as far apart as possible.
        # The variable gbw is the actualy guard bandwidth
        #    (n-1)*cgbw + 2*egbw + n*cbw = bandwidth
        # => cgbw = (bandwidth - 2*egbw - n*cbw)/(n-1)
        if config.maximize_channel_guard_bandwidth and n > 1:
            cgbw = (bandwidth-2*egbw-n*cbw)/(n-1)

        channels = [Channel(egbw + i*(cbw + cgbw) + cbw/2. - bandwidth/2., cbw) for i in range(0,n)]

        self.channels = Channels(channels[:config.max_channels])

        logging.debug("Channels: %s (bandwidth=%g; rx_oversample=%d; tx_oversample=%d; channel bandwidth=%g; channel guard=%g; edge guard=%g)",
            list(self.channels), bandwidth, config.rx_oversample_factor, config.tx_oversample_factor, cbw, cgbw, egbw)

        #
        # Set RX and TX rates
        #
        rx_rate_oversample = config.rx_oversample_factor*self.phy.min_rx_rate_oversample
        tx_rate_oversample = config.tx_oversample_factor*self.phy.min_tx_rate_oversample

        self.usrp.rx_rate = bandwidth*rx_rate_oversample
        if config.tx_upsample:
            self.usrp.tx_rate = bandwidth*tx_rate_oversample
        else:
            self.usrp.tx_rate = cbw*tx_rate_oversample

        rx_rate = self.usrp.rx_rate
        tx_rate = self.usrp.tx_rate

        self.phy.rx_rate = rx_rate
        self.phy.tx_rate = tx_rate

    def configSmartControllerSlotSize(self):
        """
        Configure the SmartController's slot size
        """
        config = self.config

        bandwidth = self.bandwidth
        cbw = self.channel_bandwidth
        cgbw = self.channel_guard_bandwidth
        egbw = self.edge_guard_bandwidth

        if config.arq:
            if config.tx_upsample:
                slot_bw = bandwidth
            else:
                slot_bw = cbw

            self.controller.slot_size = int(slot_bw*(self.config.slot_size - self.config.guard_size))

    def reconfigureBandwidthAndFrequency(self, bandwidth, frequency):
        """
        Reconfigure the radio for the given bandwidth and frequency
        """
        config = self.config

        if bandwidth == config.bandwidth and frequency == config.frequency:
            return

        config.bandwidth = bandwidth
        config.frequency = frequency

        logger.info("Reconfiguring radio: bandwidth=%f, frequency=%f", bandwidth, frequency)

        self.usrp.rx_frequency = self.frequency
        self.usrp.tx_frequency = self.frequency

        self.configRatesAndChannels()

        # Set channelization rates and channels
        self.channelizer.rx_rate = self.usrp.rx_rate
        self.synthesizer.tx_rate = self.usrp.tx_rate

        self.channelizer.channels = self.channels

        # Reconfigure taps
        self.configureChannelizationTaps()

        # Reconfigure the MAC
        if self.mac is not None:
            self.mac.reconfigure()

        if config.arq:
            self.controller.resetMCSTransitionProbabilities()
            self.configSmartControllerSlotSize()

    def configureChannelizationTaps(self):
        """Configure filter taps for channelizer and synthesizer"""
        config = self.config

        # Determine channel bandwidth, passband, and stopband
        cbw = self.channel_bandwidth
        wp = cbw-100e3
        ws = cbw+100e3

        # Re-configure channelizer taps
        rate = Fraction(cbw/self.usrp.rx_rate).limit_denominator(200)
        if rate != 1:
            self.channelizer.taps = lowpass(wp, ws, rate.numerator*self.usrp.rx_rate)

        # Re-configure synthesizer taps
        if config.tx_upsample:
            rate = Fraction(self.usrp.tx_rate/cbw).limit_denominator(200)
            if rate != 1:
                self.synthesizer.taps = lowpass(wp, ws, rate.numerator*cbw)

    def deleteMAC(self):
        """Delete the current MAC"""
        if self.config.arq:
            self.controller.mac = None

        self.mac.stop()
        self.mac = None

    def configureALOHA(self):
        config = self.config

        self.mac = dragonradio.SlottedALOHA(self.usrp,
                                            self.phy,
                                            self.controller,
                                            self.snapshot_collector,
                                            self.channelizer,
                                            self.synthesizer,
                                            config.slot_size,
                                            config.guard_size,
                                            config.aloha_prob)

        if isinstance(self.channelizer, dragonradio.OverlapTDChannelizer):
            self.channelizer.prev_demod = 0.5*config.slot_size
            self.channelizer.cur_demod = config.slot_size

        self.finishConfiguringMAC()

    def configureTDMA(self, nslots):
        """Configures a TDMA MAC with 'nslots' slots.

        This function sets up a TDMA MAC for a schedule with `nslots` slots, but
        it does not claim any of the slots. After calling this function, the
        node *will not transmit* until it is given a slot.

        Args:
            nslots: The number of slots in the schedule
        """
        config = self.config

        self.mac = dragonradio.TDMA(self.usrp,
                                    self.phy,
                                    self.controller,
                                    self.snapshot_collector,
                                    self.channelizer,
                                    self.synthesizer,
                                    config.slot_size,
                                    config.guard_size,
                                    nslots)

        self.mac.superslots = self.config.superslots

        # When we using superslots, we need to demodulate half the previous slot
        # becasue a sender could start transmitting a packet halfway into a slot
        # + epsilon.
        if isinstance(self.channelizer, dragonradio.OverlapTDChannelizer):
            if self.config.superslots:
                self.channelizer.prev_demod = 0.5*config.slot_size
                self.channelizer.cur_demod = config.slot_size
            else:
                self.channelizer.prev_demod = config.demod_overlap_size
                self.channelizer.cur_demod = \
                    config.slot_size - config.guard_size + config.demod_overlap_size

        self.finishConfiguringMAC()

    def finishConfiguringMAC(self):
        self.mac.premod_slots = self.config.premod_slots

        if self.config.arq:
            self.controller.mac = self.mac

    def setTXChannel(self, channel):
        config = self.config

        if config.tx_upsample:
            self.synthesizer.tx_channel = channel
        else:
            fc = channel.fc
            logging.info("Setting TX frequency offset to %g", fc)

            self.usrp.tx_frequency = self.frequency + fc

            self.synthesizer.tx_channel = Channel(0.0, self.channel_bandwidth)

        # Allow the MAC to figure out the TX offset so snapshot self
        # tranmissions are correctly logged
        if self.mac is not None:
            self.mac.reconfigure()

        self.tx_channel = channel

    def mkGreedyMACSchedule(self, nslots, nodes, k):
        """Create a greedy schedule that gives each node its own channel.

        Args:
            nslots: The number of time slots
            nodes: The nodes
            k: The desired channel separation

        Returns:
            A schedule consisting of a nchannels X nslots array of node IDs.
        """
        nchannels = len(self.channels)

        sched = np.zeros((nchannels, nslots), dtype=int)

        # Each node gets its own channel. Any leftover nodes don't get anything
        nodes = nodes[:nchannels]

        i = 0
        while len(nodes) != 0:
            if np.all(sched[i] == 0):
                sched[i] = nodes[0]
                nodes = nodes[1:]
                i += k
            else:
                i += 1

            if i >= nchannels:
                i = 0

        return sched

    def installMACSchedule(self, sched):
        """Install a MAC schedule.

        Args:
            sched: The schedule, which is a nchannels X nslots array of node
                IDs.
        """
        logging.debug('Installing MAC schedule:\n%s', sched)

        # Determine which nodes are allowed to transmit
        nodes_with_slot = set(sched.flatten())
        if 0 in nodes_with_slot:
            nodes_with_slot.remove(0)

        for (node_id, node) in self.net.items():
            node.can_transmit = node_id in nodes_with_slot

        # Install the MAC schedule.
        (nchannels, nslots) = sched.shape

        # Look for a channel where we have a slot. If there is more than one
        # such channel, use the first, because we don't yet support multiple
        # channels.
        for chan in range(0, nchannels):
            slots = (sched[chan] == self.node_id)
            if np.any(slots):
                self.mac.slots = slots
                self.setTXChannel(self.channels[chan])
                return

        logging.error('No MAC schedule entry for radio %d', self.node_id)

    def configureSimpleMACSchedule(self):
        """
        Set a simple, static TDMA/FDMA schedule based on configuration
        parameters and the given set of nodes.
        """
        config = self.config

        nodes = list(self.net)
        nodes.sort()

        if config.fdma:
            self.configureTDMA(1)

            if config.tx_channel != None:
                self.mac.slots[0] = True
                self.setTXChannel(self.channels[config.tx_channel])
            else:
                nchannels = len(self.channels)
                sched = fullChannelMACSchedule(nchannels, 1, nodes, 3)
                self.installMACSchedule(sched)
        else:
            idx = nodes.index(self.node_id)

            self.configureTDMA(len(self.net))
            self.mac.slots[idx] = True

            self.setTXChannel(self.channels[0])

    def synchronizeClock(self):
        """Use timestamps to syncrhonize our clock with the time master (the gateway)"""
        config = self.config

        if self.net.time_master is None:
            return

        t0 = dragonradio.clock.t0

        # Perform linear regression on all timestamps
        echoed = [((us-t0).secs, (them-t0).secs) for (us, them) in self.controller.echoed_timestamps]
        logging.debug("TIMESYNC: echoed timestamps: %s", echoed)
        if len(echoed) == 0:
            return

        master = [((us-t0).secs, (them-t0).secs) for (them, us) in self.net[self.net.time_master].timestamps]
        logging.debug("TIMESYNC: time master's timestamps: %s", master)
        if len(master) == 0:
            return

        if len(echoed) > 1 and len(master) > 1:
            # If we have a GPSDO, then assume skew is zero
            if config.clock_gpsdo:
                (sigma, delta, epsilon) = self.timetampRegressionNoSkew(echoed, master)
            else:
                (sigma, delta, epsilon) = self.timetampRegression(echoed, master)

            logging.debug("TIMESYNC: regression parameters: sigma=%f; delta=%f; epsilon=%f", sigma, delta, epsilon)

            if math.isfinite(delta) and math.isfinite(sigma):
                dragonradio.clock.offset = dragonradio.MonoTimePoint(delta)
                dragonradio.clock.skew = sigma

    def timetampRegression(self, echoed, master):
        """Perform a linear regression on timestamps to determine clock skew and delta"""
        xs = [x for (x, _) in echoed]
        ys = [y for (_, y) in echoed]

        ss = [s for (_, s) in master]
        ts = [t for (t, _) in master]

        xbar = np.mean(xs)
        ybar = np.mean(ys)

        sbar = np.mean(ss)
        tbar = np.mean(ts)

        sigma = (sum([(x - xbar)*(y - ybar) for (x, y) in echoed]) + sum([(s - sbar)*(t - tbar) for (t, s) in master])) / (sum([(x-xbar)**2.0 for x in xs]) + sum([(s-sbar)**2.0 for s in ss]))

        delta = (tbar - sigma*sbar + sigma*ybar - sigma**2.0*xbar)/(1.0 + sigma)

        epsilon = (ybar - sigma*xbar - tbar + sigma*sbar)/(1.0 + sigma)

        return (sigma, delta, epsilon)

    def timetampRegressionNoSkew(self, echoed, master):
        """Perform a linear regression on timestamps to determine clock delta (assuming no skew)"""
        xs = [x for (x, _) in echoed]
        ys = [y for (_, y) in echoed]

        ss = [s for (_, s) in master]
        ts = [t for (t, _) in master]

        xbar = np.mean(xs)
        ybar = np.mean(ys)

        sbar = np.mean(ss)
        tbar = np.mean(ts)

        delta = (ybar - xbar + tbar - sbar)/2.0

        epsilon = (ybar - xbar - tbar + sbar)/2.0

        return (1.0, delta, epsilon)

    def getRadioLogPath(self):
        """
        Determine where the HDF5 log file created by the low-level radio will
        live.
        """
        path = os.path.join(self.config.logdir, 'radio.h5')
        if not os.path.exists(path):
            return path

        # If the radio log exists, create a new one.
        i = 1
        while True:
            path = os.path.join(self.config.logdir, 'radio-{:02d}.h5'.format(i))
            if not os.path.exists(path):
                return path
            i += 1

    async def snapshotLogger(self):
        if not self.logger:
            return

        config = self.config
        collector = self.snapshot_collector

        # Sleep a random amount to de-synchronize with other radios collecting
        # snapshots.
        await asyncio.sleep(random.uniform(0, config.snapshot_duration))

        while True:
            # Collecting snapshot for config.snapshot_duration
            collector.start()
            await asyncio.sleep(config.snapshot_duration)

            # Stop collecting slots
            collector.stop()

            # Wait 200ms for remaining packets in snapshot to be demodulated and
            # get the snapshot
            await asyncio.sleep(0.2)
            snapshot = collector.finish()

            # Log the snapshot
            slots = snapshot.slots
            if len(slots) != 0:
                t = slots[0].timestamp
                fc = slots[0].fc
                fs = slots[0].fs

                if all([slot.fc == fc for slot in slots]) and all([slot.fs == fs for slot in slots]):
                    # Get concatenated IQ buffer for all slots
                    iqbuf = dragonradio.IQBuf(np.concatenate([slot.data for slot in slots]))
                    iqbuf.timestamp = t
                    iqbuf.fc = fc
                    iqbuf.fs = fs

                    self.logger.logSnapshot(iqbuf)
                    for e in snapshot.selftx:
                        self.logger.logSelfTX(snapshot.timestamp.wall_time, e)

            await asyncio.sleep(config.snapshot_period)

    @property
    def frequency(self):
        config = self.config

        # We always use the lower portion of the available bandwidth. This
        # allows us to avoid the incumbent in the DSRC scenario, which always
        # uses the top portion of the available bandwidth when sharing spectrum.
        diff = config.bandwidth - self.bandwidth
        return config.frequency - diff/2.0

    @property
    def bandwidth(self):
        config = self.config
        return min(config.bandwidth, config.max_bandwidth)

    @property
    def channel_bandwidth(self):
        config = self.config
        if config.fdma:
            return config.channel_bandwidth
        else:
            return config.bandwidth

    @property
    def channel_guard_bandwidth(self):
        config = self.config
        return config.channel_guard_bandwidth

    @property
    def edge_guard_bandwidth(self):
        config = self.config
        if config.edge_guard_bandwidth == None:
            return self.channel_guard_bandwidth
        else:
            return config.edge_guard_bandwidth

def fullChannelMACSchedule(nchannels, nslots, nodes, k):
    """Create a greedy schedule that gives each node its own channel.

    Args:
        nslots: The number of time slots
        nodes: The nodes
        k: The desired channel separation

    Returns:
        A schedule consisting of a nchannels X nslots array of node IDs.
    """
    sched = np.zeros((nchannels, nslots), dtype=int)

    # Each node gets its own channel. Any leftover nodes don't get anything
    nodes = nodes[:nchannels]

    i = 0
    while len(nodes) != 0:
        if np.all(sched[i] == 0):
            sched[i] = nodes[0]
            nodes = nodes[1:]
            i += k
        else:
            i += 1

        if i >= nchannels:
            i = 0

    return sched

def fairMACSchedule(nchannels, nslots, nodes, k):
    """Create a schedule that distributes slots evenly amongst nodes.

    Args:
        nslots: The number of time slots
        nodes: The nodes
        k: The desired channel separation

    Returns:
        A schedule consisting of a nchannels X nslots array of node IDs.
    """
    sched = np.zeros((nchannels, nslots), dtype=int)

    # How many slots to fill per assignment
    slotsper = 1

    # Current slot
    slot = 0

    # How many channels we've filled in the current slot
    filled = 0

    # Current channel
    chan = 0

    # Remaining nodes to assign
    rem_nodes = nodes

    while slot < nslots:
        if np.all(sched[chan,slot:slot+slotsper] == 0):
            sched[chan,slot:slot+slotsper] = rem_nodes[0]
            rem_nodes = rem_nodes[1:]
            chan += k
            filled += 1
        else:
            chan += 1

        if chan >= nchannels:
            chan = 0

        if filled == nchannels:
            slot += slotsper
            filled = 0

        if not rem_nodes:
            rem_nodes = nodes

    return sched
