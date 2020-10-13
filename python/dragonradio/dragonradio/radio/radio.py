# Copyright 2018-2020 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

"""Radio class for managing the radio."""
import asyncio
import ipaddress
import logging
import math
import os
import random
import signal

import numpy as np

try:
  from _dragonradio.radio import *
  from _dragonradio.logging import Logger, EventCategory, setLogLevel, setPrintLogLevel
except:
  pass

import dragonradio.channels
from dragonradio.liquid import MCS # pylint: disable=no-name-in-module
import dragonradio.schedule
import dragonradio.signal
import dragonradio.tasks

from .version import version as __version__

logger = logging.getLogger('radio')

_MACS = { 'aloha': True
        , 'tdma': True
        , 'tdma-fdma': True
        , 'fdma': False
        }

def _isSlottedMAC(mac):
    return _MACS.get(mac, ValueError("Unknown MAC %s", mac))

class Radio(dragonradio.tasks.TaskManager):
    """Radio configuration, setup, and maintenance"""
    # pylint: disable=too-many-public-methods
    # pylint: disable=too-many-instance-attributes
    # pylint: disable=no-member

    def __init__(self, config, mac, loop=None):
        if loop is None:
            loop = asyncio.get_event_loop()

        super().__init__(loop)

        logger.info('Radio version: %s', __version__)
        logger.info('Radio configuration:\n%s', str(config))

        self.config = config
        """Config object for radio"""

        self.node_id = config.node_id
        """This node's ID"""

        self.logger = None
        """Our DragonRadio logger"""

        self.lock = asyncio.Lock()
        """Lock protecting radio configuration"""

        # Set the TX and RX rates to None to ensure they are properly set
        # everywhere by setTXRate and setRXRate the first time those two
        # functions are called.
        self.tx_rate = None
        """Current TX rate. None if not yet set."""

        self.rx_rate = None
        """Current RX rate. None if not yet set."""

        self.tx_channel_idx = 0
        """Default TX channel index"""

        self.mac = None
        """The radio's MAC"""

        self.mac_schedule = None
        """Our MAC schedule"""

        self.channels = []
        """Channels"""

        # Copy configuration to RadioConfig
        self.configureRadioConfig()

        # Add global work queue workers
        work_queue.addThreads(1)

        # Initialize USRP
        self.configureUSRP()

        # Configure valid decimation rates
        self.configureValidDecimationRates()

        # Create the logger *after* we create the USRP so that we have a global
        # clock
        self.configureLogging()

        # Configure snapshots
        self.configureSnapshots()

        # Create the PHY
        self.phy = self.mkPHY(self.header_mcs, self.mcs_table)

        # Configure channelizer
        self.channelizer = self.mkChannelizer()

        # Configure synthesizer
        self.synthesizer = self.mkSynthesizer(_isSlottedMAC(mac))

        # Hook up the radio components
        self.configureComponents()

        # If we are in TDMA mode, set channel bandwidth to None so we use a
        # single channel. After this, we must re-configure our channels.
        if mac == 'tdma':
            config.channel_bandwidth = None

        # Configure channels
        self.configureDefaultChannels()

    def __del__(self):
        if self.logger:
            self.logger.close()

    def start(self, user_ns=locals()):
        """Start the radio"""
        # Collect snapshots if requested
        if self.config.snapshot_period is not None:
            self.startSnapshots()

        # Add radio nodes to the network if number of nodes was specified
        if self.config.num_nodes is not None:
            for i in range(0, self.config.num_nodes):
                self.net.addNode(i+1)

        # Configure the MAC
        self.configureMAC(self.config.mac)

        # Either start the interactive loop or run the loop ourselves
        if self.config.interactive:
            import IPython.terminal.embed
            from traitlets.config import Config
            c = Config()

            c.TerminalInteractiveShell.loop_runner = 'asyncio'
            c.TerminalInteractiveShell.autoawait = True

            user_ns['radio'] = self

            shell = IPython.terminal.embed.InteractiveShellEmbed(config=c, user_ns=user_ns)
            shell.enable_gui('asyncio')
            shell()

            self.stop()
        else:
            for sig in [signal.SIGINT, signal.SIGTERM]:
                self.loop.add_signal_handler(sig, self.stop)

            try:
                self.loop.run_forever()
            finally:
                self.loop.close()

        return 0

    def stop(self):
        """Stop the radio and all associated tasks"""
        self.loop.create_task(self._stop())

    async def _stop(self):
        """Task to stop the radio and all associated tasks"""
        # Stop radio tasks
        await self.stopTasks()

        # Wait for remaining tasks and stop the event loop
        await dragonradio.tasks.stopEventLoop(self.loop, logger)

    def configureRadioConfig(self):
        """Configure the singleton RadioConfig object"""
        # Make sure RadioConfig has node id
        rc.node_id = self.node_id

    def configureUSRP(self):
        """Construct USRP object from configuration parameters"""
        config = self.config

        # Create the USRP
        self.usrp = USRP(config.addr,
                         config.tx_subdev,
                         config.rx_subdev,
                         self.frequency,
                         config.tx_antenna,
                         config.rx_antenna,
                         config.tx_gain,
                         config.rx_gain)

        # Set USRP clock and time sources. If they were not specified in the
        # configuration, we leave the default setting as-is.
        if config.clock_source is not None:
            self.usrp.clock_source = config.clock_source

        if config.time_source is not None:
            self.usrp.time_source = config.time_source

        self.usrp.rx_max_samps_factor = config.rx_max_samps_factor
        self.usrp.tx_max_samps_factor = config.tx_max_samps_factor

    def configureLogging(self):
        """Configure radio logging"""
        config = self.config

        if config.logdir:
            path = self.getRadioLogPath()

            self.logger = Logger(path)
            self.logger.setAttribute('version', __version__)
            self.logger.setAttribute('node_id', self.node_id)
            self.logger.setAttribute('config', str(config))

            if hasattr(config, 'log_sources'):
                for source in config.log_sources:
                    setattr(self.logger, source, True)

            for cat in EventCategory.__members__.keys():
                setLogLevel(cat, logging.DEBUG)
                setPrintLogLevel(cat, config.loglevel)

            if config.verbose_packet_trace:
                setPrintLogLevel('NET', logging.DEBUG-1)
                setPrintLogLevel('TUNTAP', logging.DEBUG-1)

            Logger.singleton = self.logger

        PHY.log_invalid_headers = config.log_invalid_headers

    def configureSnapshots(self):
        """Configure snapshots"""
        config = self.config

        if config.snapshot_period is not None:
            self.snapshot_collector = SnapshotCollector()
        else:
            self.snapshot_collector = None

        # Make sure RadioConfig has snapshot collector
        rc.snapshot_collector = self.snapshot_collector

    def configureComponents(self):
        """Hook up all the radio components"""
        # pylint: disable=pointless-statement
        config = self.config

        # Create object representing internal and external IP networks
        int_net = ipaddress.IPv4Network(config.internal_net)
        ext_net = ipaddress.IPv4Network(config.external_net)

        # Create tun/tap interface and net neighborhood
        self.tuntap = TunTap(config.tap_iface,
                             config.tap_ipaddr,
                             str(int_net.netmask),
                             config.tap_macaddr,
                             False,
                             config.mtu,
                             self.node_id)

        self.net = Net(self.tuntap, self.node_id)

        # Configure the controller
        self.controller = self.mkController(self.evm_thresholds)

        #
        # Create flow performance measurement component
        #
        self.flowperf = FlowPerformance(config.measurement_period)

        #
        # Create packet compression component
        #
        self.packet_compressor = PacketCompressor(config.packet_compression,
                                                  int(int_net.network_address),
                                                  int(int_net.netmask),
                                                  int(ext_net.network_address),
                                                  int(ext_net.netmask))

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
        #   tun/tap -> NetFilter -> FlowPerformance.net -> NetFirewall ->
        #       PacketCompressor.net -> NetQueue -> controller -> synthesizer
        #
        self.netfilter = NetFilter(self.net,
                                   int(int_net.network_address),
                                   int(int_net.netmask),
                                   int(int_net.broadcast_address),
                                   int(ext_net.network_address),
                                   int(ext_net.netmask),
                                   int(ext_net.broadcast_address))
        self.netfirewall = NetFirewall()
        self.netq = self.mkNetQueue()

        self.tuntap.source >> self.netfilter.input

        self.netfilter.output >> self.flowperf.net_in

        self.flowperf.net_out >> self.netfirewall.input

        self.netfirewall.output >> self.packet_compressor.net_in

        self.packet_compressor.net_out >> self.netq.push

        self.netq.pop >> self.controller.net_in

        self.controller.net_out >> self.synthesizer.sink

        # Allow Controller access to the network queue
        self.controller.net_queue = self.netq

    def mkPHY(self, header_mcs, mcs_table):
        """Construct a PHY from configuration parameters"""
        config = self.config

        if config.phy == 'flexframe':
            phy = dragonradio.liquid.FlexFrame(header_mcs,
                                               mcs_table,
                                               config.soft_header,
                                               config.soft_payload)
        elif config.phy == 'newflexframe':
            phy = dragonradio.liquid.NewFlexFrame(header_mcs,
                                                  mcs_table,
                                                  config.soft_header,
                                                  config.soft_payload)
        elif config.phy == 'ofdm':
            phy = dragonradio.liquid.OFDM(header_mcs,
                                          mcs_table,
                                          config.soft_header,
                                          config.soft_payload,
                                          config.M,
                                          config.cp_len,
                                          config.taper_len,
                                          config.subcarriers)
        else:
            raise ValueError('Unknown PHY: %s' % config.phy)

        return phy

    def mkChannelizer(self):
        """Construct a Channelizer according to configuration parameters"""
        config = self.config

        if config.channelizer == 'overlap':
            channelizer = OverlapTDChannelizer(self.phy,
                                               self.usrp.rx_rate,
                                               Channels([]),
                                               config.num_demodulation_threads)

            channelizer.enforce_ordering = config.channelizer_enforce_ordering
        elif config.channelizer == 'timedomain':
            channelizer = TDChannelizer(self.phy,
                                        self.usrp.rx_rate,
                                        Channels([]),
                                        config.num_demodulation_threads)
        elif config.channelizer == 'freqdomain':
            channelizer = FDChannelizer(self.phy,
                                        self.usrp.rx_rate,
                                        Channels([]),
                                        config.num_demodulation_threads)
        else:
            raise ValueError('Unknown channelizer: %s' % config.channelizer)

        return channelizer

    def mkSynthesizer(self, slotted):
        """Construct a Synthesizer according to configuration parameters"""
        config = self.config

        if slotted:
            if config.synthesizer == 'timedomain':
                synthesizer = TDSlotSynthesizer(self.phy,
                                                self.usrp.tx_rate,
                                                Channels([]),
                                                config.num_modulation_threads)
            elif config.synthesizer == 'freqdomain':
                synthesizer = FDSlotSynthesizer(self.phy,
                                                self.usrp.tx_rate,
                                                Channels([]),
                                                config.num_modulation_threads)
            elif config.synthesizer == 'multichannel':
                synthesizer = MultichannelSynthesizer(self.phy,
                                                      self.usrp.tx_rate,
                                                      Channels([]),
                                                      config.num_modulation_threads)
            else:
                raise ValueError('Unknown synthesizer: %s' % config.synthesizer)
        else:
            if config.synthesizer == 'timedomain':
                synthesizer = TDSynthesizer(self.phy,
                                            self.usrp.tx_rate,
                                            Channels([]),
                                            config.num_modulation_threads)
            elif config.synthesizer == 'freqdomain':
                synthesizer = FDSynthesizer(self.phy,
                                            self.usrp.tx_rate,
                                            Channels([]),
                                            config.num_modulation_threads)
            elif config.synthesizer == 'multichannel':
                raise ValueError('Multichannel synthesizer can only be used with a slotted MAC')
            else:
                raise ValueError('Unknown synthesizer: %s' % config.synthesizer)

        return synthesizer

    def mkController(self, evm_thresholds):
        """Construct a Controller according to configuration parameters"""
        config = self.config

        if config.arq:
            controller = SmartController(self.net,
                                         # Add MCU to MTU
                                         config.mtu + config.arq_mcu,
                                         self.phy,
                                         config.slot_size,
                                         config.arq_window,
                                         config.arq_window,
                                         evm_thresholds)

            # ARQ parameters
            controller.enforce_ordering = config.arq_enforce_ordering
            controller.max_retransmissions = config.arq_max_retransmissions
            controller.ack_delay = config.arq_ack_delay
            controller.ack_delay_estimation_window = config.arq_ack_delay_estimation_window
            controller.retransmission_delay = config.arq_retransmission_delay
            controller.min_retransmission_delay = config.arq_min_retransmission_delay
            controller.retransmission_delay_slop = config.arq_retransmission_delay_slop
            controller.sack_delay = config.arq_sack_delay
            controller.explicit_nak_window = config.arq_explicit_nak_win
            controller.explicit_nak_window_duration = config.arq_explicit_nak_win_duration
            controller.selective_ack = config.arq_selective_ack
            controller.selective_ack_feedback_delay = config.arq_selective_ack_feedback_delay
            controller.move_along = config.arq_move_along
            controller.decrease_retrans_mcsidx = config.arq_decrease_retrans_mcsidx
            controller.broadcast_gain.dB = config.arq_broadcast_gain_db
            controller.ack_gain.dB = config.arq_ack_gain_db

            # AMC parameters
            controller.short_per_window = config.amc_short_per_window
            controller.long_per_window = config.amc_long_per_window
            controller.long_stats_window = config.amc_long_stats_window
            if config.amc_mcsidx_broadcast is not None:
                controller.mcsidx_broadcast = config.amc_mcsidx_broadcast
            if config.amc_mcsidx_ack is not None:
                controller.mcsidx_ack = config.amc_mcsidx_ack
            if config.amc_mcsidx_min is not None:
                controller.mcsidx_min = config.amc_mcsidx_min
            if config.amc_mcsidx_max is not None:
                controller.mcsidx_max = config.amc_mcsidx_max
            controller.mcsidx_init = config.amc_mcsidx_init
            controller.mcsidx_up_per_threshold = config.amc_mcsidx_up_per_threshold
            controller.mcsidx_down_per_threshold = config.amc_mcsidx_down_per_threshold
            controller.mcsidx_alpha = config.amc_mcsidx_alpha
            controller.mcsidx_prob_floor = config.amc_mcsidx_prob_floor

        else:
            controller = DummyController(self.net, config.mtu)

        return controller

    def mkNetQueue(self):
        """Construct a network queue according to configuration parameters"""
        config = self.config

        if config.queue == 'fifo':
            netq = SimpleQueue(SimpleQueue.FIFO)
        elif config.queue == 'lifo':
            netq = SimpleQueue(SimpleQueue.LIFO)
        elif config.queue == 'mandate':
            netq = MandateQueue()
            netq.bonus_phase = config.mandate_bonus_phase
        else:
            raise ValueError('Unknown queue type: %s' % config.queue)

        netq.transmission_delay = config.transmission_delay

        return netq

    def mkAutoGain(self):
        """Construct an AutoGain object according to configuration parameters"""
        config = self.config

        autogain = AutoGain()

        autogain.soft_tx_gain_0dBFS = config.soft_tx_gain
        if config.auto_soft_tx_gain is not None:
            autogain.recalc0dBFSEstimate(config.auto_soft_tx_gain)
            autogain.auto_soft_tx_gain_clip_frac = config.auto_soft_tx_gain_clip_frac

        return autogain

    def configureDefaultChannels(self):
        """Configure default channels"""
        config = self.config

        bandwidth = self.bandwidth

        if config.channel_bandwidth is None:
            cbw = bandwidth
        else:
            cbw = config.channel_bandwidth

        channels = dragonradio.channels.defaultChannelPlan(bandwidth, cbw)

        logger.debug(("Channels: %s "
                      "(bandwidth=%g; "
                      "rx_oversample=%d; "
                      "tx_oversample=%d; "
                      "channel bandwidth=%g)"),
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
        self.channels = channels[:self.config.max_channels]

        # Initialize RX chain
        self.setRXChannels(channels)

        # Initialize TX chain
        self.setTXChannels(channels)

        # Reconfigure the MAC
        if self.mac is not None:
            self.mac.reconfigure()

    def setRXChannels(self, channels):
        """Configure RX chain for channels"""
        # Initialize channelizer
        self.setRXRate(self.bandwidth)

        # We need to do this *after* setting the RX rate because it is used to
        # determine filter parameters
        self.setChannelizerChannels(channels)

    def setTXChannels(self, channels):
        """Configure TX chain for channels"""
        if self.config.tx_upsample:
            self.setTXRate(self.bandwidth)
            self.setSynthesizerChannels(channels)
        else:
            self.setTXChannel(self.tx_channel_idx)

    def setChannelizerChannels(self, channels):
        """Set channelizer's channels."""
        self.channelizer.channels = \
            Channels([(chan, self.genChannelizerTaps(chan)) for chan in channels])

    def setSynthesizerChannels(self, channels):
        """Set synthesizer's channels."""
        self.synthesizer.channels = \
            Channels([(chan, self.genSynthesizerTaps(chan)) for chan in channels])

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

        logger.debug('Desired decimation rate: %g', dec_rate)

        # Otherwise, make sure we use a safe decimation rate
        if dec_rate != 1:
            for rate in self.valid_rates:
                if dec_rate >= rate:
                    dec_rate = rate
                    break

        logger.debug('Actual decimation rate: %g', dec_rate)

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
                raise ValueError('Wanted RX rate %g, but got %g' % (want_rx_rate, self.rx_rate))

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
                raise ValueError('Wanted TX rate %g, but got %g' % (want_tx_rate, self.tx_rate))

            self.synthesizer.tx_rate = self.tx_rate

    def setTXChannel(self, channel_idx):
        """Set the transmission channel.

        If we are upsampling on TX, this is a no-op. Otherwise we configure the
        radio's frequency and bandwidth and synthesizer for the new, single
        channel.
        """
        config = self.config

        if config.tx_upsample:
            logger.warning('Attempt to set TX channel when upsampling')
        else:
            # Determine TX channel from index
            self.tx_channel_idx = min(channel_idx, len(self.channels) - 1)
            channel = self.channels[self.tx_channel_idx]

            # Set TX rate
            self.setTXRate(channel.bw)

            # Set TX frequency
            logger.info("Setting TX frequency offset to %g", channel.fc)
            self.usrp.tx_frequency = self.frequency + channel.fc

            # Set synthesizer channel
            self.setSynthesizerChannels([Channel(0, channel.bw)])

            # Allow the MAC to figure out the TX offset so snapshot self
            # tranmissions are correctly logged
            if self.mac is not None:
                self.mac.reconfigure()

    def reconfigureBandwidthAndFrequency(self, bandwidth, frequency):
        """Reconfigure the radio for the given bandwidth and frequency"""
        config = self.config

        if bandwidth == config.bandwidth and frequency == config.frequency:
            return

        logger.info("Reconfiguring radio: bandwidth=%f, frequency=%f", bandwidth, frequency)

        # Set current frequency
        config.frequency = frequency

        self.usrp.rx_frequency = self.frequency

        # If we are upsampling on TX, set TX frequency. Otherwise the call to
        # setTXChannel below will set the appropriate TX frequency.
        if config.tx_upsample:
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
        if isinstance(self.controller, SmartController):
            self.controller.resetMCSTransitionProbabilities()

    def genChannelizerTaps(self, channel):
        """Generate channelizer filter taps for given channel"""
        config = self.config

        # Calculate channelizer taps
        if channel.bw == self.usrp.rx_rate:
            return [1]

        if config.channelizer == 'freqdomain':
            wp = 0.95*channel.bw
            ws = channel.bw
            fs = self.usrp.rx_rate

            h = dragonradio.signal.lowpass(wp, ws, fs, ftype='firpm1f2', Nmax=FDChannelizer.P)
        else:
            wp = 0.9*channel.bw
            ws = 1.1*channel.bw
            fs = self.usrp.rx_rate

            h = dragonradio.signal.lowpass(wp, ws, fs)

        logger.debug('Created prototype lowpass filter for channelizer: N=%d; wp=%g; ws=%g; fs=%g',
                     len(h), wp, ws, fs)
        return h

    def genSynthesizerTaps(self, channel):
        """Generate synthesizer filter taps for given channel"""
        config = self.config

        if channel.bw == self.usrp.tx_rate:
            return [1]

        if config.synthesizer == 'freqdomain' or config.synthesizer == 'multichannel':
            # Frequency-space synthesizers don't apply a filter
            return [1]

        wp = 0.9*channel.bw
        ws = 1.1*channel.bw
        fs = self.usrp.tx_rate

        h = dragonradio.signal.lowpass(wp, ws, fs)

        logger.debug('Created prototype lowpass filter for synthesizer: N=%d; wp=%g; ws=%g; fs=%g',
                     len(h), wp, ws, fs)
        return h

    def configureMAC(self, mac):
        """Configure MAC"""
        if mac == 'aloha':
            self.configureALOHA()
        elif mac == 'tdma':
            self.configureSimpleMACSchedule()
        elif mac == 'tdma-fdma':
            self.configureSimpleMACSchedule()
        elif mac == 'fdma':
            self.configureSimpleMACSchedule(fdma_mac=True)
        else:
            raise ValueError("Unknown MAC: {}".format(mac))

    def deleteMAC(self):
        """Delete the current MAC"""
        if self.mac is not None:
            self.mac.stop()
            self.mac = None

    def configureALOHA(self):
        """Configure ALOHA MAC"""
        config = self.config

        self.mac = SlottedALOHA(self.usrp,
                                self.phy,
                                self.controller,
                                self.snapshot_collector,
                                self.channelizer,
                                self.synthesizer,
                                config.slot_size,
                                config.guard_size,
                                config.slot_send_lead_time,
                                config.aloha_prob)

        # Install slot-per-channel schedule for ALOHA MAC
        self.installALOHASchedule()

        # We may not use superslots with the ALOHA MAC
        self.synthesizer.superslots = False

        # Set up overlap channelizer
        if isinstance(self.channelizer, OverlapTDChannelizer):
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

        if isinstance(self.mac, TDMA) and self.mac.nslots == nslots:
            return

        # Replace the synthesizer if it is not a SlotSynthesizer
        if not isinstance(self.synthesizer, SlotSynthesizer):
            self.replaceSynthesizer(False)

        # Replace the MAC
        self.deleteMAC()

        self.mac = TDMA(self.usrp,
                        self.phy,
                        self.controller,
                        self.snapshot_collector,
                        self.channelizer,
                        self.synthesizer,
                        config.slot_size,
                        config.guard_size,
                        config.slot_send_lead_time,
                        nslots)

        # We may use superslots with the TDMA MAC
        self.synthesizer.superslots = config.superslots

        # Set up overlap channelizer
        if isinstance(self.channelizer, OverlapTDChannelizer):
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

    def configureFDMA(self):
        """Configures a FDMA MAC."""
        config = self.config

        if isinstance(self.mac, FDMA):
            return

        # Replace the synthesizer if it is not a ChannelSynthesizer
        if not isinstance(self.synthesizer, ChannelSynthesizer):
            self.replaceSynthesizer(False)

        # Replace the MAC
        self.deleteMAC()

        self.mac = FDMA(self.usrp,
                        self.phy,
                        self.controller,
                        self.snapshot_collector,
                        self.channelizer,
                        self.synthesizer,
                        config.slot_size)

        self.finishConfiguringMAC()

    def finishConfiguringMAC(self):
        """Finish configuring MAC"""
        bws = [chan.bw for (chan, _taps) in self.synthesizer.channels]
        if len(bws) != 0:
            self.mac.min_channel_bandwidth = min(bws)

    def replaceSynthesizer(self, slotted):
        """Replace the synthesizer"""
        # pylint: disable=pointless-statement

        # Disconnect synthesizer. When the controller is disconnected from the
        # old synthesizer, it disconnects itself from the upstream queue, so we
        # must re-reconnect the queue to the controller too. We go ahead and
        # disconnect everything here to prevent issues with disconnect/connect
        # order during re-connection. The network queue is disconnected first to
        # ensure we don't lose any network packets during the transition.
        self.netq.pop.disconnect()
        self.controller.net_in.disconnect()

        self.controller.net_out.disconnect()
        self.synthesizer.sink.disconnect()

        # Replace synthesizer
        self.synthesizer = self.mkSynthesizer(slotted)

        # Reconnect the controller to the synthesizer.
        self.netq.pop >> self.controller.net_in

        self.controller.net_out >> self.synthesizer.sink

        # Re-configure TX chain, which includes synthesizer
        self.setTXChannels(self.channels)

    def setALOHAChannel(self, channel_idx):
        """Set the transmission channel for the ALOHA MAC."""
        if not isinstance(self.mac, SlottedALOHA):
            logger.debug("Cannot change ALOHA channel for non-ALOHA MAC")

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

        # All nodes can transmit
        for (_node_id, node) in self.net.nodes.items():
            node.can_transmit = True

        if self.config.tx_upsample:
            self.mac_schedule = np.identity(len(self.channels)).astype('bool')
        else:
            self.setTXChannel(0)

            self.mac_schedule = [[1]]

        self.mac.schedule = self.mac_schedule
        self.synthesizer.schedule = self.mac_schedule

    def installMACSchedule(self, sched, fdma_mac=False):
        """Install a MAC schedule.

        Args:
            sched: The schedule, which is a nchannels X nslots array of node
                IDs.
            fdma_mac: If True, use the FDMA MAC
        """
        config = self.config

        logger.debug('Installing MAC schedule:\n%s', sched)

        # Get number of channels and slots
        (_nchannels, nslots) = sched.shape

        # First configure the TDMA MAC for the desired number of slots
        if fdma_mac:
            if nslots != 1:
                raise ValueError("FDMA schedule has more than one slot: %s" % sched)
            self.configureFDMA()
        else:
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
                chan = dragonradio.schedule.bestScheduleChannel(sched, self.node_id)
            except ValueError:
                logger.error('No MAC schedule entry for radio %d', self.node_id)
                chan = 0

            self.setTXChannel(chan)

            self.mac_schedule = [sched[chan] == self.node_id]

        self.mac.schedule = self.mac_schedule
        self.synthesizer.schedule = self.mac_schedule

    def configureSimpleMACSchedule(self, fdma_mac=False):
        """Set a simple static schedule."""
        nchannels = len(self.channels)
        nodes = sorted(list(self.net.nodes))

        if nchannels == 1:
            sched = dragonradio.schedule.pureTDMASchedule(nodes)
        else:
            sched = dragonradio.schedule.fullChannelMACSchedule(nchannels,
                                                                1,
                                                                nodes,
                                                                3)

        self.installMACSchedule(sched, fdma_mac=fdma_mac)

    def synchronizeClock(self):
        """Use timestamps to synchronize our clock with the time master (the gateway)"""
        config = self.config

        if self.net.time_master is None:
            return

        t0 = clock.t0

        # Perform linear regression on all timestamps
        echoed = _relativizeTimestamps(t0, self.controller.echoed_timestamps)
        logger.debug("TIMESYNC: echoed timestamps: %s", echoed)
        if len(echoed) == 0:
            return

        master = _relativizeTimestamps(t0, self.net.nodes[self.net.time_master].timestamps)
        logger.debug("TIMESYNC: time master's timestamps: %s", master)
        if len(master) == 0:
            return

        if len(echoed) > 1 and len(master) > 1:
            # If we have a GPSDO, then assume skew is zero
            if config.clock_noskew or \
                (self.usrp.clock_source == 'external' and self.usrp.time_source == 'external'):
                (sigma, delta, tau) = timestampRegressionNoSkew(echoed, master)
            else:
                (sigma, delta, tau) = timestampRegression(echoed, master)

            old_sigma = clock.skew
            old_delta = clock.offset.secs

            logger.debug(("TIMESYNC: regression parameters: "
                          "old_sigma=%g; "
                          "old_delta=%g; "
                          "sigma=%g; "
                          "delta=%g; "
                          "tau=%g"),
                old_sigma, old_delta, sigma, delta, tau)

            if math.isfinite(delta) and math.isfinite(sigma):
                clock.offset = MonoTimePoint(delta)
                clock.skew = sigma
                self.logger.logEvent(("TIMESYNC: set skew and offset: "
                                      "sigma={:g}; "
                                      "delta={:g}").format(sigma, delta))

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

    def startSnapshots(self):
        """Start the snapshot logger"""
        self.createTask(self.snapshotTask(), name='snapshots')

    async def snapshotTask(self):
        """Snapshot logging task"""
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

                # Wait for remaining packets in snapshot to be demodulated and
                # get the snapshot
                if config.snapshot_finish_wait != 0:
                    await asyncio.sleep(config.snapshot_finish_wait)

                snapshot = collector.finish()

                # Log the snapshot
                if config.log_snapshots:
                    self.logger.logSnapshot(snapshot)

                await asyncio.sleep(config.snapshot_period)
        except asyncio.CancelledError:
            return

    @property
    def frequency(self):
        """Center frequency"""
        return self.config.frequency

    @property
    def bandwidth(self):
        """Bandwidth"""
        return min(self.config.bandwidth, self.config.max_bandwidth)

    @property
    def header_mcs(self):
        "Header MCS"
        return MCS(self.config.header_check,
                   self.config.header_fec0,
                   self.config.header_fec1,
                   self.config.header_ms)

    @property
    def mcs_table(self):
        """MCS table"""
        # pylint: disable=no-else-return

        config = self.config

        if config.amc and config.amc_table:
            return [(MCS(*mcs), self.mkAutoGain()) for (mcs, _thresh) in config.amc_table]
        else:
            mcs = MCS(config.check, config.fec0, config.fec1, config.ms)

            return [(mcs, self.mkAutoGain())]

    @property
    def evm_thresholds(self):
        """EVM thresholds for each MCS"""
        # pylint: disable=no-else-return

        def zeroToNone(x):
            if x != 0:
                return x

            return None

        config = self.config

        if config.amc and config.amc_table:
            # libconfig can't parse None, so we use zero to represent a
            # non-existant threshold (zero is not a valid EVM threshold)
            return [zeroToNone(thresh) for (_mcs, thresh) in config.amc_table]
        else:
            return [None for _ in self.mcs_table]

def _relativizeTimestamps(t0, ts):
    """Make (t_send, t_recv) timestamps relative to t0"""
    return [((t_send-t0).secs, (t_recv-t0).secs) for (t_send, t_recv) in ts]

def timestampRegression(echoed, master):
    """Perform a linear regression on timestamps to determine clock skew and delta"""
    # pylint: disable=too-many-locals

    avec = [a for (a, _) in echoed]
    bvec = [b for (_, b) in echoed]

    cvec = [c for (c, _) in master]
    dvec = [d for (_, d) in master]

    abar = np.mean(avec)
    bbar = np.mean(bvec)

    cbar = np.mean(cvec)
    dbar = np.mean(dvec)

    covab = sum([(a - abar)*(b - bbar) for (a, b) in echoed])
    vara = sum([(a - abar)**2.0 for a in avec])

    covcd = sum([(c - cbar)*(d - dbar) for (c, d) in master])
    vard = sum([(d - dbar)**2.0 for d in dvec])

    sigma = (covab + covcd)/(vara + vard)

    delta_plus_tau = bbar - sigma*abar
    delta_minus_tau = cbar - sigma*dbar

    delta = (delta_plus_tau + delta_minus_tau) / 2.0
    tau = (delta_plus_tau - delta_minus_tau) / 2.0

    return (sigma, delta, tau)

def timestampRegressionNoSkew(echoed, master):
    """Perform a linear regression on timestamps to determine clock delta (assuming no skew)"""
    avec = [a for (a, _) in echoed]
    bvec = [b for (_, b) in echoed]

    cvec = [c for (c, _) in master]
    dvec = [d for (_, d) in master]

    abar = np.mean(avec)
    bbar = np.mean(bvec)

    cbar = np.mean(cvec)
    dbar = np.mean(dvec)

    delta_plus_tau = bbar - abar
    delta_minus_tau = cbar - dbar

    delta = (delta_plus_tau + delta_minus_tau) / 2.0
    tau = (delta_plus_tau - delta_minus_tau) / 2.0

    return (1.0, delta, tau)
