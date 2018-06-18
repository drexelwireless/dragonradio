import configparser
import io
import libconf
import logging
import os
from pprint import pformat
import platform
import re
import sys

import dragonradio

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

class Config(object):
    def __init__(self):
        # Set some default values
        self.node_id = getNodeIdFromHostname()

        # Log parameters
        self.log_directory = None
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

        # TX/RX gain parameters
        self.tx_gain = 25
        self.rx_gain = 25
        self.soft_tx_gain = -12
        self.auto_soft_tx_gain = None

        # PHY parameters
        self.phy = 'flexframe'
        self.min_packet_size = 512
        self.num_modulation_threads = 2
        self.num_demodulation_threads = 10

        # General liquid modulation options
        self.check = 'crc32'
        self.fec0 = 'v29'
        self.fec1 = 'rs8'
        self.ms = 'qpsk'

        # OFDM parameters
        self.M = 48
        self.cp_len = 6
        self.taper_len = 4

        # MAC parameters
        self.slot_size = .035
        self.guard_size = .01
        self.aloha_prob = .1

        # ARQ options
        self.arq = False
        self.arq_window = 1024

        # AMC options
        self.amc = False
        self.amc_table = None

        self.short_per_npackets = 50
        self.long_per_npackets = 200
        self.modidx_up_per_threshold = 0.02
        self.modidx_down_per_threshold = 0.10

        # Default collab server settings
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
        except:
            logger.exception("Cannot load radio config '%s'", path)

    def loadArgs(self, args):
        """
        Load configuration parameters from command-line arguments.
        """
        # Forget arguments that aren't set so that they don't override existing
        # settings
        dict = args.__dict__
        keys = list(dict.keys())

        for key in keys:
            if dict[key] == None:
                del dict[key]

        self.mergeConfig(dict)

    def loadColosseumIni(self, path):
        """
        Load configuration parameters from a colosseum_config.ini file.
        """
        try:
            config = configparser.ConfigParser()
            config.read(path)

            if 'COLLABORATION' in config:
                for key in config['COLLABORATION']:
                    setattr(self, key, config['COLLABORATION'][key])

            if 'RF' in config:
                for key in config['RF']:
                    setattr(self, key, float(config['COLLABORATION'][key]))
        except:
            logger.exception('Cannot load colosseum_config.ini')

    def addArguments(self, parser, allow_defaults=True):
        """
        Populate an ArgumentParser with arguments corresponding to configuration
        parameters.
        """
        def enumHelp(cls):
            return ', '.join(sorted(cls.__members__.keys()))

        def add_argument(*args, default=None, **kwargs):
            if allow_defaults:
                parser.add_argument(*args, default=getattr(self, kwargs['dest']), **kwargs)
            else:
                parser.add_argument(*args, **kwargs)

        # Log parameters
        add_argument('-l', action='store',
                     dest='log_directory',
                     help='specify directory for log files')

        # USRP settings
        add_argument('--addr', action='store',
                     dest='addr',
                     help='specify device address')
        add_argument('--rx-antenna', action='store',
                     dest='rx_antenna',
                     help='set RX antenna')
        add_argument('--tx-antenna', action='store',
                     dest='tx_antenna',
                     help='set TX antenna')

        # Frequency and bandwidth
        add_argument('-f', '--frequency', action='store', type=float,
                     dest='frequency',
                     help='set center frequency (Hz)')
        add_argument('-b', '--bandwidth', action='store', type=float,
                     dest='bandwidth',
                     help='set bandwidth (Hz)')

        # Gain-related options
        add_argument('-G', '--tx-gain', action='store', type=float,
                     dest='tx_gain',
                     help='set UHD TX gain (dB)')
        add_argument('-R', '--rx-gain', action='store', type=float,
                     dest='rx_gain',
                     help='set UHD RX gain (dB)')
        add_argument('-g', '--soft-tx-gain', action='store', type=float,
                     dest='soft_tx_gain',
                     help='set soft TX gain (dB)')
        add_argument('--auto-soft-tx-gain', action='store', type=int,
                     dest='auto_soft_tx_gain',
                     help='automatically choose soft TX gain to attain 0dBFS')

        # PHY parameters
        add_argument('--phy', action='store',
                     choices=['flexframe', 'ofdm', 'multiofdm'],
                     dest='phy',
                     help='set PHY')

        # General liquid modulation options
        add_argument('-r', '--check',
                     action='store', type=dragonradio.CRCScheme,
                     dest='check',
                     help='set data validity check: ' + enumHelp(dragonradio.CRCScheme))
        add_argument('-c', '--fec0',
                     action='store', type=dragonradio.FECScheme,
                     dest='fec0',
                     help='set inner FEC: ' + enumHelp(dragonradio.FECScheme))
        add_argument('-k', '--fec1',
                     action='store', type=dragonradio.FECScheme,
                     dest='fec1',
                     help='set outer FEC: ' + enumHelp(dragonradio.FECScheme))
        add_argument('-m', '--mod',
                     action='store', type=dragonradio.ModulationScheme,
                     dest='ms',
                     help='set modulation scheme: ' + enumHelp(dragonradio.ModulationScheme))

        # OFDM-specific options
        add_argument('-M', '--subcarriers', action='store', type=int,
                     dest='M',
                     help='set number of OFDM subcarriers')
        add_argument('-C', '--cp', action='store', type=int,
                     dest='cp_len',
                     help='set OFDM cyclic prefix length')
        add_argument('-T', '--taper', action='store', type=int,
                     dest='taper_len',
                     help='set OFDM taper length')

        # ARQ options
        add_argument('--arq', action='store_const', const=True,
                     dest='arq',
                     help='enable ARQ')
        add_argument('--no-arq', action='store_const', const=False,
                     dest='arq',
                     help='disable ARQ')

        # AMC options
        add_argument('--amc', action='store_const', const=True,
                     dest='amc',
                     help='enable AMC')
        add_argument('--no-amc', action='store_const', const=False,
                     dest='amc',
                     help='disable AMC')
        add_argument('--short-per-npackets', action='store', type=int,
                     dest='short_per_npackets',
                     help='set number of packets we use to calculate short-term PER')
        add_argument('--long-per-npackets', action='store', type=int,
                     dest='long_per_npackets',
                     help='set number of packets we use to calculate long-term PER')
        add_argument('--modidx-up-per-threshold', action='store', type=float,
                     dest='modidx_up_per_threshold',
                     help='set PER threshold for increasing modulation level')
        add_argument('--modidx-down-per-threshold', action='store', type=float,
                     dest='modidx_down_per_threshold',
                     help='set PER threshold for decreasing modulation level')

class Radio(object):
    def __init__(self, config):
        self.config = config
        self.node_id = config.node_id
        self.logger = None

        logger.info('Radio configuration:\n' + str(config))

        # Copy configuration settings to the C++ RadioConfig object
        for attr in ['verbose', 'short_per_npackets', 'long_per_npackets']:
            if hasattr(config, attr):
                setattr(dragonradio.rc, attr, getattr(config, attr))

        # Add global work queue workers
        dragonradio.work_queue.addThreads(1)

        # Create the USRP
        self.usrp = dragonradio.USRP(config.addr,
                                     config.frequency,
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
            self.logger.setAttribute('node_id', self.node_id)
            self.logger.setAttribute('frequency', config.frequency)
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
        # Configure the PHY
        #
        if config.phy == 'flexframe':
            self.phy = dragonradio.FlexFrame(config.min_packet_size)
        elif config.phy == 'ofdm':
            self.phy = dragonradio.OFDM(config.M,
                                        config.cp_len,
                                        config.taper_len,
                                        config.min_packet_size)
        elif config.phy == 'multiofdm':
            self.phy = dragonradio.MultiOFDM(config.M,
                                             config.cp_len,
                                             config.taper_len,
                                             config.min_packet_size)
        else:
            fail('Bad PHY: {}'.format(config.phy))

        #
        # Create tun/tap interface and net neighborhood
        #
        self.tuntap = dragonradio.TunTap('tap0', False, 1500, '10.10.10.%d', 'c6:ff:ff:ff:ff:%02x', self.node_id)

        self.net = dragonradio.Net(self.tuntap, self.node_id)

        #
        # Set up TX params for network
        #
        if config.amc and config.amc_table:
            tx_params = []
            for (crc, fec0, fec1, ms) in config.amc_table:
                tx_params.append(dragonradio.TXParams(crc, fec0, fec1, ms))
        else:
            tx_params = [dragonradio.TXParams(config.check, config.fec0, config.fec1, config.ms)]

        for p in tx_params:
            p.soft_tx_gain_0dBFS = config.soft_tx_gain
            if config.auto_soft_tx_gain != None:
                p.recalc0dBFSEstimate(config.auto_soft_tx_gain)

        self.net.tx_params = dragonradio.TXParamsList(tx_params)

        #
        # Configure the modulator and demodulator
        #
        self.modulator = dragonradio.ParallelPacketModulator(self.net,
                                                             self.phy,
                                                             config.num_modulation_threads)

        self.demodulator = dragonradio.ParallelPacketDemodulator(self.net,
                                                                 self.phy,
                                                                 config.num_demodulation_threads)

        #
        # Configure the controller
        #
        if config.arq:
            self.controller = dragonradio.SmartController(self.net,
                                                          config.arq_window,
                                                          config.arq_window,
                                                          config.modidx_up_per_threshold,
                                                          config.modidx_down_per_threshold)
        else:
            self.controller = dragonradio.DummyController(self.net)

        #
        # Configure packet path from demodulator to tun/tap
        # Right now, the path is direct:
        #   demodulator -> controller -> tun/tap
        #
        self.demodulator.source >> self.controller.radio_in

        self.controller.radio_out >> self.tuntap.sink

        #
        # Configure packet path from tun/tap to the modulator
        # The path is:
        #   tun/tap -> NetFilter -> NetQueue -> controller -> modulator
        #
        self.netfilter = dragonradio.NetFilter(self.net)

        self.netq = dragonradio.NetQueue()

        self.tuntap.source >> self.netfilter.input

        self.netfilter.output >> self.netq.push

        self.netq.pop >> self.controller.net_in

        self.controller.net_out >> self.modulator.sink

        #
        # If we are using a SmartController, tell it about the network queue is
        # so that it can add high-priority packets.
        #
        if config.arq:
            self.controller.net_queue = self.netq

            self.controller.broadcast_tx_params.soft_tx_gain_0dBFS = config.soft_tx_gain
            if config.auto_soft_tx_gain != None:
                self.controller.broadcast_tx_params.recalc0dBFSEstimate(config.auto_soft_tx_gain)

    def configureALOHA(self):
        self.mac = dragonradio.SlottedALOHA(self.usrp,
                                            self.phy,
                                            self.modulator,
                                            self.demodulator,
                                            self.config.bandwidth,
                                            self.config.slot_size,
                                            self.config.guard_size,
                                            self.config.aloha_prob)

        if self.logger:
            self.logger.setAttribute('tx_bandwidth', self.usrp.tx_rate)
            self.logger.setAttribute('rx_bandwidth', self.usrp.rx_rate)

    def configureTDMA(self, nslots):
        self.mac = dragonradio.TDMA(self.usrp,
                                    self.phy,
                                    self.modulator,
                                    self.demodulator,
                                    self.config.bandwidth,
                                    self.config.slot_size,
                                    self.config.guard_size,
                                    nslots)

        if self.logger:
            self.logger.setAttribute('tx_bandwidth', self.usrp.tx_rate)
            self.logger.setAttribute('rx_bandwidth', self.usrp.rx_rate)

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
