import configparser
import io
import libconf
import platform
import re
import sys

import dragonradio

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
        pass

class Radio(object):
    def __init__(self):
        self.config = Config()
        self.logger = None
        self.logger_finished = False

    def loadConfig(self, path):
        # Read the radio configuration
        with io.open(path) as f:
            config = libconf.load(f)

        self.config.__dict__.update(config)
        self.config.ms = dragonradio.ModulationScheme(self.config.ms)
        self.config.check = dragonradio.CRCScheme(self.config.check)
        self.config.fec0 = dragonradio.FECScheme(self.config.fec0)
        self.config.fec1 = dragonradio.FECScheme(self.config.fec1)

        # Read the colosseum configuration
        colosseum_config = configparser.ConfigParser()
        colosseum_config.read(config.colosseum_config_path)

        for key in colosseum_config['RF']:
            self.config.__dict__[key] = float(colosseum_config['RF'][key])

        for key in colosseum_config['COLLABORATION']:
            self.config.__dict__[key] = colosseum_config['COLLABORATION'][key]

    def loadArgs(self, args):
        self.config.__dict__.update(args.__dict__)

    def setup(self):
        if hasattr(self.config, 'node_id') and self.config.node_id:
            self.node_id = self.config.node_id
        else:
            self.node_id = getNodeIdFromHostname()

        # Copy configuration settings to the C++ RadioConfig object
        dragonradio.rc.verbose = self.config.verbose
        dragonradio.rc.soft_txgain = self.config.soft_tx_gain
        dragonradio.rc.ms = self.config.ms
        dragonradio.rc.check = self.config.check
        dragonradio.rc.fec0 = self.config.fec0
        dragonradio.rc.fec1 = self.config.fec1

        # Create the USRP
        self.usrp = dragonradio.USRP(self.config.addr,
                                     self.config.frequency,
                                     self.config.tx_antenna,
                                     self.config.rx_antenna,
                                     self.config.tx_gain,
                                     self.config.rx_gain)

        # Create the logger *after* we create the USRP so that we have a global
        # clock
        if self.config.logfile:
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
        self.finishLogger()

    def configureTDMA(self, nslots):
        self.mac = dragonradio.TDMA(self.usrp,
                                    self.phy,
                                    self.modulator,
                                    self.demodulator,
                                    self.config.bandwidth,
                                    self.config.slot_size,
                                    self.config.guard_size,
                                    nslots)
        self.finishLogger()

    def finishLogger(self):
        if self.logger and not self.logger_finished:
            self.logger.setAttribute('tx_bandwidth', self.usrp.tx_rate)
            self.logger.setAttribute('rx_bandwidth', self.usrp.rx_rate)
            self.logger_finished = True
