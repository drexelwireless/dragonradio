# Copyright 2018-2022 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

"""Radio configuration"""
import argparse
import configparser
from enum import Enum
import json
import logging
import os
from pathlib import Path
from pprint import pformat
import platform
import re
from typing import Any, List, Mapping, Optional, Type
import yaml

import libconf

from dragonradio.liquid import CRCScheme, FECScheme, ModulationScheme

from _dragonradio.radio import MAC, PHY
from _dragonradio.radio import FDMA, SlottedALOHA, SlottedMAC, TDMA
from _dragonradio.radio import FDChannelizer, TDChannelizer
from _dragonradio.radio import FDSlotSynthesizer, TDSlotSynthesizer, MultichannelSynthesizer
from _dragonradio.radio import FDSynthesizer, TDSynthesizer
from dragonradio.liquid import FlexFrame, NewFlexFrame, OFDM

logger = logging.getLogger('config')

MAC_NAMES = { 'aloha': SlottedALOHA
            , 'tdma': TDMA
            , 'tdma-fdma': TDMA
            , 'fdma': FDMA
            }

def str2mac(name: str) -> Type[MAC]:
    if name in MAC_NAMES:
        return MAC_NAMES[name]

    raise ConfigException(f"{name:} is not a valid MAC")

PHY_NAMES = { 'ofdm': OFDM
            , 'flexframe': FlexFrame
            , 'newflexframe': NewFlexFrame
            }

def str2phy(name: str) -> Type[PHY]:
    if name in PHY_NAMES:
        return PHY_NAMES[name]

    raise ConfigException(f"{name:} is not a valid PHY")

CHANNELIZER_NAMES = { 'freqdomain': FDChannelizer
                    , 'timedomain': TDChannelizer
                    }

def str2channelizer(name: str) -> Type[PHY]:
    if name in CHANNELIZER_NAMES:
        return CHANNELIZER_NAMES[name]

    raise ConfigException(f"{name:} is not a valid channelizer")

SLOTTED_SYNTHESIZER_NAMES = { 'freqdomain': FDSlotSynthesizer
                            , 'timedomain': TDSlotSynthesizer
                            , 'multichannel': MultichannelSynthesizer
                            }

SYNTHESIZER_NAMES = { 'freqdomain': FDSynthesizer
                    , 'timedomain': TDSynthesizer
                    }

def str2synthesizer(mac_class: Type[MAC], name: str) -> Type[PHY]:
    if issubclass(mac_class, SlottedMAC):
        names = SLOTTED_SYNTHESIZER_NAMES
    else:
        names = SYNTHESIZER_NAMES

    if name in names:
        return names[name]

    if name == 'multichannel':
        raise ConfigException('Multichannel synthesizer can only be used with a slotted MAC')

    raise ConfigException(f"{name:} is not a valid synthesizer")

def getNodeIdFromHostname() -> int:
    """Determine node ID from hostname"""
    m = re.search(r'([0-9]{1,3})$', platform.node())
    if not m:
        logger.warning('Cannot determine node ID from hostname; defaulting to node ID 1')
        return 1

    return int(m.group(1))

class ConfigException(Exception):
    pass

class ExtendAction(argparse.Action):
    """Add a list of values to an argument's value"""
    # pylint: disable=too-few-public-methods

    def __init__(self, option_strings, *args, nargs=0, **kwargs):
        super().__init__(option_strings, nargs=0, *args, **kwargs)

    def __call__(self, parser, namespace, values, option_string=None):
        items = getattr(namespace, self.dest) or []
        items.extend(self.const)
        setattr(namespace, self.dest, items)

class LogLevelAction(argparse.Action):
    """Set log level along with verbose and debug flags"""
    # pylint: disable=too-few-public-methods

    def __init__(self, option_strings, *args, **kwargs):
        super().__init__(option_strings, nargs=0, *args, **kwargs)

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
    """Load configuration parameters from a file."""
    def __call__(self, parser, namespace, values, option_string=None):
        namespace.load(values)

class DumpConfigAction(argparse.Action):
    """Dump configuration parameters to a file."""
    def __call__(self, parser, namespace, values, option_string=None):
        namespace.dump(values)

class OversampleConfigAction(argparse.Action):
    """Set both TX and RX oversample factor."""
    def __call__(self, parser, namespace, values, option_string=None):
        namespace.tx_oversample_factor = values
        namespace.rx_oversample_factor = values

def parser():
    """Return the default configuration parser."""
    return Config().parser()

class Config:
    """Radio configuration"""

    team: int = 0
    "Team identifier. Must be in the range [0,7]."
    node_id: int
    "Node identifier"
    num_nodes: Optional[int] = None
    "Total number of nodes"

    # Interactive mode
    interactive: bool = False
    """If True, start IPython shell after radio starts"""
    kernel: bool = False
    """If True, start IPython kernel after radio starts"""

    # Logging
    loglevel: int = logging.WARNING
    """Log level"""
    verbose_packet_trace: bool = False
    "Print every packet sent and received"

    # Log parameters
    log_directory: Optional[str] = None
    """Path to directory holding log files"""
    log_sources: List[str]
    """event sources to log"""
    log_interfaces: List[str]
    """network interfaces to log with pcap"""
    log_invalid_headers: bool = False
    """log packets with invalid headers"""
    log_snapshots: bool = False
    """log snapshots"""
    log_protobuf: bool = False
    """set protobuf log level to DEBUG"""
    log_scoring: bool = False
    """set protobuf log level to DEBUG"""
    compress_interface_logs: bool = False
    """compress pcap file with xz post-capture"""
    logdir_: Optional[str] = None
    """Path to the log directory for this node"""

    # USRP settings
    addr: str = ''
    """USRP device address"""
    rx_antenna: str = 'RX2'
    """USRP RX antenna"""
    tx_antenna: str = 'TX/RX'
    """USRP TX antenna"""
    rx_subdev: Optional[str] = None
    """USRP RX sub-device"""
    tx_subdev: Optional[str] = None
    """USRP TX sub-device"""
    rx_max_samps_factor: int = 8
    """Factor used to calculate maximum number of RX samples"""
    tx_max_samps_factor: int = 8
    """Factor used to calculate maximum number of TX samples"""
    clock_source: Optional[str] = None
    """Clock source for the USRP device"""
    time_source: Optional[str] = None
    """Time source for the USRP device"""

    # USRP firmware
    vivado_path: Optional[Path] = None
    """Path to Vivado"""
    firmware_path: Optional[Path] = None
    """Path to USRP firmware"""

    # Frequency and bandwidth
    # Default frequency in the Colosseum is 1GHz
    frequency: float = 1e9
    """Radio frequency"""
    bandwidth: float = 5e6
    """Radio bandwidth to use"""
    max_bandwidth: float = 50e6
    """float: Max bandwidth radio can handle"""
    rx_bandwidth: Optional[float]  = None
    """If set, always receive at this bandwidth. Otherwise, calculate receive bandwidth based on RX oversample factor and radio bandwidth."""
    tx_bandwidth: Optional[float] = None
    """If set, always transmit at this bandwidth. Otherwise, calculate transmit bandwidth based on TX oversample factor and channel bandwidth."""
    rx_oversample_factor: float = 1.0
    """Oversample factor on RX"""
    tx_oversample_factor: float = 1.0
    """Oversample factor on TX"""
    channel_bandwidth: float = 1e6
    """Default channel bandwidth for FDMA"""

    # TX/RX gain parameters
    tx_gain: float = 25.0
    """USRP RF TX gain (dB)"""
    rx_gain: float = 25.0
    """USRP RF RX gain (dB)"""
    soft_tx_gain: float = -8.0
    """soft (digital) TX gain (dB)"""
    auto_soft_tx_gain: Optional[int] = None
    """Optional[int]: number of packets to use to calculate 0 dBFS soft TX gain"""
    auto_soft_tx_gain_clip_frac: float = 1.0
    """fraction of IQ samples used to calculate 0 dBFS soft TX gain"""

    # PHY parameters
    phy: str = 'ofdm'
    """PHY to use"""
    num_modulation_threads: int = 1
    """Number of modulation threads"""
    num_demodulation_threads: int = 10
    """Number of demodulation threads"""
    max_channels: int = 10
    """Maximum number of channels"""
    tx_upsample: bool = True
    """If True, upsample transmitted data"""

    # Filter parameters
    ftype: str = 'ls'
    """Algorithm used to construct low-pass filter for channelizer."""
    wp_cutoff: float = 0.95
    """Passband cutoff as a fraction of stopband."""
    max_taps: Optional[int] = None
    """Maximum number of filter taps"""
    poly_taps: int = 30
    """Maximum number of taps in a polyphase subfilter"""
    max_denom: int = 2000
    """Maximum denominator for rational resampler rate"""

    # Channelizer parameters
    channelizer: str = 'freqdomain'
    "Channelizer to use"

    # Synthesizer parameters
    synthesizer: str = 'freqdomain'
    """Synthesizer to use"""

    # General liquid modulation options
    check: CRCScheme = 'crc32'
    """CRCScheme: default Liquid CRC scheme"""
    fec0: FECScheme = 'rs8'
    """default Liquid inner FEC"""
    fec1: FECScheme = 'none'
    """default Liquid outer FEC"""
    ms: ModulationScheme = 'qpsk'
    """default Liquid modulation scheme"""

    # Header MCS
    header_check: CRCScheme = 'crc32'
    """Liquid CRC scheme for header"""
    header_fec0: FECScheme = 'none'
    """Liquid inner FEC for header"""
    header_fec1: FECScheme = 'v29p78'
    """Liquid outer FEC for header"""
    header_ms: ModulationScheme = 'bpsk'
    """Liquid modulation scheme for header"""

    # Soft decoding options
    soft_header: bool = True
    "bool: Use soft decoding for header"
    soft_payload: bool = True
    "Use soft decoding for payload"

    # OFDM parameters
    M: int = 48
    """number of OFDM subcarriers"""
    cp_len: int = 6
    """OFDM cyclic prefix length"""
    taper_len: int = 4
    """OFDM taper length"""
    subcarriers: Optional[str] = None
    """string specifying OFDM subcarriers: '.' = null; 'P' = pilot; '+' = data."""

    # MAC parameters
    mac: str = 'tdma-fdma'
    """MAC to use"""
    slot_size: float = .035
    """Total slot duration, including guard interval (seconds)"""
    guard_size: float = .01
    """Size of slot guard interval (seconds)"""
    slot_send_lead_time: float = 5e-3
    """Lead time needed for slot transmission (seconds)"""
    aloha_prob: float = .1
    """Probability of transmission in a given slot for ALOHA"""
    superslots: bool = False
    """True if slots should be combined into superslots"""
    mac_accurate_tx_timestamps: bool = False
    """True if MAC should provide more accurate TX timestamps at a potential performance cost"""
    mac_timed_tx_delay: float = 500e-6
    """Delay for timed TX"""
    neighbor_discovery_period: float = 12.0
    """Neighbor discovery period at radio startup (sec)"""

    # ARQ options
    arq: bool = False
    """Should ARQ be enabled?"""
    arq_window: int = 1024
    """ARQ window size"""
    arq_enforce_ordering: bool = False
    """Should ARQ enforce packet ordering?"""
    arq_max_retransmissions: Optional[int] = None
    """Maximum number of times a packet is allowed to be retransmitted"""
    arq_unreachable_timeout: Optional[float] = None
    """Timeout after which a node is marked unreachable (sec)"""
    arq_proactive_unreachable: bool = False
    """If true, proactively test for unreachable nodes"""
    arq_purge_unreachable: bool = False
    """If true, purge unreachable nodes"""
    arq_ack_delay: float = 100e-3
    """Maximum delay before an explicit ACK is sent (sec)"""
    arq_ack_delay_estimation_window: float = 1.0
    """Time window over which to estimate ACK delay (sec)"""
    arq_retransmission_delay: float = 500e-3
    """Default duration of retransmission timer (sec)"""
    arq_min_retransmission_delay: float = 200e-3
    """Minimum duration of retransmission timer (sec)"""
    arq_retransmission_delay_slop: float = 1.1
    """Safety factor for retransmission timer estimator"""
    arq_sack_delay: float = 50e-3
    """Maximum time to wait for a regular packet to have a SACK attached (sec)"""
    arq_max_sacks: Optional[int] = None
    """Maximum number of SACKs in a packet"""
    arq_explicit_nak_win: int = 10
    """Maximum number of NAKs to send during NAK window"""
    arq_explicit_nak_win_duration: float = 0.1
    """Duration of NAK window (sec)"""
    arq_selective_ack: bool = True
    """Send selective ACKs?"""
    arq_selective_ack_feedback_delay: float = 0.300
    """Maximum time to wait before counting a selective NAK as a TX failure"""
    arq_mcu: int = 100
    """Maximum number of extra bytes beyond MTU to be used for control information"""
    arq_move_along: bool = True
    """Move the send window along even when it's full"""
    arq_decrease_retrans_mcsidx: bool = False
    """Decrease MCS index for retransmitted packets with a deadline"""
    arq_broadcast_gain_db: float = 0.0
    """Gain to be applied to broadcast packets (dB)"""
    arq_ack_gain_db: float = 0.0
    """Gain to be applied to ACK packets (dB)"""

    # AMC options
    amc: bool = False
    """If True, enable AMC. Requires ARQ."""
    amc_table: Optional[Mapping] = None
    """AMC table of modulation and coding schemes"""
    amc_short_per_window: float = 100e-3
    """Time window over which short-term packer error rate is calculated"""
    amc_long_per_window: float = 400e-3
    """Time window over which long-term packer error rate is calculated"""
    amc_short_stats_window: float = 100e-3
    """Time window over which short-term channel state statistics are calculated"""
    amc_long_stats_window: float = 400e-3
    """Time window over which long-term channel state statistics are calculated"""
    amc_aggressive_stats_reset: bool = True
    """Aggressively reset link statistics even if we cannot change MCS index"""
    amc_mcs_fast_adjustment_period: float = 1.0
    """Duration of fast adjustment period after an environment change"""
    amc_mcsidx_broadcast: Optional[int] = None
    """MCS index for broadcast packets"""
    amc_mcsidx_ack: Optional[int] = None
    """MCS index for ACK packets"""
    amc_mcsidx_min: Optional[int] = None
    """minimum MCS index"""
    amc_mcsidx_max: Optional[int] = None
    """maximum MCS index"""
    amc_mcsidx_init: Optional[int] = 0
    """initial MCS index"""
    amc_mcsidx_up_per_threshold: float = 0.04
    """Packet error rate threshold for increasing MCS index"""
    amc_mcsidx_down_per_threshold: float = 0.10
    """Packet error rate threshold for decreasing MCS index"""
    amc_mcsidx_alpha: float = 0.5
    """Multiplier for MCS transition probe probability on MCS index decrease"""
    amc_mcsidx_prob_floor: float = 0.1
    """Minimum MCS transition probe probability"""

    # Snapshot options
    snapshot_frequency: Optional[float] = None
    """How often to take a snapshot (sec)"""
    snapshot_duration: float = 0.5
    """Duration of each snapshot (sec)"""
    snapshot_finalize_wait: float = 200e-3
    """How long to wait for demodulation to finish after stopping a snapshot (sec)"""

    # Network options
    mtu: int = 1500
    """Maximum Transmission Unit"""

    internal_net: str = '10.10.10.0'
    """IP network for internal radio network"""
    internal_netmask: str = '255.255.255.0'
    """IP network mask for internal radio network"""

    external_net: str = '192.168.0.0'
    """IP network for external network"""
    external_netmask: str = '255.255.0.0'
    """IP network mask for external network"""

    tap_iface: str = 'tap0'
    """Tap interface to use"""
    tap_ipaddr: str = '10.10.10.%d'
    """printf-style format string specifying node IP address"""
    tap_macaddr: str = 'c6:ff:ff:ff:ff:%02x'
    """printf-style format string specifying node MAC address"""

    queue: str = 'fifo'
    """Network queue to use"""

    packet_compression: bool = False
    """Enable packet compression?"""

    manet: bool = False
    """MANET mode"""

    #
    # Queue options
    #
    transmission_delay: float = 0.0
    """Estimated packet transmission delay (seconds)"""

    # Mandate queue options
    mandate_bonus_phase: bool = True
    """Flag indicating whether or not to have a bonus phase"""
    mandate_use_wall_timestamp: bool = False
    """Flag indicating whether or not to compute deadline from packet creation timestamp"""

    # Tail drop queue options
    tail_drop_max_size: int = 5*1500
    """Tail drop queue maximum size (bytes)"""

    # RED queue options
    red_gentle: bool = True
    """Gentle RED"""
    red_min_thresh: int = 5*1500
    """RED minimum queue threshold"""
    red_max_thresh: int = 3*5*1500
    """RED maximum queue threshold"""
    red_max_p: float = 0.1
    """RED maximum packet drop probability"""
    red_w_q: float = 0.002
    """RED queue weight (for EWMA)"""

    # Neighbor discover options
    # discovery_hello_interval is how often we send HELLO packets during
    # discovery, and standard_hello_interval is how often we send HELLO
    # packets during the rest of the run
    discovery_hello_interval: float = 1.0
    """Interval between HELLO packets during node discovery (sec)"""
    standard_hello_interval: float = 60.0
    """Standard interval between HELLO packets (post node discovery) (sec)"""

    # Clock synchronization
    clock_sync_period: Optional[float] = None
    """Period at which clock is synchronized"""
    clock_noskew: bool = False
    """Assume no clock skew relative to master"""
    clock_random_bias: float = 0.0
    """Introduce a random bias in USRP clock selected from the uniform distribution [0, bias) (for testing)"""
    clock_use_pps: bool = False
    """Set time on PPS edge. Use with GPSDO."""

    # Measurement options
    measurement_period: float = 1.0
    """DARPA SC2 measurement period (sec)"""

    # Scoring options
    max_performance_age: float = 8.0
    """Performance reports may be from a measurement period no older than
    this many seconds"""

    stats_ignore_window: float = 1.5
    """Ignore flow statistics during this (most recent) time window"""

    # Internal agent options
    status_update_period: float = 5.0
    """Period between status updates (sec)"""

    # Collaboration server options
    force_gateway: bool = False
    """Force this node to be the collaboration gateway"""
    collab_iface: Optional[str] = None
    """Name of Colosseum collaboration interface"""
    collab_server_ip: Optional[str] = None
    """Collaboration server IP address"""
    collab_server_port: int = 5556
    """Collaboration server port"""
    collab_client_port: int = 5557
    """Collaboration client port"""
    collab_peer_port: int = 5558
    """Collaboration peer port"""

    # Collaboration agent message periods
    location_update_period: float = 15.0
    """Period between location updates"""
    spectrum_usage_update_period: float = 5.0
    """Period between spectrum updates"""
    detailed_performance_update_period: float = 5.0
    """Period between detailed performance updates"""

    # Spectrum usage tuning parameters
    spec_future_period: float = 10.0
    """How far into the future to predict spectrum usage"""
    spec_chan_trim_lo: float = 0.05
    """Trim this fraction of the bandwidth from the low edge of channel when predicting"""
    spec_chan_trim_hi: float = 0.05
    """Trim this fraction of the bandwidth from the high edge of channel when predicting"""

    # Traffic options
    traffic_iface: str = 'tr0'
    """Name of Colosseum traffic interface"""

    def __init__(self):
        self.node_id = getNodeIdFromHostname()

        # Log parameters
        self.log_sources = []
        self.log_interfaces  = []

    def __str__(self):
        return pformat(self.asdict())

    def asdict(self) -> dict:
        """Return the attribute values of Config instance as a dictionary.

        Returns:
            dict: Dictionary containing attributes.
        """
        d = dict()

        for attr in self.__annotations__:
            if attr[-1] != '_':
                d[attr] = getattr(self, attr)

        if self.__class__ != Config:
            for attr in Config.__annotations__:
                if attr[-1] != '_':
                    d[attr] = getattr(self, attr)

        return d

    @property
    def mac_class(self) -> Type[MAC]:
        return str2mac(self.mac)

    @property
    def phy_class(self) -> Type[PHY]:
        return str2phy(self.phy)

    @property
    def channelizer_class(self) -> Type[MAC]:
        return str2channelizer(self.channelizer)

    @property
    def synthesizer_class(self) -> Type[MAC]:
        return str2synthesizer(self.mac_class, self.synthesizer)\

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
    def log_level(self, level: str):
        self.loglevel = getattr(logging, level)

    def merge(self, config: Mapping[str, Any]):
        """Merge configuration parameters into this configuration.

        Args:
            config (Mapping[str, Any]): Configuration parameters.
        """
        for key in config:
            setattr(self, key, config[key])

    def load(self, path: Path):
        """Load configuration parameters from a file.

        Configurations can be specified in a libconf-style (``.conf``) file,
        JSON, YAML, or .ini file. A .ini file is assumed to be in
        ``colosseum_config.ini`` format (see the `Radio Command and Control (C2)
        API
        <https://colosseumneu.freshdesk.com/support/solutions/articles/61000253495-radio-command-and-control-c2-api>_`).

        Args:
            path (Path): Path to a configuration file.
        """
        if path.suffix == '.conf':
            with path.open('r') as fp:
                config = libconf.load(fp)
        elif path.suffix == '.json':
            with path.open('r') as fp:
                config = json.load(fp)
        elif path.suffix == '.yaml':
            with path.open('r') as fp:
                config = yaml.safe_load(fp)
        elif path.suffix == '.ini':
            with path.open('r') as fp:
                config_parser = configparser.ConfigParser()
                config_parser.read_file(fp)

            config = {}

            if 'COLLABORATION' in config_parser:
                for key in config_parser['COLLABORATION']:
                    config[key] = config_parser['COLLABORATION'][key]

            if 'RF' in config_parser:
                for key in config_parser['RF']:
                    config[key] = float(config_parser['RF'][key])
        else:
            raise ConfigException(f"Unknown configuration file extension: {path.suffix:}.")

        self.merge(config)

    def dump(self, path: Path):
        """Dump configuration parameters to a file.

        Configurations can be specified in a libconf-style (``.conf``) file,
        JSON, or YAML. The file type will be determined from the suffix of path.

        Args:
            path (Path): Path to a configuration file.
        """
        if path.suffix == '.conf':
            with path.open('w') as fp:
                libconf.dump(self.asdict(), fp)
        elif path.suffix == '.json':
            with path.open('w') as fp:
                json.dump(self.asdict(), fp, indent=2)
        elif path.suffix == '.yaml':
            with path.open('w') as fp:
                yaml.safe_dump(self.asdict(), fp)

    def parser(self):
        """Create an argument parser and populate it with arguments."""
        parser = argparse.ArgumentParser(description='Run dragonradio.',
            formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        self.addArguments(parser)
        return parser

    def addArguments(self, parser: argparse.ArgumentParser):
        """
        Populate an ArgumentParser with arguments corresponding to configuration
        parameters.
        """
        # pylint: disable=too-many-statements
        # pylint: disable=too-many-locals

        def enumHelp(cls):
            return ', '.join(sorted(cls.__members__.keys()))

        # Node ID
        parser.add_argument('--team', action='store', type=int,
                            dest='team',
                            metavar='ID',
                            help='set team ID')
        parser.add_argument('-i', action='store', type=int,
                            dest='node_id',
                            metavar='ID',
                            help='set node ID')
        parser.add_argument('-n', action='store', type=int, dest='num_nodes',
                            metavar='N',
                            help='set number of nodes in network')

        # Load configuration file
        parser.add_argument('--config', type=Path, action=LoadConfigAction,
                            default=argparse.SUPPRESS,
                            metavar='FILE',
                            help='load configuration options from a file')
        parser.add_argument('--dump-config', type=Path, action=DumpConfigAction,
                            default=argparse.SUPPRESS,
                            metavar='FILE',
                            help='dump configuration options to a file')

        # Interactive mode
        interact = parser.add_argument_group('Interactive mode').add_mutually_exclusive_group()

        interact.add_argument('--interactive', action='store_const', const=True,
                              dest='interactive',
                              help='enter interactive shell after radio is started')
        interact.add_argument('--kernel', action='store_const', const=True,
                              dest='kernel',
                              help='start IPython kernel')

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
                         metavar='PATH',
                         help='specify directory for log files')
        log.add_argument('--log-iq', action=ExtendAction,
                         const=['log_slots', 'log_recv_symbols', 'log_sent_iq'],
                         default=argparse.SUPPRESS,
                         dest='log_sources',
                         help='log IQ data')
        log.add_argument('--log-iface', action='append',
                         dest='log_interfaces',
                         metavar='IFACE',
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

        # USRP firmware
        firmware = parser.add_argument_group('USRP firmware')

        firmware.add_argument('--vivado', action='store',
                              dest='vivado_path',
                              help='path to Vivado')
        firmware.add_argument('--firmware', action='store',
                              dest='firmware_path',
                              help='firmware to flash before starting radio')
        firmware.add_argument('--no-firmware', action='store_const', const=None,
                              dest='firmware_path',
                              help='do not flash radio firmware before starting radio')

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
        freqbw.add_argument('--oversample', action=OversampleConfigAction, type=float,
                            dest='oversample_factor',
                            metavar='X',
                            help='set TX and RX oversample factor')
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
                         choices=sorted(PHY_NAMES.keys()),
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

        # Filter parameters
        phy.add_argument('--ftype', action='store',
                         choices=['kaiser', 'ls', 'firpm1f', 'firpm1f2'],
                         dest='ftype',
                         help='algorithm used to construct low-pass filter for channelizer')
        phy.add_argument('--max-taps', action='store', type=int,
                         dest='max_taps',
                         help='maximum number of filter taps')

        # Channelizer parameters
        phy.add_argument('--channelizer', action='store',
                         choices=sorted(CHANNELIZER_NAMES.keys()),
                         dest='channelizer',
                         help='set channelization algorithm')

        # Synthesizer parameters
        phy.add_argument('--synthesizer', action='store',
                         choices=sorted(SLOTTED_SYNTHESIZER_NAMES.keys()),
                         dest='synthesizer',
                         help='set synthesizer algorithm')

        # General liquid modulation options
        liquid = parser.add_argument_group('liquid-dsp')

        liquid.add_argument('-r', '--check',
                            action='store', type=str,
                            dest='check',
                            help='set data validity check: ' + \
                                enumHelp(CRCScheme))
        liquid.add_argument('-c', '--fec0',
                            action='store', type=str,
                            dest='fec0',
                            metavar='FEC',
                            help='set inner FEC: ' + enumHelp(FECScheme))
        liquid.add_argument('-k', '--fec1',
                            action='store', type=str,
                            dest='fec1',
                            metavar='FEC',
                            help='set outer FEC: ' + enumHelp(FECScheme))
        liquid.add_argument('-m', '--mod',
                            action='store', type=str,
                            dest='ms',
                            metavar='MODULATION',
                            help='set modulation scheme: ' + enumHelp(ModulationScheme))

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
                         choices=sorted(MAC_NAMES.keys()),
                         dest='mac',
                         help='set MAC')
        mac.add_argument('--aloha', action='store_const', const='aloha',
                         dest='mac',
                         default=argparse.SUPPRESS,
                         help='use slotted ALOHA MAC')
        mac.add_argument('--tdma', action='store_const', const='tdma',
                         dest='mac',
                         default=argparse.SUPPRESS,
                         help='use pure TDMA MAC')
        mac.add_argument('--fdma', action='store_const', const='fdma',
                         dest='mac',
                         default=argparse.SUPPRESS,
                         help='use FDMA MAC')
        mac.add_argument('--tdma-fdma', action='store_const', const='tdma-fdma',
                         dest='mac',
                         default=argparse.SUPPRESS,
                         help='use TDMA/FDMA MAC')

        mac.add_argument('--slot-size', action='store', type=float,
                         dest='slot_size',
                         metavar='SEC',
                         help='set MAC slot size (sec)')
        mac.add_argument('--guard-size', action='store', type=float,
                         dest='guard_size',
                         metavar='SEC',
                         help='set MAC guard interval (sec)')
        mac.add_argument('--superslots', action='store_const', const=True,
                         dest='superslots',
                         help='use TDMA superslots')

        mac.add_argument('--accurate-mac-tx-timestamps', action='store_const', const=True,
                         dest='mac_accurate_tx_timestamps',
                         help='provide more accurate TX timestamps at a potential performance cost')
        mac.add_argument('--mac-timed-tx-delay', action='store', type=float,
                         dest='mac_timed_tx_delay',
                         metavar='SEC',
                         help='delay for timed TX (sec)')

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
        arq.add_argument('--unreachable-timout', action='store', type=float,
                         dest='arq_unreachable_timeout',
                         metavar='SEC',
                         help='set timeout after which a node is marked unreachable (sec)')
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
        amc.add_argument('--short-stats-window', action='store', type=float,
                         dest='amc_short_stats_window',
                         metavar='SEC',
                         help=('time window used to calculate short-term statistics, '
                               'e.g., EVM and RSSI'))
        amc.add_argument('--long-stats-window', action='store', type=float,
                         dest='amc_long_stats_window',
                         metavar='SEC',
                         help=('time window used to calculate long-term statistics, '
                               'e.g., EVM and RSSI'))
        amc.add_argument('--agressive-stats-reset', action='store_const', const=True,
                         dest='amc_aggressive_stats_reset',
                         help=('Aggressively reset PER statistics even when MCS index does not change'))
        amc.add_argument('--no-agressive-stats-reset', action='store_const', const=False,
                         dest='amc_aggressive_stats_reset',
                         help=('Do not aggressively reset PER statistics even when MCS index does not change'))
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

        snapshot.add_argument('--snapshot-frequency', action='store', type=float,
                              dest='snapshot_frequency',
                              metavar='SEC',
                              help='set snapshot frequency (sec)')
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

        net.add_argument('--tap', action='store', type=str,
                         dest='tap_iface',
                         metavar='IFACE',
                         help='name of tap interface')

        net.add_argument('--queue', action='store',
                         choices=['fifo', 'lifo', 'mandate', 'red'],
                         dest='queue',
                         help='set network queuing algorithm')
        net.add_argument('--fifo', action='store_const', const='fifo',
                         dest='queue',
                         help='use FIFO network queue algorithm')
        net.add_argument('--lifo', action='store_const', const='lifo',
                         dest='queue',
                         help='use LIFO network queue algorithm')
        net.add_argument('--taildrop', action='store_const', const='taildrop',
                         dest='queue',
                         help='use tail drop network queue algorithm')
        net.add_argument('--red', action='store_const', const='red',
                         dest='queue',
                         help='use RED network queue algorithm')

        net.add_argument('--packet-compression', action='store_const', const=True,
                         dest='packet_compression',
                         help='enable network packet compress')

        net.add_argument('--manet', action='store_const', const=True,
                         dest='manet',
                         help='enable MANET support')

        # Clock synchronization options
        clock = parser.add_argument_group('Clock')

        clock.add_argument('--random-bias', action='store', type=float,
                            dest='clock_random_bias',
                            help='introduce a random bias in USRP clock selected from the uniform distribution [0, bias) (for testing)')
        clock.add_argument('--use-pps', action='store_const', const=True,
                           dest='clock_use_pps',
                           help='set time on PPS edge. Use with GPSDO.')

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

        # Set up logging
        logging.basicConfig(format='%(asctime)s:%(name)s:%(levelname)s:%(message)s',
                            level=logging.DEBUG)
