import configparser
import io
import libconf
import logging
import platform
import re
import sys

import dragonradio

logger = logging.getLogger('radio')

def fail(msg):
    print('Cannot determine node id', file=sys.stderr)
    sys.exit(1)

def getNodeIdFromHostname():
    m = re.search(r'([0-9]{1,3})$', platform.node())
    if m:
        return int(m.group(1))
    else:
        fail('Cannot determine node id')

class Config(object):
    def __init__(self):
        # Set some default values
        self.frequency = 1e9
        self.collab_server_port = 5556
        self.collab_client_port = 5557
        self.collab_peer_port = 5558

    def fixEnums(self):
        if hasattr(self, 'ms') and self.ms:
            self.ms = dragonradio.ModulationScheme(self.ms)

        if hasattr(self, 'check') and self.check:
            self.check = dragonradio.CRCScheme(self.check)

        if hasattr(self, 'fec0') and self.fec0:
            self.fec0 = dragonradio.FECScheme(self.fec0)

        if hasattr(self, 'fec1') and self.fec1:
            self.fec1 = dragonradio.FECScheme(self.fec1)

def enumHelp(cls):
    return ', '.join(sorted(cls.__members__.keys()))

def addArguments(parser, allow_defaults=True):
    def add_argument(*args, default=None, **kwargs):
        if allow_defaults:
            parser.add_argument(*args, default=default, **kwargs)
        else:
            parser.add_argument(*args, **kwargs)

    add_argument('--addr', action='store',
                 default='',
                 dest='addr',
                 help='specify device address')
    add_argument('--rx-antenna', action='store',
                 default='RX2',
                 dest='rx_antenna',
                 help='set RX antenna')
    add_argument('--tx-antenna', action='store',
                 default='TX/RX',
                 dest='tx_antenna',
                 help='set TX antenna')
    add_argument('--phy', action='store',
                 choices=['flexframe', 'ofdm', 'multiofdm'],
                 default='flexframe',
                 dest='phy',
                 help='set PHY')
    add_argument('-f', '--frequency', action='store', type=float,
                 default=3e9,
                 dest='frequency',
                 help='set center frequency (Hz)')
    add_argument('-b', '--bandwidth', action='store', type=float,
                 default=5e6,
                 dest='bandwidth',
                 help='set bandwidth (Hz)')
    add_argument('-g', '--soft-tx-gain', action='store', type=float,
                 default=-12,
                 dest='soft_tx_gain',
                 help='set soft TX gain (dB)')
    add_argument('-G', '--tx-gain', action='store', type=float,
                 default=25,
                 dest='tx_gain',
                 help='set UHD TX gain (dB)')
    add_argument('-R', '--rx-gain', action='store', type=float,
                 default=25,
                 dest='rx_gain',
                 help='set UHD RX gain (dB)')
    add_argument('--auto-soft-tx-gain', action='store_const', const=True,
                 default=False,
                 dest='auto_soft_tx_gain',
                 help='automatically choose soft TX gain')
    add_argument('-M', '--subcarriers', action='store', type=int,
                 default=48,
                 dest='M',
                 help='set number of OFDM subcarriers')
    add_argument('-C', '--cp', action='store', type=int,
                 default=6,
                 dest='cp_len',
                 help='set OFDM cyclic prefix length')
    add_argument('-T', '--taper', action='store', type=int,
                 default=4,
                 dest='taper_len',
                 help='set OFDM taper length')
    add_argument('-m', '--mod',
                 action='store', type=dragonradio.ModulationScheme,
                 default='qpsk',
                 dest='ms',
                 help='set modulation scheme: ' + enumHelp(dragonradio.ModulationScheme))
    add_argument('-c', '--fec0',
                 action='store', type=dragonradio.FECScheme,
                 default='v29',
                 dest='fec0',
                 help='set inner FEC: ' + enumHelp(dragonradio.FECScheme))
    add_argument('-k', '--fec1',
                 action='store', type=dragonradio.FECScheme,
                 default='rs8',
                 dest='fec1',
                 help='set outer FEC: ' + enumHelp(dragonradio.FECScheme))
    add_argument('-r', '--check',
                 action='store', type=dragonradio.CRCScheme,
                 default='crc32',
                 dest='check',
                 help='set data validity check: ' + enumHelp(dragonradio.CRCScheme))
    add_argument('--arq', action='store_const', const=True,
                 default=False,
                 dest='arq',
                 help='enable ARQ')

class Radio(object):
    def __init__(self):
        self.config = Config()
        self.logger = None

    def loadConfig(self, path):
        try:
            with io.open(path) as f:
                config = libconf.load(f)
            self.config.__dict__.update(config)
        except:
            logger.exception('Cannot load radio.conf')

    def loadArgs(self, args):
        keys = list(args.__dict__.keys())
        for key in keys:
            if args.__dict__[key] == None:
                del args.__dict__[key]

        self.config.__dict__.update(args.__dict__)

    def loadColosseumIni(self, path):
        try:
            config = configparser.ConfigParser()
            config.read(path)

            if 'COLLABORATION' in config:
                for key in config['COLLABORATION']:
                    self.config.__dict__[key] = config['COLLABORATION'][key]

            if 'RF' in config:
                for key in config['RF']:
                    self.config.__dict__[key] = float(config['RF'][key])
        except:
            logger.exception('Cannot load colosseum_config.ini')

    def setup(self):
        if hasattr(self.config, 'node_id') and self.config.node_id:
            self.node_id = self.config.node_id
        else:
            self.node_id = getNodeIdFromHostname()

        # Copy configuration settings to the C++ RadioConfig object
        self.config.fixEnums()

        for attr in ['verbose', 'soft_tx_gain', 'ms' ,'check', 'fec0', 'fec1']:
            if hasattr(self.config, attr):
                setattr(dragonradio.rc, attr, getattr(self.config, attr))

        # Create the USRP
        self.usrp = dragonradio.USRP(self.config.addr,
                                     self.config.frequency,
                                     self.config.tx_antenna,
                                     self.config.rx_antenna,
                                     self.config.tx_gain,
                                     self.config.rx_gain)

        # Create the logger *after* we create the USRP so that we have a global
        # clock
        if hasattr(self.config, 'logfile') and self.config.logfile:
            self.logger = dragonradio.Logger(self.config.logfile)
            self.logger.setAttribute('node_id', self.node_id)
            self.logger.setAttribute('frequency', self.config.frequency)
            self.logger.setAttribute('soft_tx_gain', self.config.soft_tx_gain)
            self.logger.setAttribute('tx_gain', self.config.tx_gain)
            self.logger.setAttribute('rx_gain', self.config.rx_gain)
            self.logger.setAttribute('M', self.config.M)
            self.logger.setAttribute('cp_len', self.config.cp_len)
            self.logger.setAttribute('taper_len', self.config.taper_len)

            dragonradio.Logger.singleton = self.logger

        #
        # Configure the PHY
        #
        if self.config.phy == 'flexframe':
            self.phy = dragonradio.FlexFrame(self.config.min_packet_size)
        elif self.config.phy == 'ofdm':
            self.phy = dragonradio.OFDM(self.config.M,
                                        self.config.cp_len,
                                        self.config.taper_len,
                                        self.config.min_packet_size)
        elif self.config.phy == 'multiofdm':
            self.phy = dragonradio.MultiOFDM(self.config.M,
                                             self.config.cp_len,
                                             self.config.taper_len,
                                             self.config.min_packet_size)
        else:
            fail('Bad PHY: {}'.format(self.config.phy))

        #
        # Create tun/tap interface and net neighborhood
        #
        self.tuntap = dragonradio.TunTap('tap0', False, 1500, '10.10.10.%d', 'c6:ff:ff:ff:ff:%02x', self.node_id)

        self.net = dragonradio.Net(self.tuntap, self.node_id)

        #
        # Configure the modulator and demodulator
        #
        self.modulator = dragonradio.ParallelPacketModulator(self.net,
                                                             self.phy,
                                                             self.config.num_modulation_threads)

        self.demodulator = dragonradio.ParallelPacketDemodulator(self.net,
                                                                 self.phy,
                                                                 self.config.num_demodulation_threads)

        #
        # Configure the controller
        #
        if self.config.arq:
            self.controller = dragonradio.SmartController(self.net,
                                                          self.config.arq_window,
                                                          self.config.arq_window)
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
        # If we are using a SmartController, tell it that the network queue is a
        # splice queue so that it can splice packets at the front of the queue.
        #
        if self.config.arq:
            self.controller.splice_queue = self.netq

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
