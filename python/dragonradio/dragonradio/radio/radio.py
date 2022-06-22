# Copyright 2018-2022 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

"""Radio class for managing the radio."""
import asyncio
from functools import cached_property
import ipaddress
import logging
import math
import os
import random
import signal
from typing import Any, Callable, Dict, List, Optional, Sequence, Tuple, Type

from ipykernel.kernelapp import IPKernelApp
import numpy as np

try:
  from _dragonradio.radio import *
  from _dragonradio.logging import Logger, EventCategory, setLogLevel, setPrintLogLevel
except:
  pass

import dragonradio.channels
from dragonradio.liquid import MCS # pylint: disable=no-name-in-module
import dragonradio.net
from dragonradio.radio.config import Config, ConfigException, str2mac
import dragonradio.radio.timesync as timesync
import dragonradio.schedule
from dragonradio.schedule import Schedule
import dragonradio.signal
import dragonradio.tasks

from .version import version as __version__

logger = logging.getLogger('radio')

class Radio(dragonradio.tasks.TaskManager, NeighborhoodListener):
    """Radio configuration, setup, and maintenance"""

    config: Config
    """Config object for radio"""

    node_id: int
    """This node's ID"""

    logger: Optional[Logger] = None
    """Our DragonRadio logger"""

    lock: asyncio.Lock
    """Lock protecting radio configuration"""

    _tx_channel_idx: int = 0
    """TX channel index"""

    phy: PHY = None
    """The radio's PHY"""

    mac: MAC = None
    """The radio's MAC"""

    mac_schedule: np.ndarray
    """Our MAC schedule"""

    channels: List[Channel]
    """Radio channels"""

    channelizer: Channelizer
    """The radio's channelizer"""

    synthesizer: Synthesizer
    """The radio's synthesizer"""

    timesync : Optional[Tuple[float, float, float]] = None
    """Time synchronization parameters"""

    kernel: Optional[IPKernelApp] = None
    """IPython kernel"""

    def __init__(self, config: Config, mac: str, loop: asyncio.AbstractEventLoop=None):
        if loop is None:
            loop = asyncio.get_event_loop()

        dragonradio.tasks.TaskManager.__init__(self, loop)
        NeighborhoodListener.__init__(self)

        logger.info('Radio version: %s', __version__)
        logger.info('Radio configuration:\n%s', str(config))

        self.config = config

        # Validate node ID range
        if not (config.node_id >= 1 and config.node_id <= 254):
            raise ConfigException(f"Node ID is {config.node_id} but must be in the range [1,254].")

        self.node_id = config.node_id

        self.lock = asyncio.Lock()

        self.channels = []

        # Add global work queue workers
        work_queue.addThreads(1)

        # Initialize USRP
        self.configureUSRP()

        # Create the logger *after* we create the USRP so that we have a global
        # clock
        self.configureLogging()

        # Configure snapshots
        self.configureSnapshots()

        # Create the PHY
        PHY.team = config.team
        PHY.node_id = config.node_id

        self.phy = self.mkPHY(self.header_mcs, self.mcs_table)

        # Configure channelizer
        self.channelizer = self.mkChannelizer()

        # Configure synthesizer
        self.synthesizer = self.mkSynthesizer(str2mac(mac))

        # Hook up the radio components
        self.configureComponents()

        # If we are in TDMA mode, set channel bandwidth to None so we use a
        # single channel. After this, we must re-configure our channels.
        if mac == 'tdma':
            config.channel_bandwidth = None

        # Configure channels
        self.configureDefaultChannels()

    def __del__(self):
        if hasattr(self, 'tuntap'):
            self.tuntap.source.disconnect()
            self.tuntap.sink.disconnect()

        try:
            self.nhood.removeListener(self)
        except:
            logger.exception("Could not remove radio as neighborhood listener")

    def start(self, user_ns=locals()):
        """Start the radio"""
        # Collect snapshots if requested
        if self.config.snapshot_frequency is not None:
            self.startSnapshots()

        # Add radio nodes to the network if number of nodes was specified
        if self.config.num_nodes is not None and not self.config.manet:
            for i in range(0, self.config.num_nodes):
                self.nhood.addNeighbor(i+1)

        # Configure the MAC
        self.configureMAC(self.config.mac_class)

        # Either start the interactive loop or run the loop ourselves
        user_ns['radio'] = self

        self.run(finalizer=self.stop, user_ns=user_ns)

        return 0

    def run(self, finalizer: Callable[[], None], user_ns: Dict[str, Any]=locals()):
        """Run the radio's asyncio loop.

        Args:
            finalizer (Callable[[], None]): Finalizer function to be called on termination.
            user_ns (Dict[str, Any], optional): User namespace. Defaults to locals().

        Returns:
            int: Exit code
        """
        if self.config.kernel:
            self.kernel = IPKernelApp.instance()
            self.kernel.initialize(["python"])
            self.kernel.shell.user_ns.update(user_ns)
            self.kernel.start()
        elif self.config.interactive:
            import IPython.terminal.embed
            from traitlets.config import Config
            c = Config()

            c.TerminalInteractiveShell.loop_runner = 'asyncio'
            c.TerminalInteractiveShell.autoawait = True

            try:
                shell = IPython.terminal.embed.InteractiveShellEmbed(config=c, user_ns=user_ns)
                shell.enable_gui('asyncio')
                shell()

                finalizer()
                self.loop.run_forever()
            finally:
                self.loop.close()
        else:
            for sig in [signal.SIGINT, signal.SIGTERM, signal.SIGQUIT]:
                self.loop.add_signal_handler(sig, finalizer)

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

    async def stopTasks(self):
        # Stop any running IPython kernel
        if self.kernel is not None:
            self.kernel.close()

        await super().stopTasks()

    def neighborAdded(self, node : Node):
        dragonradio.net.addStaticARPEntry(self.tuntap.iface,
                                          self.config.tap_ipaddr % node.id,
                                          self.config.tap_macaddr % node.id)
        logging.debug("Neighbor %d added", node.id)

    def neighborRemoved(self, node : Node):
        dragonradio.net.deleteARPEntry(self.tuntap.iface,
                                       self.config.tap_ipaddr % node.id)
        logging.debug("Neighbor %d removed", node.id)

    def configureUSRP(self):
        """Construct USRP object from configuration parameters"""
        config = self.config

        # Create the USRP
        self.usrp = USRP(config.addr)

        if config.tx_subdev is not None:
            self.usrp.tx_subdev_spec = config.tx_subdev

        if config.rx_subdev is not None:
            self.usrp.rx_subdev_spec = config.rx_subdev

        self.usrp.tx_antenna = config.tx_antenna
        self.usrp.rx_antenna = config.rx_antenna

        self.usrp.tx_gain = config.tx_gain
        self.usrp.rx_gain = config.rx_gain

        self.usrp.tx_frequency = self.frequency
        self.usrp.rx_frequency = self.frequency

        # Set USRP clock and time sources. If they were not specified in the
        # configuration, we leave the default setting as-is.
        if config.clock_source is not None:
            self.usrp.clock_source = config.clock_source

        if config.time_source is not None:
            self.usrp.time_source = config.time_source

        # Synchronize USRP time with host
        self.usrp.syncTime(random_bias=config.clock_random_bias,
                           use_pps=config.clock_use_pps)

        # Set USRP as clock's time keeper
        dragonradio.radio.clock.time_keeper = self.usrp

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

            Logger.singleton = self.logger

        for cat in EventCategory.__members__.keys():
            setPrintLogLevel(cat, config.loglevel)

        if config.verbose_packet_trace:
            setPrintLogLevel('PHY', logging.DEBUG-1)
            setPrintLogLevel('NET', logging.DEBUG-1)
            setPrintLogLevel('TUNTAP', logging.DEBUG-1)

        PHY.log_invalid_headers = config.log_invalid_headers

    def configureSnapshots(self):
        """Configure snapshots"""
        config = self.config

        if config.snapshot_frequency is not None:
            if config.snapshot_frequency < config.snapshot_duration:
                raise ConfigException("Snapshot duration frequency must be no greater than snapshot frequency")

            self.snapshot_collector = SnapshotCollector()
        else:
            self.snapshot_collector = None

        # Make sure PHY has snapshot collector
        PHY.snapshot_collector = self.snapshot_collector

    def configureComponents(self):
        """Hook up all the radio components"""
        # pylint: disable=pointless-statement
        config = self.config

        # Create object representing internal and external IP networks
        int_net = ipaddress.IPv4Network((config.internal_net, config.internal_netmask))
        ext_net = ipaddress.IPv4Network((config.external_net, config.external_netmask))

        # Create tun/tap interface and net neighborhood
        self.tuntap = TunTap(config.tap_iface,
                             config.tap_ipaddr,
                             str(int_net.netmask),
                             config.tap_macaddr,
                             False,
                             config.mtu,
                             self.node_id)

        # In MANET mode, we may send packets back out the interface from whence
        # they came, so disable ICMP redirects.
        if config.manet:
            self.tuntap.accept_redirects = 0
            self.tuntap.send_redirects = 0

        # Create neighborhood and listen for events
        self.nhood = self.mkNeighborhood()
        self.nhood.addListener(self)

        # Configure the controller
        self.controller = self.mkController()

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
        self.netfilter = NetFilter(self.nhood,
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
        self.controller.net_link = self.netq

    def mkPHY(self, header_mcs, mcs_table):
        """Construct a PHY from configuration parameters"""
        config = self.config

        if issubclass(config.phy_class, dragonradio.liquid.OFDM):
            phy = config.phy_class(header_mcs,
                                   mcs_table,
                                   config.soft_header,
                                   config.soft_payload,
                                   config.M,
                                   config.cp_len,
                                   config.taper_len,
                                   config.subcarriers)
        else:
            phy = config.phy_class(header_mcs,
                                   mcs_table,
                                   config.soft_header,
                                   config.soft_payload)

        return phy

    def mkNeighborhood(self):
        return Neighborhood(self.node_id)

    def mkChannelizer(self):
        """Construct a Channelizer according to configuration parameters"""
        config = self.config

        channelizer = config.channelizer_class(PHYChannels([]),
                                               self.usrp.rx_rate,
                                               config.num_demodulation_threads)

        if isinstance(channelizer, OverlapTDChannelizer):
            channelizer.enforce_ordering = config.channelizer_enforce_ordering

        return channelizer

    def mkSynthesizer(self, mac_class: Type[MAC]):
        """Construct a Synthesizer according to configuration parameters"""
        config = self.config

        return config.synthesizer_class(PHYChannels([]),
                                        self.usrp.tx_rate,
                                        config.num_modulation_threads)

    def mkController(self) -> Controller:
        """Construct a Controller according to configuration parameters"""
        config = self.config

        if config.amc and not config.arq:
            raise ConfigException('AMC requires ARQ')

        if config.arq:
            controller = SmartController(self.nhood,
                                         # Add MCU to MTU
                                         config.mtu + config.arq_mcu,
                                         self.phy,
                                         config.arq_window,
                                         config.arq_window,
                                         self.evm_thresholds)

            # ARQ parameters
            controller.enforce_ordering = config.arq_enforce_ordering
            controller.max_retransmissions = config.arq_max_retransmissions
            controller.unreachable_timeout = config.arq_unreachable_timeout
            controller.proactive_unreachable = config.arq_proactive_unreachable
            controller.purge_unreachable = config.arq_purge_unreachable
            controller.ack_delay = config.arq_ack_delay
            controller.ack_delay_estimation_window = config.arq_ack_delay_estimation_window
            controller.retransmission_delay = config.arq_retransmission_delay
            controller.min_retransmission_delay = config.arq_min_retransmission_delay
            controller.retransmission_delay_slop = config.arq_retransmission_delay_slop
            controller.sack_delay = config.arq_sack_delay
            controller.max_sacks = config.arq_max_sacks
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
            controller.aggressive_stats_reset = config.amc_aggressive_stats_reset
            if config.amc_mcs_fast_adjustment_period is not None:
                controller.mcs_fast_adjustment_period = config.amc_mcs_fast_adjustment_period
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
            controller = DummyController(self.nhood, config.mtu)

        return controller

    def mkNetQueue(self) -> Queue:
        """Construct a network queue according to configuration parameters"""
        config = self.config

        if config.queue == 'fifo':
            netq = SimpleQueue(SimpleQueue.FIFO)
        elif config.queue == 'lifo':
            netq = SimpleQueue(SimpleQueue.LIFO)
        elif config.queue == 'mandate':
            netq = MandateQueue()
            netq.bonus_phase = config.mandate_bonus_phase
            netq.use_wall_timestamp = config.mandate_use_wall_timestamp
        elif config.queue == 'taildrop':
            netq = TailDropQueue(config.tail_drop_max_size)
        elif config.queue == 'red':
            netq = REDQueue(config.red_gentle,
                            config.red_min_thresh,
                            config.red_max_thresh,
                            config.red_max_p,
                            config.red_w_q)
        else:
            raise ConfigException('Unknown queue type: %s' % config.queue)

        netq.transmission_delay = config.transmission_delay

        return netq

    def mkAutoGain(self) -> AutoGain:
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

    def setChannels(self, channels: Sequence[Channel]):
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

    def setRXChannels(self, channels: Sequence[Channel]):
        """Configure RX chain for channels"""
        # Initialize channelizer
        self.setRXRate(self.bandwidth)

        # We need to do this *after* setting the RX rate because it is used to
        # determine filter parameters
        self.setChannelizerChannels(channels)

    def setTXChannels(self, channels: Sequence[Channel]):
        """Configure TX chain for channels"""
        if self.config.tx_upsample:
            self.setTXRate(self.bandwidth)
            self.setSynthesizerChannels(channels)
        else:
            self.setTXChannelIdx(self.tx_channel_idx)

    def setChannelizerChannels(self, channels: Sequence[Channel]):
        """Set channelizer's channels."""
        self.channelizer.channels = \
            PHYChannels([PHYChannel(chan, self.evm_thresholds, self.genChannelizerTaps(chan), self.phy) for chan in channels])

    def setSynthesizerChannels(self, channels: Sequence[Channel]):
        """Set synthesizer's channels."""
        self.synthesizer.channels = \
            PHYChannels([PHYChannel(chan, self.evm_thresholds, self.genSynthesizerTaps(chan), self.phy) for chan in channels])

        # LLC needs to know transmitting channels
        self.controller.channels = self.synthesizer.channels

    @cached_property
    def valid_rates(self) -> List[float]:
        """Valid decimation and interpolation rates

        See:
          https://files.ettus.com/manual/page_general.html#general_sampleratenotes
        """

        # Start out with only even rates. We sort this list in reverse so we can
        # easily find the first rate that is less than or equal to the requested
        # decimation rate.
        rates = sorted([2**i * 5**j for i in range(1,5) for j in range(0,4)], reverse=True)

        # If the rate exceeds 128, then rate must be evenly divisible by 2
        rates = [r for r in rates if r <= 128 or r % 2 == 0]

        # If the rate exceeds 256, the rate must be evenly divisible by 4.
        rates = [r for r in rates if r <= 256 or r % 4 == 0]

        return rates

    def findValidRate(self, min_rate: float) -> float:
        """Find a valid rate no less than min_rate given the clock rate.

        Args:
            min_rate (float): The minimum desired rate
            clock_rate (float): The radio clock rate

        Returns:
            float: A rate no less than rate min_rate that is supported by the hardware
        """
        clock_rate = self.usrp.clock_rate

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

    @property
    def rx_rate(self) -> float:
        """Current RX rate"""
        return self.usrp.rx_rate

    def setRXRate(self, rate: float):
        """Set RX rate"""
        config = self.config

        if config.rx_bandwidth is not None:
            want_rx_rate = config.rx_bandwidth
        else:
            rx_rate_oversample = config.rx_oversample_factor*self.phy.min_rx_rate_oversample

            want_rx_rate = rate*rx_rate_oversample
            want_rx_rate = min(want_rx_rate, config.max_bandwidth)

        want_rx_rate = self.findValidRate(want_rx_rate)

        if self.rx_rate != want_rx_rate:
            self.usrp.rx_rate = want_rx_rate

            if self.usrp.rx_rate != want_rx_rate:
                raise ValueError('Wanted RX rate %g, but got %g' % (want_rx_rate, self.rx_rate))

        if self.channelizer.rx_rate != self.rx_rate:
            self.channelizer.rx_rate = self.rx_rate

    @property
    def tx_rate(self) -> float:
        """Current TX rate"""
        return self.usrp.tx_rate

    def setTXRate(self, rate: float):
        """Set TX rate"""
        config = self.config

        if config.tx_bandwidth is not None and config.tx_upsample:
            logger.warning("TX bandwidth set, but TX upsampling requested.")

        if config.tx_bandwidth is not None and not config.tx_upsample:
            want_tx_rate = config.tx_bandwidth
        else:
            tx_rate_oversample = config.tx_oversample_factor*self.phy.min_tx_rate_oversample

            want_tx_rate = rate*tx_rate_oversample
            want_tx_rate = min(want_tx_rate, config.max_bandwidth)

        want_tx_rate = self.findValidRate(want_tx_rate)

        if self.tx_rate != want_tx_rate:
            self.usrp.tx_rate = want_tx_rate

            if self.usrp.tx_rate != want_tx_rate:
                raise ValueError('Wanted TX rate %g, but got %g' % (want_tx_rate, self.tx_rate))

        if self.synthesizer.tx_rate != self.tx_rate:
            self.synthesizer.tx_rate = self.tx_rate

    @property
    def tx_channel_idx(self) -> Optional[int]:
        """The index of the channel the radio uses for transmissions.

        When the radio is not upsampling, it configures itself to transmit on a
        single channel. This is the index of that channel.

        If the radio is upsampling, there is no TX channel index, and this
        property is None.

        Returns:
            Optional[int]: The TX channel index
        """
        if self.config.tx_upsample:
            return None

        return self._tx_channel_idx

    def setTXChannelIdx(self, channel_idx: int):
        """Set the index of the transmission channel.

        If we are upsampling on TX, this is a no-op. Otherwise, the radio
        configures its frequency, bandwidth, and synthesizer for the new, single
        channel.

        Args:
            channel_idx (int): The index of the channel to use for
            transmissions.
        """
        config = self.config

        if config.tx_upsample:
            logger.warning('Attempt to set TX channel when upsampling')
            return

        # Sanity check the TX channel
        if channel_idx < 0 or channel_idx >= len(self.channels):
            logger.warning('Attempt to set TX channel to %d, but there are only %d channels',
                            channel_idx,
                            len(self.channels))

            channel_idx = max(0, min(channel_idx, len(self.channels) - 1))

        self._tx_channel_idx = channel_idx

        # Get the channel corresponding to the channel index
        channel = self.channels[channel_idx]

        # Set TX rate to the channel's bandwidth. This will compensate for
        # any necessary oversampling.
        self.setTXRate(channel.bw)

        # Set TX frequency to configured radio frequency plus the center
        # frequency offset of our TX channel.
        logger.info("Setting TX frequency offset to %g", channel.fc)
        self.usrp.tx_frequency = self.frequency + channel.fc

        # Set synthesizer channel to a "dummy" channel. From the
        # synthesizer's perspective, it is operating with a single a channel
        # whose center frequency is 0 and whose bandwidth is the bandwidth
        # of the "true" channel.
        self.setSynthesizerChannels([Channel(0, channel.bw)])

        # Allow the MAC to figure out the TX offset so snapshot self
        # tranmissions are correctly logged
        if self.mac is not None:
            self.mac.reconfigure()

    def reconfigureBandwidthAndFrequency(self, bandwidth: float, frequency: float):
        """Reconfigure the radio for the given bandwidth and frequency

        Args:
            bandwidth (float): bandwidth (Hz)
            frequency (float): frequency (Hz)
        """
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
            self.setTXChannelIdx(self.tx_channel_idx)

    def environmentDiscontinuity(self):
        # When the environment changes, we need to inform the controller so that
        # it can reset MCS transition probabilities and adjust its MCS strategy
        # appropriately.
        if isinstance(self.controller, SmartController):
            self.controller.environmentDiscontinuity()

    def genChannelizerTaps(self, channel: Channel) -> np.ndarray:
        """Generate channelizer filter taps for given channel"""
        config = self.config

        # Calculate channelizer taps
        if channel.bw == self.usrp.rx_rate:
            return [1]

        if config.channelizer == 'freqdomain':
            wp = 0.95*channel.bw
            ws = channel.bw
            fs = self.usrp.rx_rate

            h = dragonradio.signal.lowpass(wp, ws, fs, ftype=config.channelizer_ftype, Nmax=FDChannelizer.P)
        else:
            wp = 0.9*channel.bw
            ws = 1.1*channel.bw
            fs = self.usrp.rx_rate

            h = dragonradio.signal.lowpass(wp, ws, fs)

        logger.debug('Created prototype lowpass filter for channelizer: N=%d; wp=%g; ws=%g; fs=%g',
                     len(h), wp, ws, fs)
        return h

    def genSynthesizerTaps(self, channel: Channel) -> np.ndarray:
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

    def configureMAC(self, mac_class: Type[MAC]):
        """Configure MAC"""
        if issubclass(mac_class, SlottedALOHA):
            self.configureALOHA()
        else:
            self.configureSimpleMACSchedule(mac_class)

    def deleteMAC(self):
        """Delete the current MAC"""
        if self.mac is not None:
            self.mac.stop()
            self.mac = None

    def configureALOHA(self):
        """Configure ALOHA MAC"""
        config = self.config

        self.mac = SlottedALOHA(self.usrp,
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

    def configureTDMA(self, nslots: int):
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
            self.replaceSynthesizer(TDMA)

        # Replace the MAC
        self.deleteMAC()

        self.mac = TDMA(self.usrp,
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
            self.replaceSynthesizer(FDMA)

        # Replace the MAC
        self.deleteMAC()

        self.mac = FDMA(self.usrp,
                        self.controller,
                        self.snapshot_collector,
                        self.channelizer,
                        self.synthesizer,
                        config.slot_size)

        self.mac.accurate_tx_timestamps = config.mac_accurate_tx_timestamps
        self.mac.timed_tx_delay = config.mac_timed_tx_delay

        self.finishConfiguringMAC()

    def finishConfiguringMAC(self):
        """Finish configuring MAC"""
        pass

    def replaceSynthesizer(self, mac_class: Type[MAC]):
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
        self.synthesizer = self.mkSynthesizer(mac_class)

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
            self.setTXChannelIdx(channel_idx)

    def installALOHASchedule(self):
        """Install a schedule for an ALOHA MAC.

        This installs a schedule with one slot per channel. If we are not
        resampling on TX, it installs a schedule with one slot.
        """
        self.mac.slotidx = 0

        # All nodes can transmit
        for node_id in self.nhood.neighbors.keys():
            self.controller.setEmcon(node_id, False)

        if self.config.tx_upsample:
            self.mac_schedule = np.identity(len(self.channels)).astype('bool')
        else:
            self.setTXChannelIdx(0)

            self.mac_schedule = [[1]]

        self.mac.schedule = self.mac_schedule
        self.synthesizer.schedule = self.mac_schedule

    def installMACSchedule(self, sched: Schedule, mac_class: Optional[Type[MAC]]):
        """Install a MAC schedule.

        Args:
            sched: The schedule, which is a nchannels X nslots array of node
                IDs.
            fdma_mac: If True, use the FDMA MAC
        """
        config = self.config

        if mac_class is None:
            mac_class = config.mac_class

        logger.debug('Installing MAC schedule:\n%s', sched)

        # Get number of channels and slots
        (_nchannels, nslots) = sched.shape

        # First configure the TDMA MAC for the desired number of slots
        if issubclass(mac_class, FDMA):
            if nslots != 1:
                raise ValueError("FDMA schedule has more than one slot: %s" % sched)
            self.configureFDMA()
        else:
            self.configureTDMA(nslots)

        # Determine which nodes are allowed to transmit
        nodes_with_slot = set(sched.flatten())
        if 0 in nodes_with_slot:
            nodes_with_slot.remove(0)

        for node_id in self.nhood.neighbors.keys():
            self.controller.setEmcon(node_id, node_id not in nodes_with_slot)

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

            self.setTXChannelIdx(chan)

            self.mac_schedule = [sched[chan] == self.node_id]

        self.mac.schedule = self.mac_schedule
        self.synthesizer.schedule = self.mac_schedule

    def configureSimpleMACSchedule(self, mac_class: Optional[Type[MAC]]=None):
        """Set a simple static schedule."""
        nchannels = len(self.channels)
        if self.config.manet and self.config.num_nodes is not None:
            nodes = range(1, self.config.num_nodes + 1)
        else:
            nodes = sorted(list(self.nhood.neighbors))

        if nchannels == 1:
            sched = dragonradio.schedule.pureTDMASchedule(nodes)
        else:
            sched = dragonradio.schedule.fullChannelMACSchedule(nchannels,
                                                                1,
                                                                nodes,
                                                                3)

        self.installMACSchedule(sched, mac_class)

    def synchronizeClock(self):
        """Use timestamps to synchronize our clock with the time master (the gateway)"""
        if self.nhood.time_master is None:
            return

        if self.node_id == self.nhood.time_master:
            return

        timesync.synchronize(self.config, self)

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
            if config.snapshot_duration != config.snapshot_frequency:
                # Sleep a random amount to de-synchronize with other radios
                # collecting snapshots.
                await asyncio.sleep(random.uniform(0, config.snapshot_duration))

            while True:
                # Collecting snapshot for config.snapshot_duration
                collector.start()
                await asyncio.sleep(config.snapshot_duration)

                if config.snapshot_duration == config.snapshot_frequency:
                    snapshot = collector.next()
                else:
                    # Stop collecting slots
                    collector.stop()

                    # Wait for remaining packets in snapshot to be demodulated and
                    # get the snapshot
                    if config.snapshot_finalize_wait != 0:
                        await asyncio.sleep(config.snapshot_finalize_wait)

                    # Finalize the snapshot
                    snapshot = collector.finalize()

                # Log the snapshot
                if config.log_snapshots:
                    self.logger.logSnapshot(snapshot)

                if config.snapshot_duration != config.snapshot_frequency:
                    await asyncio.sleep(config.snapshot_frequency - (config.snapshot_duration + config.snapshot_finalize_wait))
        except asyncio.CancelledError:
            return

    @property
    def frequency(self):
        """Center frequency"""
        return self.config.frequency

    @frequency.setter
    def frequency(self, new_frequency):
        self.reconfigureBandwidthAndFrequency(self.bandwidth, new_frequency)

    @property
    def bandwidth(self):
        """Bandwidth"""
        return min(self.config.bandwidth, self.config.max_bandwidth)

    @bandwidth.setter
    def bandwidth(self, new_bandwidth):
        self.reconfigureBandwidthAndFrequency(new_bandwidth, self.frequency)

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

    @property
    def me_timestamps(self):
        """Timestamps for this node"""
        if isinstance(self.controller, SmartController):
            me = self.nhood.me
            if me.id in self.controller.timestamps:
                return self.controller.timestamps[me.id].values()
            else:
                return []
        else:
            return []

    @property
    def master_timestamps(self):
        """Timestamps for time master"""
        if isinstance(self.controller, SmartController) and self.nhood.time_master is not None:
            master = self.nhood.neighbors[self.nhood.time_master]
            if master.id in self.controller.timestamps:
                return self.controller.timestamps[master.id].values()
            else:
                return []
        else:
            return []
