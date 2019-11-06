import argparse
import asyncio
from concurrent.futures import CancelledError
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
from dragonradio import Channel, Channels
from dragonradio.liquid import MCS

import dragon.channels
import dragon.schedule
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
        self.rx_subdev = None
        self.tx_subdev = None
        self.rx_max_samps_factor = 8
        self.tx_max_samps_factor = 8

        # Frequency and bandwidth
        # Default frequency in the Colosseum is 1GHz
        self.frequency = 1e9
        """Radio frequency"""
        self.bandwidth = 5e6
        """Radio bandwidth to use"""
        self.max_bandwidth = 50e6
        """Max bandwidth radio can handle"""
        self.rx_bandwidth = None
        """If set, always receive at this bandwidth. Otherwise, calculate
        receive bandwidth based on RX oversample factor and radio bandwidth."""
        self.tx_bandwidth = None
        """If set, always transmit at this bandwidth. Otherwise, calculate
        transmit bandwidth based on TX oversample factor and channel
        bandwidth."""
        self.rx_oversample_factor = 1.0
        """Oversample factor on RX"""
        self.tx_oversample_factor = 1.0
        """Oversample factor on TX"""
        self.channel_bandwidth = 1e6
        """Default channel bandwidth for FDMA"""

        # TX/RX gain parameters
        self.tx_gain = 25
        self.rx_gain = 25
        self.soft_tx_gain = -8
        self.auto_soft_tx_gain = None
        self.auto_soft_tx_gain_clip_frac = 1.0

        # PHY parameters
        self.phy = 'ofdm'
        self.num_modulation_threads = 1
        self.num_demodulation_threads = 10
        self.max_channels = 10
        self.tx_upsample = True

        # General liquid modulation options
        self.check = 'crc32'
        self.fec0 = 'rs8'
        self.fec1 = 'none'
        self.ms = 'qpsk'

        # Header MCS
        self.header_check = 'crc32'
        self.header_fec0 = 'none'
        self.header_fec1 = 'v29p78'
        self.header_ms = 'bpsk'

        # Soft decoding options
        self.soft_header = True
        self.soft_payload = True

        # OFDM parameters
        self.M = 48
        self.cp_len = 6
        self.taper_len = 4
        self.subcarriers = None

        # Channelizer parameters
        self.channelizer = 'freqdomain'
        self.channelizer_enforce_ordering = False

        # Synthesizer parameters
        self.synthesizer = 'freqdomain'

        # MAC parameters
        self.pin_rx_worker = False
        """Pin RX worker thread to CPU"""
        self.pin_tx_worker = False
        """Pin TX worker thread to CPU"""
        self.slot_size = .035
        """Total slot duration, including guard interval (seconds)"""
        self.guard_size = .01
        """Size of slot guard interval (seconds)"""
        self.demod_overlap_size = .005
        """Size of demodulation overlap if using the overlapping demodulator (seconds)"""
        self.slot_send_lead_time = 5e-3
        """Lead time needed for slot transmission (seconds)"""
        self.aloha_prob = .1
        """Probability of transmission in a given slot for ALOHA"""
        self.fdma = False
        """True if we should use FDMA MAC, False for pure TDMA"""
        self.tx_channel_idx = None
        """TX channel index"""
        self.superslots = False
        """True if slots should be combined into superslots"""
        self.neighbor_discovery_period = 12
        """Neighbor discovery period at radio startup (sec)"""

        # ARQ options
        self.arq = False
        """Should ARQ be enabled?"""
        self.arq_window = 1024
        """ARQ window size"""
        self.arq_enforce_ordering = False
        """Should ARQ enforce packet ordering?"""
        self.arq_max_retransmissions = None
        """Maximum number of times a packet is allowed to be retransmitted"""
        self.arq_ack_delay = 100e-3
        """Maximum delay before an explicit ACK is sent (sec)"""
        self.arq_retransmission_delay = 500e-3
        """Duration of retransmission timer (sec)"""
        self.arq_sack_delay = 50e-3
        """Maximum time to wait for a regular packet to have a SACK attached (sec)"""
        self.arq_explicit_nak_win = 10
        """Maximum number of NAKs to send during NAK window"""
        self.arq_explicit_nak_win_duration = 0.1
        """Duration of NAK window (sec)"""
        self.arq_selective_ack = True
        """Send selective ACKs?"""
        self.arq_selective_ack_feedback_delay = 0.300
        """Maximum time to wait before counting a selective NAK as a TX failure"""
        self.arq_mcu = 100
        """Maximum number of extra bytes beyond MTU to be used for control information"""
        self.arq_move_along = True
        """Move the send window along even when it's full"""
        self.arq_broadcast_gain_db = 0.0
        """Gain to be applied to broadcast packets (dB)"""
        self.arq_ack_gain_db = 0.0
        """Gain to be applied to ACK packets (dB)"""

        # AMC options
        self.amc = False
        self.amc_table = None
        self.amc_short_per_window = 100e-3
        self.amc_long_per_window = 400e-3
        self.amc_long_stats_window = 400e-3
        self.amc_mcsidx_broadcast = 0
        self.amc_mcsidx_min = 0
        self.amc_mcsidx_max = 0
        self.amc_mcsidx_init = 0
        self.amc_mcsidx_up_per_threshold = 0.04
        self.amc_mcsidx_down_per_threshold = 0.10
        self.amc_mcsidx_alpha = 0.5
        self.amc_mcsidx_prob_floor = 0.1

        # Snapshot options
        self.snapshot_period = None
        self.snapshot_duration = 0.5

        # Network options
        self.mtu = 1500
        self.queue = 'fifo'
        self.packet_compression = False

        # mandate queue
        self.mandate_bonus_phase = True
        """Flag indicating whether or not to have a bonus phase"""

        # Neighbor discover options
        # discovery_hello_interval is how often we send HELLO packets during
        # discovery, and standard_hello_interval is how often we send HELLO
        # packets during the rest of the run
        self.discovery_hello_interval = 1.0
        self.standard_hello_interval = 60.0

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
        self.collab_iface = None
        self.collab_server_ip = None
        self.collab_server_port = 5556
        self.collab_client_port = 5557
        self.collab_peer_port = 5558

        # Collaboration agent message periods
        self.location_update_period = 15
        self.spectrum_usage_update_period = 5
        self.detailed_performance_update_period = 5

        # Spectrum usage tuning parameters
        self.spec_future_period = 10.0
        """How far into the future to predict spectrum usage"""
        self.spec_chan_trim_lo = 0.05
        """Trim this fraction of the bandwidth from the low edge of channel when predicting"""
        self.spec_chan_trim_hi = 0.05
        """Trim this fraction of the bandwidth from the high edge of channel when predicting"""

    def __str__(self):
        return pformat(self.__dict__)

    @property
    def mcs_table(self):
        return [(self.check, self.fec0, self.fec1, self.ms)]

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
        parser.add_argument('--log-iface', action='append',
                            dest='log_interfaces',
                            help='log packets received on interface')
        parser.add_argument('--log-invalid-headers', action='store_const', const=True,
                            dest='log_invalid_headers',
                            help='log packets with invalid headers')
        parser.add_argument('--log-snapshots', action='store_const', const=True,
                            dest='log_snapshots',
                            help='log snapshots')
        parser.add_argument('--log-protobuf', action='store_const', const=True,
                            dest='log_protobuf',
                            help='log protobuf')
        parser.add_argument('--compress-iface-logs', action='store_const', const=True,
                            dest='compress_interface_logs',
                            help='compress interface logs')

        # USRP settings
        parser.add_argument('--addr', action='store',
                            dest='addr',
                            help='specify device address')
        parser.add_argument('--rx-subdev', action='store', type=str,
                            dest='rx_subdev',
                            help='specify RX subdevice')
        parser.add_argument('--tx-subdev', action='store', type=str,
                            dest='tx_subdev',
                            help='specify TX subdevice')
        parser.add_argument('--rx-antenna', action='store',
                            dest='rx_antenna',
                            help='set RX antenna')
        parser.add_argument('--tx-antenna', action='store',
                            dest='tx_antenna',
                            help='set TX antenna')
        parser.add_argument('--rx-max-samps-factor', action='store', type=int,
                            dest='rx_max_samps_factor',
                            help='set multiplicative factor for rx_max_samps')
        parser.add_argument('--tx-max-samps-factor', action='store', type=int,
                            dest='tx_max_samps_factor',
                            help='set multiplicative factor for tx_max_samps')

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
        parser.add_argument('--rx-bandwidth', action='store', type=float,
                            dest='rx_bandwidth',
                            help='set receive bandwidth (Hz)')
        parser.add_argument('--tx-bandwidth', action='store', type=float,
                            dest='tx_bandwidth',
                            help='set transmit bandwidth (Hz)')
        parser.add_argument('--rx-oversample', action='store', type=float,
                            dest='rx_oversample_factor',
                            help='set RX oversample factor')
        parser.add_argument('--tx-oversample', action='store', type=float,
                            dest='tx_oversample_factor',
                            help='set TX oversample factor')
        parser.add_argument('--channel-bandwidth', action='store', type=float,
                            dest='channel_bandwidth',
                            help='set channel bandwidth (Hz)')

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
                            action='store', type=dragonradio.liquid.CRCScheme,
                            dest='check',
                            help='set data validity check: ' + enumHelp(dragonradio.liquid.CRCScheme))
        parser.add_argument('-c', '--fec0',
                            action='store', type=dragonradio.liquid.FECScheme,
                            dest='fec0',
                            help='set inner FEC: ' + enumHelp(dragonradio.liquid.FECScheme))
        parser.add_argument('-k', '--fec1',
                            action='store', type=dragonradio.liquid.FECScheme,
                            dest='fec1',
                            help='set outer FEC: ' + enumHelp(dragonradio.liquid.FECScheme))
        parser.add_argument('-m', '--mod',
                            action='store', type=dragonradio.liquid.ModulationScheme,
                            dest='ms',
                            help='set modulation scheme: ' + enumHelp(dragonradio.liquid.ModulationScheme))

        # Soft decoding options
        parser.add_argument('--soft-header', action='store_const', const=True,
                            dest='soft_header',
                            help='use soft decoding for header')
        parser.add_argument('--soft-payload', action='store_const', const=True,
                            dest='soft_payload',
                            help='use soft decoding for payload')

        # OFDM-specific options
        parser.add_argument('-M', action='store', type=int,
                            dest='M',
                            help='set number of OFDM subcarriers')
        parser.add_argument('-C', '--cp', action='store', type=int,
                            dest='cp_len',
                            help='set OFDM cyclic prefix length')
        parser.add_argument('-T', '--taper', action='store', type=int,
                            dest='taper_len',
                            help='set OFDM taper length')
        parser.add_argument('--subcarriers', action='store', type=str,
                            dest='subcarriers',
                            help='set OFDM subcarrier allocation (.=null, P=pilot, +=data)')

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
                            choices=['multichannel', 'freqdomain', 'timedomain'],
                            dest='synthesizer',
                            help='set synthesizer algorithm')

        # MAC parameters
        parser.add_argument('--pin-rx-worker', action='store_const', const=True,
                            dest='pin_rx_worker',
                            help='pin RX worker thread to a CPU')
        parser.add_argument('--pin-tx-worker', action='store_const', const=True,
                            dest='pin_tx_worker',
                            help='pin TX worker thread to a CPU')
        parser.add_argument('--slot-size', action='store', type=float,
                            dest='slot_size',
                            help='set MAC slot size (sec)')
        parser.add_argument('--guard-size', action='store', type=float,
                            dest='guard_size',
                            help='set MAC guard interval (sec)')
        parser.add_argument('--demod-overlap-size', action='store', type=float,
                            dest='demod_overlap_size',
                            help='set demodulation overlap interval (sec)')
        parser.add_argument('--fdma', action='store_const', const=True,
                            dest='fdma',
                            help='use FDMA instead of TDMA')
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
        parser.add_argument('--short-per-window', action='store', type=float,
                            dest='amc_short_per_window',
                            help='time window used to calculate short-term PER')
        parser.add_argument('--long-per-window', action='store', type=float,
                            dest='amc_long_per_window',
                            help='time window used to calculate long-term PER')
        parser.add_argument('--long-stats-window', action='store', type=float,
                            dest='amc_long_stats_window',
                            help='time window used to calculate long-term statistics, e.g., EVM and RSSI')
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
                            choices=['fifo', 'lifo', 'mandate'],
                            dest='queue',
                            help='set network queuing algorithm')
        parser.add_argument('--fifo', action='store_const', const='fifo',
                            dest='queue',
                            help='use FIFO network queue algorithm')
        parser.add_argument('--lifo', action='store_const', const='lifo',
                            dest='queue',
                            help='use LIFO network queue algorithm')
        parser.add_argument('--packet-compression', action='store_const', const=True,
                            dest='packet_compression',
                            help='enable network packet compress')

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
        for attr in ['verbose', 'debug', 'mtu', 'verbose_packet_trace']:
            if hasattr(config, attr):
                setattr(dragonradio.rc, attr, getattr(config, attr))

        # Add global work queue workers
        dragonradio.work_queue.addThreads(1)

        # Set default TX channel index
        self.tx_channel_idx = 0

        # Create the USRP
        self.usrp = dragonradio.USRP(config.addr,
                                     config.tx_subdev,
                                     config.rx_subdev,
                                     self.frequency,
                                     config.tx_antenna,
                                     config.rx_antenna,
                                     config.tx_gain,
                                     config.rx_gain)

        self.usrp.rx_max_samps_factor = config.rx_max_samps_factor
        self.usrp.tx_max_samps_factor = config.tx_max_samps_factor

        # Configure valid decimation rates
        self.configureValidDecimationRates()

        # Set the TX and RX rates to None to ensure they are properly set
        # everywhere by setTXRate and setRXRate the first time those two
        # functions are called.
        self.tx_rate = None
        """Current TX rate. None if not yet set."""

        self.rx_rate = None
        """Current RX rate. None if not yet set."""

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
        if config.snapshot_period is not None:
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

        # Construct MCS table
        if config.amc and config.amc_table:
            mcs_table = [(MCS(*mcs), self.mkAutoGain()) for (mcs, _evm_threshold) in config.amc_table]
        else:
            mcs_table = [(MCS(*mcs), self.mkAutoGain()) for mcs in config.mcs_table]

        if config.phy == 'flexframe':
            self.phy = dragonradio.liquid.FlexFrame(self.snapshot_collector,
                                                    self.node_id,
                                                    header_mcs,
                                                    mcs_table,
                                                    config.soft_header,
                                                    config.soft_payload)
        elif config.phy == 'newflexframe':
            self.phy = dragonradio.liquid.NewFlexFrame(self.snapshot_collector,
                                                       self.node_id,
                                                       header_mcs,
                                                       mcs_table,
                                                       config.soft_header,
                                                       config.soft_payload)
        elif config.phy == 'ofdm':
            self.phy = dragonradio.liquid.OFDM(self.snapshot_collector,
                                               self.node_id,
                                               header_mcs,
                                               mcs_table,
                                               config.soft_header,
                                               config.soft_payload,
                                               config.M,
                                               config.cp_len,
                                               config.taper_len,
                                               config.subcarriers)
        else:
            fail('Bad PHY: {}'.format(config.phy))

        #
        # Configure the MAC
        #
        # We start out without a MAC
        #
        self.mac = None
        """The radio's MAC"""

        self.mac_schedule = None
        """Our MAC schedule"""

        #
        # Create tun/tap interface and net neighborhood
        #
        self.tuntap = dragonradio.TunTap('tap0', False, self.config.mtu, self.node_id)

        self.net = dragonradio.Net(self.tuntap, self.node_id)

        #
        # Configure the channelization
        #
        if config.channelizer == 'overlap':
            self.channelizer = dragonradio.OverlapTDChannelizer(self.phy,
                                                                self.usrp.rx_rate,
                                                                Channels([]),
                                                                config.num_demodulation_threads)

            self.channelizer.enforce_ordering = config.channelizer_enforce_ordering
        elif config.channelizer == 'timedomain':
            self.channelizer = dragonradio.TDChannelizer(self.phy,
                                                         self.usrp.rx_rate,
                                                         Channels([]),
                                                         config.num_demodulation_threads)
        elif config.channelizer == 'freqdomain':
            self.channelizer = dragonradio.FDChannelizer(self.phy,
                                                         self.usrp.rx_rate,
                                                         Channels([]),
                                                         config.num_demodulation_threads)
        else:
            raise Exception('Unknown channelizer: %s' % config.channelizer)

        if config.synthesizer == 'timedomain':
            self.synthesizer = dragonradio.TDSynthesizer(self.phy,
                                                         self.usrp.tx_rate,
                                                         Channels([]),
                                                         config.num_modulation_threads)
        elif config.synthesizer == 'freqdomain':
            self.synthesizer = dragonradio.FDSynthesizer(self.phy,
                                                         self.usrp.tx_rate,
                                                         Channels([]),
                                                         config.num_modulation_threads)
        elif config.synthesizer == 'multichannel':
            self.synthesizer = dragonradio.MultichannelSynthesizer(self.phy,
                                                                   self.usrp.tx_rate,
                                                                   Channels([]),
                                                                   config.num_modulation_threads)
        else:
            raise Exception('Unknown synthesizer: %s' % config.synthesizer)

        #
        # Configure the controller
        #

        # Configure EVM thresholds
        if config.amc and config.amc_table:
            def zeroToNone(x):
                if x:
                    return x
                else:
                    return None

            # libconfig can't parse None, so we use zero to represent a
            # non-existant threshold (zero is not a valid EVM threshold)
            evm_thresholds = [zeroToNone(evm_threshold) for (_mcs, evm_threshold) in config.amc_table]
        else:
            evm_thresholds = [None for _ in config.mcs_table]

        if config.arq:
            self.controller = dragonradio.SmartController(self.net,
                                                          self.phy,
                                                          config.slot_size,
                                                          config.arq_window,
                                                          config.arq_window,
                                                          evm_thresholds)

            # Add MCU to MTU
            dragonradio.rc.mtu += config.arq_mcu

            # ARQ parameters
            self.controller.enforce_ordering = config.arq_enforce_ordering
            self.controller.max_retransmissions = config.arq_max_retransmissions
            self.controller.ack_delay = config.arq_ack_delay
            self.controller.retransmission_delay = config.arq_retransmission_delay
            self.controller.sack_delay = config.arq_sack_delay
            self.controller.explicit_nak_window = config.arq_explicit_nak_win
            self.controller.explicit_nak_window_duration = config.arq_explicit_nak_win_duration
            self.controller.selective_ack = config.arq_selective_ack
            self.controller.selective_ack_feedback_delay = config.arq_selective_ack_feedback_delay
            self.controller.move_along = config.arq_move_along
            self.controller.broadcast_gain.dB = config.arq_broadcast_gain_db
            self.controller.ack_gain.dB = config.arq_ack_gain_db

            # AMC parameters
            self.controller.short_per_window = config.amc_short_per_window
            self.controller.long_per_window = config.amc_long_per_window
            self.controller.long_stats_window = config.amc_long_stats_window
            self.controller.mcsidx_broadcast = config.amc_mcsidx_broadcast
            self.controller.mcsidx_min = config.amc_mcsidx_min
            self.controller.mcsidx_max = config.amc_mcsidx_max
            self.controller.mcsidx_init = config.amc_mcsidx_init
            self.controller.mcsidx_up_per_threshold = config.amc_mcsidx_up_per_threshold
            self.controller.mcsidx_down_per_threshold = config.amc_mcsidx_down_per_threshold
            self.controller.mcsidx_alpha = config.amc_mcsidx_alpha
            self.controller.mcsidx_prob_floor = config.amc_mcsidx_prob_floor

        else:
            self.controller = dragonradio.DummyController(self.net)

        #
        # Create flow performance measurement component
        #
        self.flowperf = dragonradio.FlowPerformance(config.measurement_period)

        #
        # Create packet compression component
        #
        self.packet_compressor = dragonradio.PacketCompressor(config.packet_compression)

        #
        # Configure packet path from channelizer to tun/tap
        #
        #   channelizer -> controller -> PacketCompressor.radio -> FlowPerformance.radio -> tun/tap
        #
        self.channelizer.source >> self.controller.radio_in

        self.controller.radio_out >> self.packet_compressor.radio_in

        self.packet_compressor.radio_out >> self.flowperf.radio_in

        self.flowperf.radio_out >> self.tuntap.sink

        #
        # Configure packet path from tun/tap to the synthesizer
        #
        # The path is:
        #   tun/tap -> NetFilter -> FlowPerformance.net -> NetFirewall -> PacketCompressor.net -> NetQueue -> controller -> synthesizer
        #
        self.netfilter = dragonradio.NetFilter(self.net)
        self.netfirewall = dragonradio.NetFirewall()

        if config.queue == 'fifo':
            self.netq = dragonradio.SimpleQueue(dragonradio.SimpleQueue.FIFO)
        elif config.queue == 'lifo':
            self.netq = dragonradio.SimpleQueue(dragonradio.SimpleQueue.LIFO)
        elif config.queue == 'mandate':
            self.netq = dragonradio.MandateQueue()
            self.netq.bonus_phase = config.mandate_bonus_phase
        else:
            raise Exception('Unknown queue type: %s' % config.queue)

        self.tuntap.source >> self.netfilter.input

        self.netfilter.output >> self.flowperf.net_in

        self.flowperf.net_out >> self.netfirewall.input

        self.netfirewall.output >> self.packet_compressor.net_in

        self.packet_compressor.net_out >> self.netq.push

        self.netq.pop >> self.controller.net_in

        self.controller.net_out >> self.synthesizer.sink

        # Allow Controller access to the network queue
        self.controller.net_queue = self.netq

        #
        # Configure channels
        #
        self.configureDefaultChannels()

    def __del__(self):
        if self.logger:
            self.logger.close()

    def mkAutoGain(self):
        config = self.config

        autogain = dragonradio.AutoGain()

        autogain.soft_tx_gain_0dBFS = config.soft_tx_gain
        if config.auto_soft_tx_gain != None:
            autogain.recalc0dBFSEstimate(config.auto_soft_tx_gain)
            autogain.auto_soft_tx_gain_clip_frac = config.auto_soft_tx_gain_clip_frac

        return autogain

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

    def configureDefaultChannels(self):
        """Configure default channels"""
        config = self.config

        bandwidth = self.bandwidth

        if self.config.fdma:
            cbw = config.channel_bandwidth
        else:
            cbw = config.bandwidth

        channels = dragon.channels.defaultChannelPlan(bandwidth, cbw)

        logging.debug("Channels: %s (bandwidth=%g; rx_oversample=%d; tx_oversample=%d; channel bandwidth=%g)",
            list(channels),
            bandwidth,
            config.rx_oversample_factor,
            config.tx_oversample_factor,
            cbw)

        self.setChannels(channels)

    def setChannels(self, channels):
        """Set current channels.

        This function will configure the necessary RX and TX rates and
        initialize the synthesizer and channelizer.
        """
        config = self.config

        self.channels = channels[:config.max_channels]

        #
        # Initialize channelizer
        #
        self.setRXRate(self.bandwidth)

        # We need to do this *after* setting the RX rate because it is used to
        # determine filter parameters
        self.setChannelizerChannels(self.channels)

        #
        # Initialize synthesizer
        #
        if config.tx_upsample:
            self.setTXRate(self.bandwidth)
            self.setSynthesizerChannels(self.channels)
        else:
            self.setTXChannel(self.tx_channel_idx)

        #
        # Reconfigure the MAC
        #
        if self.mac is not None:
            self.mac.reconfigure()

    def setChannelizerChannels(self, channels):
        """Set channelizer's channels."""
        self.channelizer.channels = Channels([(chan, self.genChannelizerTaps(chan)) for chan in channels])

    def setSynthesizerChannels(self, channels):
        """Set synthesizer's channels."""
        config = self.config

        self.synthesizer.channels = Channels([(chan, self.genSynthesizerTaps(chan)) for chan in channels])

        #
        # Tell the MAC the minimum number of samples in a slot
        #
        min_channel_bandwidth = min([chan.bw for (chan, _taps) in self.synthesizer.channels])

        if self.mac is not None:
            self.mac.min_channel_bandwidth = min_channel_bandwidth

        self.controller.min_channel_bandwidth = min_channel_bandwidth

    def configureValidDecimationRates(self):
        """Determine valid decimation and interpolation rates"""
        # See:
        #   https://files.ettus.com/manual/page_general.html#general_sampleratenotes

        # Start out with only even rates. We sort this list in reverse so we can
        # easily find the first rate that is less than or equal to the requested
        # decimation rate.
        rates = sorted([2**i * 5**j for i in range(1,5) for j in range(0,4)], reverse=True)

        # If the rate exceeds 128, then rate must be evenly divisible by 2
        rates = [r for r in rates if r <= 128 or r % 2 == 0]

        # If the rate exceeds 256, the rate must be evenly divisible by 4.
        rates = [r for r in rates if r <= 256 or r % 4 == 0]

        self.valid_rates = rates

    def validRate(self, min_rate, clock_rate):
        """Find a valid rate no less than min_rate given the clock rate clock_rate.

        Arguments:
            min_rate: The minimum desired rate
            clock_rate: The radio clock rate

        Returns:
            A rate no less than rate min_rate that is supported by the hardware"""
        # Compute decimation rate
        dec_rate = math.floor(clock_rate/min_rate)

        logging.debug('Desired decimation rate: %g', dec_rate)

        # Otherwise, make sure we use a safe decimation rate
        if dec_rate != 1:
            for rate in self.valid_rates:
                if dec_rate >= rate:
                    dec_rate = rate
                    break

        logging.debug('Actual decimation rate: %g', dec_rate)

        return clock_rate/dec_rate

    def setRXRate(self, rate):
        """Set RX rate"""
        config = self.config

        if config.rx_bandwidth:
            want_rx_rate = config.rx_bandwidth
        else:
            rx_rate_oversample = config.rx_oversample_factor*self.phy.min_rx_rate_oversample

            want_rx_rate = rate*rx_rate_oversample
            # We max out at about 50Mhz with UHD 3.9
            want_rx_rate = min(want_rx_rate, 50e6)

        want_rx_rate = self.validRate(want_rx_rate, self.usrp.clock_rate)

        if self.rx_rate != want_rx_rate:
            self.usrp.rx_rate = want_rx_rate
            self.rx_rate = self.usrp.rx_rate

            if self.rx_rate != want_rx_rate:
                raise Exception('Wanted RX rate %g, but got %g' % (want_rx_rate, self.rx_rate))

            self.channelizer.rx_rate = self.rx_rate

    def setTXRate(self, rate):
        """Set TX rate"""
        config = self.config

        if config.tx_bandwidth and config.tx_upsample:
            logger.warning("TX bandwidth set, but TX upsampling requested.")

        if config.tx_bandwidth and not config.tx_upsample:
            want_tx_rate = config.tx_bandwidth
        else:
            tx_rate_oversample = config.tx_oversample_factor*self.phy.min_tx_rate_oversample

            want_tx_rate = rate*tx_rate_oversample

        want_tx_rate = self.validRate(want_tx_rate, self.usrp.clock_rate)

        if self.tx_rate != want_tx_rate:
            self.usrp.tx_rate = want_tx_rate
            self.tx_rate = self.usrp.tx_rate

            if self.tx_rate != want_tx_rate:
                raise Exception('Wanted TX rate %g, but got %g' % (want_tx_rate, self.tx_rate))

            self.synthesizer.tx_rate = self.tx_rate

    def setSynthesizerTXChannel(self, channel):
        """Set the synthesizer's transmission channel.

        This function creates an appropriate filter and sets the USRP's TX
        frequency for single-channel synthesis.
        """
        logging.info("Setting TX frequency offset to %g", channel.fc)
        self.usrp.tx_frequency = self.frequency + channel.fc

        self.setSynthesizerChannels([Channel(0, channel.bw)])

    def setTXChannel(self, channel_idx):
        """Set the transmission channel.

        If we are upsampling on TX, this is a no-op. Otherwise we set the
        radio's frequency to transmit on the current channel and configure the
        synthesizer for a single channel.
        """
        config = self.config

        if not config.tx_upsample:
            self.tx_channel_idx = min(channel_idx, len(self.channels) - 1)

            channel = self.channels[self.tx_channel_idx]

            self.setTXRate(channel.bw)
            self.setSynthesizerTXChannel(channel)

            # Allow the MAC to figure out the TX offset so snapshot self
            # tranmissions are correctly logged
            if self.mac is not None:
                self.mac.reconfigure()

    def reconfigureBandwidthAndFrequency(self, bandwidth, frequency):
        """
        Reconfigure the radio for the given bandwidth and frequency
        """
        config = self.config

        if bandwidth == config.bandwidth and frequency == config.frequency:
            return

        logger.info("Reconfiguring radio: bandwidth=%f, frequency=%f", bandwidth, frequency)

        # Set current frequency
        config.frequency = frequency

        self.usrp.rx_frequency = self.frequency
        self.usrp.tx_frequency = self.frequency

        # If the bandwidth has changed, re-configure channels. Otherwise just
        # set the current channel---we need to re-set the channel after a
        # frequency change because although the channel number may be the same,
        # the corresponding frequency will be different.
        if config.bandwidth != bandwidth:
            config.bandwidth = bandwidth

            self.configureDefaultChannels()
        else:
            self.setTXChannel(self.tx_channel_idx)

        # When the environment changes, we reset MCS transition probabilities
        # because we need to re-explore to find the best MCS.
        if isinstance(self.controller, dragonradio.SmartController):
            self.controller.resetMCSTransitionProbabilities()

    def genChannelizerTaps(self, channel):
        """Generate channelizer filter taps for given channel"""
        config = self.config

        # Calculate channelizer taps
        if channel.bw == self.usrp.rx_rate:
            return [1]
        elif config.channelizer == 'freqdomain':
            wp = 0.95*channel.bw
            ws = channel.bw
            fs = self.usrp.rx_rate

            h = lowpass_firpm1f2(wp, ws, fs, Nmax=dragonradio.FDChannelizer.P)
        else:
            wp = 0.9*channel.bw
            ws = 1.1*channel.bw
            fs = self.usrp.rx_rate

            h = lowpass(wp, ws, fs)

        logging.debug('Created prototype lowpass filter for channelizer: N=%d; wp=%g; ws=%g; fs=%g',
                      len(h), wp, ws, fs)
        return h

    def genSynthesizerTaps(self, channel):
        """Generate synthesizer filter taps for given channel"""
        config = self.config

        if channel.bw == self.usrp.tx_rate:
            return [1]
        elif config.synthesizer == 'freqdomain' or config.synthesizer == 'multichannel':
            # Frequency-space synthesizers don't apply a filter
            return [1]
        else:
            wp = 0.9*channel.bw
            ws = 1.1*channel.bw
            fs = self.usrp.tx_rate

            h = lowpass(wp, ws, fs)

        logging.debug('Created prototype lowpass filter for synthesizer: N=%d; wp=%g; ws=%g; fs=%g',
                      len(h), wp, ws, fs)
        return h

    def deleteMAC(self):
        """Delete the current MAC"""
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
                                            config.pin_rx_worker,
                                            config.pin_tx_worker,
                                            config.slot_size,
                                            config.guard_size,
                                            config.slot_send_lead_time,
                                            config.aloha_prob)

        # Install slot-per-channel schedule for ALOHA MAC
        self.installALOHASchedule()

        # We may not use superslots with the ALOHA MAC
        self.synthesizer.superslots = False

        # Set up overlap channelizer
        if isinstance(self.channelizer, dragonradio.OverlapTDChannelizer):
            # We need to demodulate half the previous slot because a sender
            # could start transmitting a packet halfway into a slot + epsilon.
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

        if isinstance(self.mac, dragonradio.TDMA) and self.mac.nslots == nslots:
            return

        self.mac = dragonradio.TDMA(self.usrp,
                                    self.phy,
                                    self.controller,
                                    self.snapshot_collector,
                                    self.channelizer,
                                    self.synthesizer,
                                    config.pin_rx_worker,
                                    config.pin_tx_worker,
                                    config.slot_size,
                                    config.guard_size,
                                    config.slot_send_lead_time,
                                    nslots)

        # We may use superslots with the TDMA MAC
        self.synthesizer.superslots = config.superslots

        # Set up overlap channelizer
        if isinstance(self.channelizer, dragonradio.OverlapTDChannelizer):
            # When using superslots, we need to demodulate half the previous
            # slot because a sender could start transmitting a packet halfway
            # into a slot + epsilon.
            if self.config.superslots:
                self.channelizer.prev_demod = 0.5*config.slot_size
                self.channelizer.cur_demod = config.slot_size
            else:
                self.channelizer.prev_demod = config.demod_overlap_size
                self.channelizer.cur_demod = \
                    config.slot_size - config.guard_size + config.demod_overlap_size

        self.finishConfiguringMAC()

    def finishConfiguringMAC(self):
        bws = [chan.bw for (chan, _taps) in self.synthesizer.channels]
        if len(bws) != 0:
            self.mac.min_channel_bandwidth = min(bws)

    def setALOHAChannel(self, channel_idx):
        """Set the transmission channel for the ALOHA MAC."""
        if not isinstance(self.mac, dragonradio.SlottedALOHA):
            logging.debug("Cannot change ALOHA channel for non-ALOHA MAC")

        if self.config.tx_upsample:
            self.mac.slotidx = channel_idx
        else:
            self.setTXChannel(channel_idx)

    def installALOHASchedule(self):
        """Install a schedule for an ALOHA MAC.

        This installs a schedule with one slot per channel. If we are not
        resampling on TX, it installs a schedule with one slot.
        """
        self.mac.slotidx = 0

        if self.config.tx_upsample:
            self.mac_schedule = np.identity(len(self.channels)).astype('bool')
        else:
            self.setTXChannel(0)

            self.mac_schedule = [[1]]

        self.mac.schedule = self.mac_schedule
        self.synthesizer.schedule = self.mac_schedule

    def installMACSchedule(self, sched):
        """Install a MAC schedule.

        Args:
            sched: The schedule, which is a nchannels X nslots array of node
                IDs.
        """
        config = self.config

        logging.debug('Installing MAC schedule:\n%s', sched)

        # Get number of channels and slots
        (nchannels, nslots) = sched.shape

        # First configure the TDMA MAC for the desired number of slots
        self.configureTDMA(nslots)

        # Determine which nodes are allowed to transmit
        nodes_with_slot = set(sched.flatten())
        if 0 in nodes_with_slot:
            nodes_with_slot.remove(0)

        for (node_id, node) in self.net.nodes.items():
            node.can_transmit = node_id in nodes_with_slot

        # If we are upsampling on TX, go ahead and install the schedule
        if config.tx_upsample:
            self.mac_schedule = (sched == self.node_id)
        # Otherwise we need to pick a channel we're allowed to send on and stick
        # to that
        else:
            try:
                chan = dragon.schedule.bestScheduleChannel(sched, self.node_id)
            except:
                logging.error('No MAC schedule entry for radio %d', self.node_id)
                chan = 0

            self.setTXChannel(chan)

            self.mac_schedule = [sched[chan] == self.node_id]

        self.mac.schedule = self.mac_schedule
        self.synthesizer.schedule = self.mac_schedule

    def configureSimpleMACSchedule(self):
        """
        Set a simple, static TDMA/FDMA schedule based on configuration
        parameters and the given set of nodes.
        """
        config = self.config

        nodes = list(self.net.nodes)
        nodes.sort()

        nchannels = len(self.channels)

        if config.fdma:
            sched = dragon.schedule.fullChannelMACSchedule(nchannels,
                                                           1,
                                                           nodes,
                                                           3)
        else:
            sched = dragon.schedule.pureTDMASchedule(nodes)

        self.installMACSchedule(sched)

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

        master = [((us-t0).secs, (them-t0).secs) for (them, us) in self.net.nodes[self.net.time_master].timestamps]
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

        try:
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
        except CancelledError:
            return

    @property
    def frequency(self):
        return self.config.frequency

    @property
    def bandwidth(self):
        return min(self.config.bandwidth, self.config.max_bandwidth)
