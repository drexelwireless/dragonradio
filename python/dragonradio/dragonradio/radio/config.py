"""Radio configuration"""
import argparse
import configparser
import io
import logging
import os
from pprint import pformat
import platform
import re

import libconf

import dragonradio
import dragonradio.liquid

logger = logging.getLogger('config')

def getNodeIdFromHostname():
    """Determine node ID from hostname"""
    m = re.search(r'([0-9]{1,3})$', platform.node())
    if not m:
        logger.warning('Cannot determine node id from hostname')
        return None

    return int(m.group(1))

class ExtendAction(argparse.Action):
    """Add a list of values to an argument's value"""
    # pylint: disable=too-few-public-methods

    def __init__(self, option_strings, *args, **kwargs):
        super().__init__(option_strings=option_strings,
                         nargs=0,
                         *args, **kwargs)

    def __call__(self, parser, namespace, values, option_string=None):
        items = getattr(namespace, self.dest) or []
        items.extend(self.const)
        setattr(namespace, self.dest, items)

class LogLevelAction(argparse.Action):
    """Set log level along with verbose and debug flags"""
    # pylint: disable=too-few-public-methods

    def __init__(self, option_strings, *args, **kwargs):
        super().__init__(option_strings=option_strings,
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
    """Load configuration parameters from a file in libconf format."""
    # pylint: disable=too-few-public-methods

    def __init__(self, option_strings, *args, **kwargs):
        super().__init__(option_strings=option_strings,
                         *args, **kwargs)

    def __call__(self, parser, namespace, values, option_string=None):
        namespace.loadConfig(values)

class Config:
    """Radio configuration"""
    # pylint: disable=too-many-instance-attributes

    def __init__(self):
        # pylint: disable=too-many-statements

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
        self.clock_source = None
        """Clock source for the USRP device"""
        self.time_source = None
        """Time source for the USRP device"""

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

        # Channelizer parameters
        self.channelizer = 'freqdomain'
        self.channelizer_enforce_ordering = False

        # Synthesizer parameters
        self.synthesizer = 'freqdomain'

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

        # MAC parameters
        self.mac = 'tdma-fdma'
        """Mac"""
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
        self.arq_ack_delay_estimation_window = 1
        """Time window over which to estimate ACK delay (sec)"""
        self.arq_retransmission_delay = 500e-3
        """Default duration of retransmission timer (sec)"""
        self.arq_min_retransmission_delay = 200e-3
        """Minimum duration of retransmission timer (sec)"""
        self.arq_retransmission_delay_slop = 1.1
        """Safety factor for retransmission timer estimator"""
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
        self.arq_decrease_retrans_mcsidx = False
        """Decrease MCS index for retransmitted packets with a deadline"""
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
        self.amc_mcsidx_broadcast = None
        self.amc_mcsidx_ack = None
        self.amc_mcsidx_min = None
        self.amc_mcsidx_max = None
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

        # Queue options
        self.transmission_delay = 0
        """Estimated packet transmission delay (seconds)"""
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
        self.clock_noskew = False
        """Assume no clock skew relative to master"""

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
    def logdir(self):
        """Log directory"""
        if self.logdir_:
            return self.logdir_

        if self.log_directory is None:
            return None

        logdir = os.path.join(self.log_directory, 'node-{:03d}'.format(self.node_id))
        if not os.path.exists(logdir):
            os.makedirs(logdir)

        self.logdir_ = os.path.abspath(logdir)
        return self.logdir_

    @property
    def log_level(self):
        """Log level"""
        return logging.getLevelName(self.loglevel)

    @log_level.setter
    def log_level(self, level):
        self.loglevel = getattr(logging, level)

    def mergeConfig(self, config):
        """Merge a configuration into this configuration"""
        for key in config:
            setattr(self, key, config[key])

    def loadConfig(self, path):
        """Load configuration parameters from a radio.conf file in libconf format."""
        try:
            with io.open(path) as f:
                self.mergeConfig(libconf.load(f))
            logger.info("Loaded radio config '%s'", path)
        except:
            logger.exception("Cannot load radio config '%s'", path)
            raise

    def loadColosseumIni(self, path):
        """Load configuration parameters from a colosseum_config.ini file."""
        try:
            with open(path, 'r') as f:
                logger.debug("Read colosseum.ini '%s':\n%s", path, f.read())
        except:
            logger.exception("Cannot open colosseum_config.ini '%s'", path)
            raise

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
            logger.exception("Cannot load colosseum_config.ini '%s'", path)
            raise

    def parser(self):
        """Create an argument parser and populate it with arguments."""
        parser = argparse.ArgumentParser(description='Run dragonradio.',
            formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        self.addArguments(parser)
        return parser

    def addArguments(self, parser):
        """
        Populate an ArgumentParser with arguments corresponding to configuration
        parameters.
        """
        # pylint: disable=too-many-statements
        # pylint: disable=too-many-locals

        def enumHelp(cls):
            return ', '.join(sorted(cls.__members__.keys()))

        # Node ID
        parser.add_argument('-i', action='store', type=int,
                            dest='node_id',
                            help='set node ID')

        # Load configuration file
        parser.add_argument('--config', action=LoadConfigAction,
                            default=argparse.SUPPRESS,
                            metavar='FILE',
                            help='load configuration options from a file')

        # Log parameters
        log = parser.add_argument_group('Logging')

        log.add_argument('-d', '--debug', action=LogLevelAction, const=logging.DEBUG,
                         default=argparse.SUPPRESS,
                         dest='loglevel',
                         help='print debugging information')
        log.add_argument('-v', '--verbose', action=LogLevelAction, const=logging.INFO,
                         default=argparse.SUPPRESS,
                         dest='loglevel',
                         help='be verbose')
        log.add_argument('--verbose-packet-trace', action='store_const', const=True,
                         default=argparse.SUPPRESS,
                         dest='verbose_packet_trace',
                         help='show trace of packets written to network')

        log.add_argument('-l', action='store',
                         dest='log_directory',
                         help='specify directory for log files')
        log.add_argument('--log-iq', action=ExtendAction,
                         const=['log_slots', 'log_recv_symbols', 'log_sent_iq'],
                         default=argparse.SUPPRESS,
                         dest='log_sources',
                         help='log IQ data')
        log.add_argument('--log-iface', action='append',
                         dest='log_interfaces',
                         help='log packets received on interface')
        log.add_argument('--log-invalid-headers', action='store_const', const=True,
                         dest='log_invalid_headers',
                         help='log packets with invalid headers')
        log.add_argument('--log-snapshots', action='store_const', const=True,
                         dest='log_snapshots',
                         help='log snapshots')
        log.add_argument('--log-protobuf', action='store_const', const=True,
                         dest='log_protobuf',
                         help='log protobuf')
        log.add_argument('--compress-iface-logs', action='store_const', const=True,
                         dest='compress_interface_logs',
                         help='compress interface logs')

        # USRP settings
        usrp = parser.add_argument_group('USRP')

        usrp.add_argument('--addr', action='store',
                          dest='addr',
                          help='specify device address')
        usrp.add_argument('--rx-subdev', action='store', type=str,
                          dest='rx_subdev',
                          metavar='DEVICE',
                          help='specify RX subdevice')
        usrp.add_argument('--tx-subdev', action='store', type=str,
                          dest='tx_subdev',
                          metavar='DEVICE',
                          help='specify TX subdevice')
        usrp.add_argument('--rx-antenna', action='store',
                          dest='rx_antenna',
                          metavar='ANTENNA',
                          help='set RX antenna')
        usrp.add_argument('--tx-antenna', action='store',
                          dest='tx_antenna',
                          metavar='ANTENNA',
                          help='set TX antenna')
        usrp.add_argument('--rx-max-samps-factor', action='store', type=int,
                          dest='rx_max_samps_factor',
                          metavar='X',
                          help='set multiplicative factor for rx_max_samps')
        usrp.add_argument('--tx-max-samps-factor', action='store', type=int,
                          dest='tx_max_samps_factor',
                          metavar='X',
                          help='set multiplicative factor for tx_max_samps')
        usrp.add_argument('--clock-source', action='store',
                          dest='clock_source',
                          metavar='CLOCK',
                          help='set clock source')
        usrp.add_argument('--time-source', action='store',
                          dest='time_source',
                          metavar='CLOCK',
                          help='set time source')

        # Frequency and bandwidth
        freqbw = parser.add_argument_group('Frequency and bandwidth')

        freqbw.add_argument('-f', '--frequency', action='store', type=float,
                            dest='frequency',
                            metavar='HZ',
                            help='set center frequency (Hz)')
        freqbw.add_argument('-b', '--bandwidth', action='store', type=float,
                            dest='bandwidth',
                            metavar='HZ',
                            help='set bandwidth (Hz)')
        freqbw.add_argument('--max-bandwidth', action='store', type=float,
                            dest='max_bandwidth',
                            metavar='HZ',
                            help='set maximum bandwidth (Hz)')
        freqbw.add_argument('--rx-bandwidth', action='store', type=float,
                            dest='rx_bandwidth',
                            metavar='HZ',
                            help='set receive bandwidth (Hz)')
        freqbw.add_argument('--tx-bandwidth', action='store', type=float,
                            dest='tx_bandwidth',
                            metavar='HZ',
                            help='set transmit bandwidth (Hz)')
        freqbw.add_argument('--rx-oversample', action='store', type=float,
                            dest='rx_oversample_factor',
                            metavar='X',
                            help='set RX oversample factor')
        freqbw.add_argument('--tx-oversample', action='store', type=float,
                            dest='tx_oversample_factor',
                            metavar='X',
                            help='set TX oversample factor')
        freqbw.add_argument('--channel-bandwidth', action='store', type=float,
                            dest='channel_bandwidth',
                            metavar='HZ',
                            help='set channel bandwidth (Hz)')

        # Gain-related options
        gain = parser.add_argument_group('Gain')

        gain.add_argument('-G', '--tx-gain', action='store', type=float,
                          dest='tx_gain',
                          metavar='DB',
                          help='set UHD TX gain (dB)')
        gain.add_argument('-R', '--rx-gain', action='store', type=float,
                          dest='rx_gain',
                          metavar='DB',
                          help='set UHD RX gain (dB)')
        gain.add_argument('-g', '--soft-tx-gain', action='store', type=float,
                          dest='soft_tx_gain',
                          metavar='DB',
                          help='set soft TX gain (dB)')
        gain.add_argument('--auto-soft-tx-gain', action='store', type=int,
                          dest='auto_soft_tx_gain',
                          metavar='COUNT',
                          help='use COUNT packets to calculate soft TX gain to attain 0dBFS')
        gain.add_argument('--auto-soft-tx-gain-clip-frac', action='store', type=float,
                          dest='auto_soft_tx_gain_clip_frac',
                          metavar='FRACTION',
                          help='clip fraction for automatic soft TX gain')

        # PHY parameters
        phy = parser.add_argument_group('PHY')

        phy.add_argument('--phy', action='store',
                         choices=['flexframe', 'newflexframe', 'ofdm'],
                         dest='phy',
                         help='set PHY')
        phy.add_argument('--max-channels', action='store', type=int,
                         dest='max_channels',
                         help='set maximum number of channels')
        phy.add_argument('--tx-upsample', action='store_const', const=True,
                         dest='tx_upsample',
                         help='use software upsampler on TX')
        phy.add_argument('--no-tx-upsample', action='store_const', const=False,
                         dest='tx_upsample',
                         help='use USRP\'s hardware upsampler on TX')

        # Channelizer parameters
        phy.add_argument('--channelizer', action='store',
                         choices=['freqdomain', 'timedomain', 'overlap'],
                         dest='channelizer',
                         help='set channelization algorithm')
        phy.add_argument('--channelizer-enforce-ordering', action='store_const', const=True,
                         dest='channelizer_enforce_ordering',
                         help='enforce packet order when demodulating in channelizer')

        # Synthesizer parameters
        phy.add_argument('--synthesizer', action='store',
                         choices=['multichannel', 'freqdomain', 'timedomain'],
                         dest='synthesizer',
                         help='set synthesizer algorithm')

        # General liquid modulation options
        liquid = parser.add_argument_group('liquid-dsp')

        liquid.add_argument('-r', '--check',
                            action='store', type=dragonradio.liquid.CRCScheme,
                            dest='check',
                            help='set data validity check: ' + \
                                enumHelp(dragonradio.liquid.CRCScheme))
        liquid.add_argument('-c', '--fec0',
                            action='store', type=dragonradio.liquid.FECScheme,
                            dest='fec0',
                            metavar='FEC',
                            help='set inner FEC: ' + \
                                enumHelp(dragonradio.liquid.FECScheme))
        liquid.add_argument('-k', '--fec1',
                            action='store', type=dragonradio.liquid.FECScheme,
                            dest='fec1',
                            metavar='FEC',
                            help='set outer FEC: ' + \
                                enumHelp(dragonradio.liquid.FECScheme))
        liquid.add_argument('-m', '--mod',
                            action='store', type=dragonradio.liquid.ModulationScheme,
                            dest='ms',
                            metavar='MODULATION',
                            help='set modulation scheme: ' + \
                                enumHelp(dragonradio.liquid.ModulationScheme))

        # Soft decoding options
        liquid.add_argument('--soft-header', action='store_const', const=True,
                            dest='soft_header',
                            help='use soft decoding for header')
        liquid.add_argument('--soft-payload', action='store_const', const=True,
                            dest='soft_payload',
                            help='use soft decoding for payload')

        # OFDM-specific options
        ofdm = parser.add_argument_group('OFDM')

        ofdm.add_argument('-M', action='store', type=int,
                          dest='M',
                          metavar='N',
                          help='set number of OFDM subcarriers')
        ofdm.add_argument('-C', '--cp', action='store', type=int,
                          dest='cp_len',
                          metavar='N',
                          help='set OFDM cyclic prefix length')
        ofdm.add_argument('-T', '--taper', action='store', type=int,
                          dest='taper_len',
                          metavar='N',
                          help='set OFDM taper length')
        ofdm.add_argument('--subcarriers', action='store', type=str,
                          dest='subcarriers',
                          help='set OFDM subcarrier allocation (.=null, P=pilot, +=data)')

        # MAC parameters
        mac = parser.add_argument_group('MAC')

        mac.add_argument('--mac', action='store',
                         choices=['aloha', 'tdma', 'tdma-fdma', 'fdma'],
                         dest='mac',
                         help='set MAC')
        mac.add_argument('--aloha', action='store_const', const='aloha',
                         dest='mac',
                         help='use slotted ALOHA MAC')
        mac.add_argument('--tdma', action='store_const', const='tdma',
                         dest='mac',
                         help='use pure TDMA MAC')
        mac.add_argument('--fdma', action='store_const', const='fdma',
                         dest='mac',
                         help='use FDMA MAC')
        mac.add_argument('--tdma-fdma', action='store_const', const='tdma-fdma',
                         dest='mac',
                         help='use TDMA/FDMA MAC')

        mac.add_argument('--slot-size', action='store', type=float,
                         dest='slot_size',
                         metavar='SEC',
                         help='set MAC slot size (sec)')
        mac.add_argument('--guard-size', action='store', type=float,
                         dest='guard_size',
                         metavar='SEC',
                         help='set MAC guard interval (sec)')
        mac.add_argument('--demod-overlap-size', action='store', type=float,
                         dest='demod_overlap_size',
                         metavar='SEC',
                         help='set demodulation overlap interval (sec)')
        mac.add_argument('--superslots', action='store_const', const=True,
                         dest='superslots',
                         help='use TDMA superslots')

        # ARQ options
        arq = parser.add_argument_group('ARQ')

        arq.add_argument('--arq', action='store_const', const=True,
                         dest='arq',
                         default=argparse.SUPPRESS,
                         help='enable ARQ')
        arq.add_argument('--no-arq', action='store_const', const=False,
                         dest='arq',
                         default=argparse.SUPPRESS,
                         help='disable ARQ')
        arq.add_argument('--arq-window', action='store', type=int,
                         dest='arq_window',
                         metavar='NPACKETS',
                         help='set ARQ window size')
        arq.add_argument('--arq-enforce-ordering', action='store_const', const=True,
                         dest='arq_enforce_ordering',
                         help='enforce packet order when performing ARQ')
        arq.add_argument('--max-retransmissions', action='store', type=int,
                         dest='arq_max_retransmissions',
                         metavar='COUNT',
                         help='set maximum number of retransmission attempts')
        arq.add_argument('--explicit-nak-window', action='store', type=int,
                         dest='arq_explicit_nak_win',
                         metavar='NPACKETS',
                         help='set explicit NAK window size')
        arq.add_argument('--explicit-nak-window-duration', action='store', type=float,
                         dest='arq_explicit_nak_win_duration',
                         metavar='SEC',
                         help='set explicit NAK window duration (sec)')
        arq.add_argument('--selective-ack', action='store_const', const=True,
                         dest='arq_selective_ack',
                         help='send selective ACK\'s')
        arq.add_argument('--no-selective-ack', action='store_const', const=False,
                         dest='arq_selective_ack',
                         help='do not send selective ACK\'s')

        # AMC options
        amc = parser.add_argument_group('AMC')

        amc.add_argument('--amc', action='store_const', const=True,
                         dest='amc',
                         default=argparse.SUPPRESS,
                         help='enable AMC')
        amc.add_argument('--no-amc', action='store_const', const=False,
                         dest='amc',
                         default=argparse.SUPPRESS,
                         help='disable AMC')
        amc.add_argument('--short-per-window', action='store', type=float,
                         dest='amc_short_per_window',
                         metavar='SEC',
                         help='time window used to calculate short-term PER')
        amc.add_argument('--long-per-window', action='store', type=float,
                         dest='amc_long_per_window',
                         metavar='SEC',
                         help='time window used to calculate long-term PER')
        amc.add_argument('--long-stats-window', action='store', type=float,
                         dest='amc_long_stats_window',
                         metavar='SEC',
                         help=('time window used to calculate long-term statistics, '
                               'e.g., EVM and RSSI'))
        amc.add_argument('--mcsidx-up-per-threshold', action='store', type=float,
                         dest='amc_mcsidx_up_per_threshold',
                         metavar='FRACTION',
                         help='set PER threshold for increasing modulation level')
        amc.add_argument('--mcsidx-down-per-threshold', action='store', type=float,
                         dest='amc_mcsidx_down_per_threshold',
                         metavar='FRACTION',
                         help='set PER threshold for decreasing modulation level')
        amc.add_argument('--mcsidx-alpha', action='store', type=float,
                         dest='amc_mcsidx_alpha',
                         metavar='ALPHA',
                         help='set decay factor for learning MCS transition probabilities')
        amc.add_argument('--mcsidx-prob-floor', action='store', type=float,
                         dest='amc_mcsidx_prob_floor',
                         metavar='FRACTION',
                         help='set minimum MCS transition probability')

        # Snapshot options
        snapshot = parser.add_argument_group('Snapshots')

        snapshot.add_argument('--snapshot-period', action='store', type=float,
                              dest='snapshot_period',
                              metavar='SEC',
                              help='set snapshot period (sec)')
        snapshot.add_argument('--snapshot-duration', action='store', type=float,
                              dest='snapshot_duration',
                              metavar='SEC',
                              help='set snapshot duration (sec)')

        # Network options
        net = parser.add_argument_group('Network')

        net.add_argument('--mtu', action='store', type=int,
                         dest='mtu',
                         metavar='BYTES',
                         help='set Maximum Transmission Unit (bytes)')
        net.add_argument('--queue', action='store',
                         choices=['fifo', 'lifo', 'mandate'],
                         dest='queue',
                         help='set network queuing algorithm')
        net.add_argument('--fifo', action='store_const', const='fifo',
                         dest='queue',
                         help='use FIFO network queue algorithm')
        net.add_argument('--lifo', action='store_const', const='lifo',
                         dest='queue',
                         help='use LIFO network queue algorithm')

        net.add_argument('--packet-compression', action='store_const', const=True,
                         dest='packet_compression',
                         help='enable network packet compress')

        # Collaboration server options
        collab = parser.add_argument_group('Collaboration')

        collab.add_argument('--force-gateway', action='store_const', const=True,
                            dest='force_gateway',
                            help='force this node to act as a gateway')
        collab.add_argument('--collab-server-ip', action='store', type=str,
                            dest='collab_server_ip',
                            metavar='IP',
                            help='set collaboration server IP address')

        # Set defaults
        defaults = {}

        for act in parser._actions: # pylint: disable=protected-access
            if not isinstance(act, LogLevelAction) and not isinstance(act, ExtendAction):
                dest = act.dest
                if hasattr(self, dest) and parser.get_default(dest) != argparse.SUPPRESS:
                    defaults[dest] = getattr(self, dest)

        parser.set_defaults(**defaults)
