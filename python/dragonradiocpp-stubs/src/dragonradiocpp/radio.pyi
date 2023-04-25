from __future__ import annotations
import datetime
import dragonradiocpp.packet
import numpy
import typing
import typing_extensions
__all__ = ['AutoGain', 'Channel', 'Channelizer', 'Controller', 'ControllerNetLink', 'Demodulator', 'DoubleEstimator', 'DoubleMean', 'DoubleWindowedMean', 'DummyController', 'FDChannelizer', 'FDMA', 'FDSynthesizer', 'FloatEstimator', 'FloatMean', 'FloatWindowedMean', 'FlowPerformance', 'FlowStats', 'GPSLocation', 'Gain', 'IQBuf', 'Load', 'MAC', 'MCS', 'MCSEntry', 'MPStats', 'MPStatsVector', 'Mandate', 'MandateMap', 'MandateQueue', 'ModPacket', 'Modulator', 'MonoClock', 'MonoTimeWindowEstimator', 'MonoTimeWindowMax', 'MonoTimeWindowMean', 'MonoTimeWindowMeanRate', 'MonoTimeWindowMin', 'MultichannelSynthesizer', 'Neighborhood', 'NeighborhoodListener', 'NetFilter', 'NetFirewall', 'NetInPull', 'NetInPush', 'NetNoop', 'NetOutPull', 'NetOutPush', 'NetProcessor', 'Node', 'PHY', 'PHYChannel', 'PHYChannels', 'PacketCompressor', 'PacketDemodulator', 'PacketModulator', 'Queue', 'REDQueue', 'Radio', 'RadioFirewall', 'RadioInPull', 'RadioInPush', 'RadioNoop', 'RadioOutPull', 'RadioOutPush', 'RadioProcessor', 'ReceiveWindow', 'ReceiveWindows', 'Schedule', 'Score', 'ScoreMap', 'Scorer', 'Scores', 'SelfTX', 'SelfTXs', 'SendWindow', 'SendWindows', 'SimpleQueue', 'SizedQueue', 'SlottedALOHA', 'SlottedMAC', 'SmartController', 'Snapshot', 'SnapshotCollector', 'Synthesizer', 'TDChannelizer', 'TDMA', 'TDSynthesizer', 'TailDropQueue', 'TimeKeeper', 'Timestamps', 'TunTap', 'USRP', 'WallClock', 'WallTimeWindowEstimator', 'WallTimeWindowMax', 'WallTimeWindowMean', 'WallTimeWindowMeanRate', 'WallTimeWindowMin', 'WorkQueue', 'clock', 'work_queue']
class AutoGain:
    def __init__(self) -> None:
        ...
    def recalc0dBFSEstimate(self, nsamples: int) -> None:
        """
        Reset the 0dBFS estimate
        """
    @property
    def auto_soft_tx_gain_clip_frac(self) -> float:
        """
        float: Clipping threshold for automatic TX soft gain
        """
    @auto_soft_tx_gain_clip_frac.setter
    def auto_soft_tx_gain_clip_frac(self, arg1: float) -> None:
        ...
    @property
    def g_0dBFS(self) -> float:
        """
        float: Soft TX gain (multiplicative factor)
        """
    @g_0dBFS.setter
    def g_0dBFS(self, arg1: float) -> None:
        ...
    @property
    def soft_tx_gain_0dBFS(self) -> float:
        """
        float: Soft TX gain (dBFS)
        """
    @soft_tx_gain_0dBFS.setter
    def soft_tx_gain_0dBFS(self, arg1: float) -> None:
        ...
class Channel:
    def __eq__(self, arg0: Channel) -> bool:
        ...
    def __getstate__(self) -> tuple:
        ...
    def __gt__(self, arg0: Channel) -> bool:
        ...
    def __hash__(self) -> int:
        ...
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, fc: float, bw: float) -> None:
        """
        Initialize channel with center frequency and bandwidth
        """
    def __lt__(self, arg0: Channel) -> bool:
        ...
    def __ne__(self, arg0: Channel) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, arg0: tuple) -> None:
        ...
    def intersects(self, channel: Channel) -> bool:
        """
        Return True if channels intersect, False otherwise
        """
    @property
    def bw(self) -> float:
        """
        float: Bandwidth
        """
    @bw.setter
    def bw(self, arg0: float) -> None:
        ...
    @property
    def fc(self) -> float:
        """
        float: Frequency shift from center
        """
    @fc.setter
    def fc(self, arg0: float) -> None:
        ...
class Channelizer:
    @property
    def channels(self) -> PHYChannels:
        """
        Sequence[Channel]: channels
        """
    @channels.setter
    def channels(self, arg1: PHYChannels) -> None:
        ...
    @property
    def rx_rate(self) -> float:
        """
        float: RX rate
        """
    @rx_rate.setter
    def rx_rate(self, arg1: float) -> None:
        ...
    @property
    def source(self) -> RadioOutPush:
        ...
class Controller:
    net_link: ControllerNetLink
    def setEmcon(self, arg0: int, arg1: bool) -> None:
        """
        Set whether or not a node can transmit
        """
    @property
    def net_in(self) -> NetInPull:
        ...
    @property
    def net_out(self) -> NetOutPull:
        ...
    @property
    def radio_in(self) -> RadioInPush:
        ...
    @property
    def radio_out(self) -> RadioOutPush:
        ...
class ControllerNetLink:
    @property
    def transmission_delay(self) -> datetime.timedelta:
        """
        Transmission delay (sec)
        """
    @transmission_delay.setter
    def transmission_delay(self, arg1: datetime.timedelta) -> None:
        ...
class Demodulator:
    def demodulate(self, arg0: numpy.ndarray) -> list[tuple[dragonradiocpp.packet.Header | None, bytes | None]]:
        """
        Demodulate a signal
        """
    def reset(self) -> None:
        """
        Reset demodulator state
        """
    @property
    def is_frame_open(self) -> bool:
        ...
class DoubleEstimator:
    def update(self, arg0: float) -> None:
        """
        Update the estimate
        """
    @property
    def size(self) -> int:
        """
        The number of samples used in the estimate
        """
    @property
    def value(self) -> float | None:
        """
        The value of the estimator
        """
class DoubleMean(DoubleEstimator):
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, arg0: float) -> None:
        ...
    def remove(self, arg0: float) -> None:
        """
        Remove a value used to estimate the mean
        """
    def reset(self, arg0: float) -> None:
        """
        Reset the estimate
        """
class DoubleWindowedMean(DoubleEstimator):
    def reset(self) -> None:
        """
        Reset the estimate
        """
    @property
    def window_size(self) -> int:
        """
        Number of samples in window
        """
    @window_size.setter
    def window_size(self, arg1: int) -> None:
        ...
class DummyController(Controller):
    def __init__(self, arg0: Neighborhood, arg1: int) -> None:
        ...
class FDChannelizer(Channelizer):
    L: typing.ClassVar[int] = 67200
    N: typing.ClassVar[int] = 76800
    P: typing.ClassVar[int] = 9601
    V: typing.ClassVar[int] = 8
    def __init__(self, arg0: PHYChannels, arg1: float, arg2: int) -> None:
        ...
class FDMA(MAC):
    def __init__(self, radio: Radio, controller: Controller, snapshot_collector: SnapshotCollector, channelizer: Channelizer, synthesizer: Synthesizer, premodulation: float) -> None:
        ...
class FDSynthesizer(Synthesizer):
    P: typing.ClassVar[int] = 9601
    def __init__(self, channels: PHYChannels, tx_rate: float, nthreads: int) -> None:
        ...
class FloatEstimator:
    def update(self, arg0: float) -> None:
        """
        Update the estimate
        """
    @property
    def size(self) -> int:
        """
        The number of samples used in the estimate
        """
    @property
    def value(self) -> float | None:
        """
        The value of the estimator
        """
class FloatMean(FloatEstimator):
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, arg0: float) -> None:
        ...
    def remove(self, arg0: float) -> None:
        """
        Remove a value used to estimate the mean
        """
    def reset(self, arg0: float) -> None:
        """
        Reset the estimate
        """
class FloatWindowedMean(FloatEstimator):
    def reset(self) -> None:
        """
        Reset the estimate
        """
    @property
    def window_size(self) -> int:
        """
        Number of samples in window
        """
    @window_size.setter
    def window_size(self, arg1: int) -> None:
        ...
class FlowPerformance:
    def __init__(self, arg0: float) -> None:
        ...
    def getSinks(self, arg0: bool) -> dict[int, FlowStats]:
        """
        Get flow sink statistics
        """
    def getSources(self, arg0: bool) -> dict[int, FlowStats]:
        """
        Get flow source statistics
        """
    @property
    def mandates(self) -> MandateMap:
        """
        Mandates
        """
    @mandates.setter
    def mandates(self, arg1: MandateMap) -> None:
        ...
    @property
    def mp(self) -> float:
        """
        Measurement period (sec)
        """
    @property
    def net_in(self) -> NetInPush:
        """
        Network packet input port
        """
    @property
    def net_out(self) -> NetOutPush:
        """
        Network packet output port
        """
    @property
    def radio_in(self) -> RadioInPush:
        """
        Radio packet input port
        """
    @property
    def radio_out(self) -> RadioOutPush:
        """
        Radio packet output port
        """
    @property
    def start(self) -> datetime.timedelta | None:
        """
        Match start time in seconds since epoch
        """
    @start.setter
    def start(self, arg1: datetime.timedelta | None) -> None:
        ...
class FlowStats:
    def __repr__(self) -> str:
        ...
    @property
    def dest(self) -> int:
        """
        Flow destinations
        """
    @property
    def flow_uid(self) -> int:
        """
        Flow UID
        """
    @property
    def low_mp(self) -> int | None:
        """
        Lowest MP modified
        """
    @property
    def src(self) -> int:
        """
        Flow source
        """
    @property
    def stats(self) -> list[MPStats]:
        """
        Flow statistics per-measuremnt period
        """
class GPSLocation:
    def __repr__(self) -> str:
        ...
    @property
    def alt(self) -> float:
        """
        Altitude
        """
    @alt.setter
    def alt(self, arg0: float) -> None:
        ...
    @property
    def lat(self) -> float:
        """
        Latitude
        """
    @lat.setter
    def lat(self, arg0: float) -> None:
        ...
    @property
    def lon(self) -> float:
        """
        Longitude
        """
    @lon.setter
    def lon(self, arg0: float) -> None:
        ...
    @property
    def timestamp(self) -> float:
        """
        Timestamp of last update
        """
    @timestamp.setter
    def timestamp(self, arg0: float) -> None:
        ...
class Gain:
    def __repr__(self) -> str:
        ...
    @property
    def dB(self) -> float:
        """
        float: Logarithmic gain (dB)
        """
    @dB.setter
    def dB(self, arg1: float) -> None:
        ...
    @property
    def lin(self) -> float:
        """
        float: Linear gain (multiplicative factor)
        """
    @lin.setter
    def lin(self, arg1: float) -> None:
        ...
class IQBuf:
    def __getitem__(self, arg0: int) -> complex:
        ...
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, arg0: numpy.ndarray) -> None:
        ...
    def __len__(self) -> int:
        ...
    def __repr__(self) -> str:
        ...
    def __setitem__(self, arg0: int, arg1: complex) -> None:
        ...
    @property
    def data(self) -> numpy.ndarray:
        """
        IQ data
        """
    @data.setter
    def data(self, arg1: numpy.ndarray) -> None:
        ...
    @property
    def delay(self) -> int:
        """
        Signal delay
        """
    @delay.setter
    def delay(self, arg0: int) -> None:
        ...
    @property
    def fc(self) -> float:
        """
        Sample senter frequency
        """
    @fc.setter
    def fc(self, arg0: float) -> None:
        ...
    @property
    def fs(self) -> float:
        """
        Sample rate
        """
    @fs.setter
    def fs(self, arg0: float) -> None:
        ...
    @property
    def timestamp(self) -> datetime.timedelta | None:
        """
        IQ buffer timestamp in seconds
        """
    @timestamp.setter
    def timestamp(self, arg0: datetime.timedelta | None) -> None:
        ...
class Load:
    @property
    def end(self) -> float:
        """
        float: End of load measurement period (sec)
        """
    @property
    def nsamples(self) -> list[int]:
        """
        int: Load per channel measured in number of samples
        """
    @nsamples.setter
    def nsamples(self, arg0: list[int]) -> None:
        ...
    @property
    def period(self) -> float:
        """
        float: Measurement period (sec)
        """
    @property
    def start(self) -> float:
        """
        float: Start of load measurement period (sec)
        """
class MAC:
    def getLoad(self) -> Load:
        """
        Get current load
        """
    def popLoad(self) -> Load:
        """
        Get current load and reset load counters
        """
    def rateChange(self) -> None:
        """
        Notify the MAC of a TX/RX rate change
        """
    def stop(self) -> None:
        """
        Tell MAC to stop processing packets.
        """
    @property
    def accurate_tx_timestamps(self) -> bool:
        """
        bool: Increase timestamp accuracy at a potential cost to performance
        """
    @accurate_tx_timestamps.setter
    def accurate_tx_timestamps(self, arg1: bool) -> None:
        ...
    @property
    def schedule(self) -> Schedule:
        """
        Schedule: MAC schedule specifying on which channels this node may transmit in each schedule slot.
        """
    @schedule.setter
    def schedule(self, arg1: typing.Any) -> None:
        ...
class MCS:
    def __str__(self) -> str:
        ...
    @property
    def rate(self) -> float:
        """
        Approximate rate (bps)
        """
class MCSEntry:
    @property
    def autogain(self) -> AutoGain:
        """
        AutoGain: AutoGain for MCS
        """
    @property
    def mcs(self) -> MCS:
        """
        MCS: Modulation and coding scheme
        """
class MPStats:
    def __repr__(self) -> str:
        ...
    @property
    def nbytes(self) -> int:
        """
        Number of bytes
        """
    @property
    def npackets(self) -> int:
        """
        Number of packets
        """
class MPStatsVector:
    def __bool__(self) -> bool:
        """
        Check whether the list is nonempty
        """
    @typing.overload
    def __delitem__(self, arg0: int) -> None:
        """
        Delete the list elements at index ``i``
        """
    @typing.overload
    def __delitem__(self, arg0: slice) -> None:
        """
        Delete list elements using a slice object
        """
    @typing.overload
    def __getitem__(self, s: slice) -> list[MPStats]:
        """
        Retrieve list elements using a slice object
        """
    @typing.overload
    def __getitem__(self, arg0: int) -> MPStats:
        ...
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, arg0: list[MPStats]) -> None:
        """
        Copy constructor
        """
    @typing.overload
    def __init__(self, arg0: typing.Iterable) -> None:
        ...
    def __iter__(self) -> typing.Iterator:
        ...
    def __len__(self) -> int:
        ...
    @typing.overload
    def __setitem__(self, arg0: int, arg1: MPStats) -> None:
        ...
    @typing.overload
    def __setitem__(self, arg0: slice, arg1: list[MPStats]) -> None:
        """
        Assign list elements using a slice object
        """
    def append(self, x: MPStats) -> None:
        """
        Add an item to the end of the list
        """
    def clear(self) -> None:
        """
        Clear the contents
        """
    @typing.overload
    def extend(self, L: list[MPStats]) -> None:
        """
        Extend the list by appending all the items in the given list
        """
    @typing.overload
    def extend(self, L: typing.Iterable) -> None:
        """
        Extend the list by appending all the items in the given list
        """
    def insert(self, i: int, x: MPStats) -> None:
        """
        Insert an item at a given position.
        """
    @typing.overload
    def pop(self) -> MPStats:
        """
        Remove and return the last item
        """
    @typing.overload
    def pop(self, i: int) -> MPStats:
        """
        Remove and return the item at index ``i``
        """
class Mandate:
    def __init__(self, arg0: int, arg1: datetime.timedelta, arg2: int, arg3: datetime.timedelta | None, arg4: float | None, arg5: datetime.timedelta | None) -> None:
        ...
    @property
    def achieved_duration(self) -> datetime.timedelta:
        """
        Achieved duration
        """
    @achieved_duration.setter
    def achieved_duration(self, arg0: datetime.timedelta) -> None:
        ...
    @property
    def file_transfer_deadline_s(self) -> datetime.timedelta | None:
        """
        File transfer delivery deadline (sec)
        """
    @property
    def flow_uid(self) -> int:
        """
        Flow UID)
        """
    @property
    def hold_period(self) -> datetime.timedelta:
        """
        Steady state period required for mandate success (sec)
        """
    @property
    def mandated_latency(self) -> datetime.timedelta | None:
        """
        Maximum latency (sec)
        """
    @property
    def max_latency_s(self) -> datetime.timedelta | None:
        """
        Maximum latency allowed for a packet (sec)
        """
    @property
    def min_throughput_bps(self) -> float | None:
        """
        Minimum throughput (bps)
        """
    @property
    def point_value(self) -> int:
        """
        Point value
        """
    @property
    def radio_ids(self) -> list[int]:
        """
        Nodes in flow
        """
    @radio_ids.setter
    def radio_ids(self, arg0: list[int]) -> None:
        ...
    @property
    def scalar_performance(self) -> float:
        """
        Scalar performance
        """
    @scalar_performance.setter
    def scalar_performance(self, arg0: float) -> None:
        ...
class MandateMap:
    def __bool__(self) -> bool:
        """
        Check whether the map is nonempty
        """
    @typing.overload
    def __contains__(self, arg0: int) -> bool:
        ...
    @typing.overload
    def __contains__(self, arg0: typing.Any) -> bool:
        ...
    def __delitem__(self, arg0: int) -> None:
        ...
    def __getitem__(self, arg0: int) -> Mandate:
        ...
    def __init__(self) -> None:
        ...
    def __iter__(self) -> typing.Iterator:
        ...
    def __len__(self) -> int:
        ...
    def __setitem__(self, arg0: int, arg1: Mandate) -> None:
        ...
    def items(self) -> typing.ItemsView[int, Mandate]:
        ...
    def keys(self) -> typing.KeysView[int]:
        ...
    def values(self) -> typing.ValuesView[Mandate]:
        ...
class MandateQueue(Queue, ControllerNetLink):
    class QueueType:
        """
        Members:
        
          FIFO
        
          LIFO
        """
        FIFO: typing.ClassVar[MandateQueue.QueueType]  # value = <QueueType.FIFO: 0>
        LIFO: typing.ClassVar[MandateQueue.QueueType]  # value = <QueueType.LIFO: 1>
        __members__: typing.ClassVar[dict[str, MandateQueue.QueueType]]  # value = {'FIFO': <QueueType.FIFO: 0>, 'LIFO': <QueueType.LIFO: 1>}
        def __eq__(self, other: typing.Any) -> bool:
            ...
        def __getstate__(self) -> int:
            ...
        def __hash__(self) -> int:
            ...
        def __index__(self) -> int:
            ...
        def __init__(self, value: int) -> None:
            ...
        def __int__(self) -> int:
            ...
        def __ne__(self, other: typing.Any) -> bool:
            ...
        def __repr__(self) -> str:
            ...
        def __setstate__(self, state: int) -> None:
            ...
        def __str__(self) -> str:
            ...
        @property
        def name(self) -> str:
            ...
        @property
        def value(self) -> int:
            ...
    FIFO: typing.ClassVar[MandateQueue.QueueType]  # value = <QueueType.FIFO: 0>
    LIFO: typing.ClassVar[MandateQueue.QueueType]  # value = <QueueType.LIFO: 1>
    def __init__(self) -> None:
        ...
    def getFlowQueuePriority(self, arg0: int) -> tuple[int, float]:
        """
        Get flow queue's priority
        """
    def getFlowQueueType(self, arg0: int) -> MandateQueue.QueueType:
        """
        Get flow queue's type
        """
    def setFlowQueuePriority(self, arg0: int, arg1: tuple[int, float]) -> None:
        """
        Set flow queue's priority
        """
    def setFlowQueueType(self, arg0: int, arg1: MandateQueue.QueueType) -> None:
        """
        Set flow queue's type
        """
    @property
    def bonus_phase(self) -> bool:
        """
        Flag indicating whether or not to have a bonus phase
        """
    @bonus_phase.setter
    def bonus_phase(self, arg1: bool) -> None:
        ...
    @property
    def mandates(self) -> MandateMap:
        """
        Mandates
        """
    @mandates.setter
    def mandates(self, arg1: MandateMap) -> None:
        ...
    @property
    def queue_priorities(self) -> list[tuple[int | None, tuple[int, float], float | None, int | None, int | None]]:
        """
        Queue priorities
        """
    @property
    def use_wall_timestamp(self) -> bool:
        """
        Flag indicating whether or not to compute deadline with packet creation timestamp
        """
    @use_wall_timestamp.setter
    def use_wall_timestamp(self, arg1: bool) -> None:
        ...
class ModPacket:
    """
    A modulated packet
    """
    def __init__(self) -> None:
        ...
    @property
    def chanidx(self) -> int:
        """
        int: Index of channel
        """
    @chanidx.setter
    def chanidx(self, arg0: int) -> None:
        ...
    @property
    def channel(self) -> Channel:
        """
        Channel: Channel
        """
    @channel.setter
    def channel(self, arg0: Channel) -> None:
        ...
    @property
    def nsamples(self) -> int:
        """
        int: Number of modulated samples
        """
    @nsamples.setter
    def nsamples(self, arg0: int) -> None:
        ...
    @property
    def offset(self) -> int:
        """
        int: Offset of start of packet from beginning of sample buffer
        """
    @offset.setter
    def offset(self, arg0: int) -> None:
        ...
    @property
    def pkt(self) -> dragonradiocpp.packet.NetPacket:
        """
        Packet: The un-modulated packet
        """
    @pkt.setter
    def pkt(self, arg0: dragonradiocpp.packet.NetPacket) -> None:
        ...
    @property
    def samples(self) -> IQBuf:
        """
        IQBuffer: Buffer containing the modulated samples
        """
    @samples.setter
    def samples(self, arg0: IQBuf) -> None:
        ...
    @property
    def start(self) -> int:
        """
        int: Offset of start of packet from start of slot, in number of samples
        """
    @start.setter
    def start(self, arg0: int) -> None:
        ...
class Modulator:
    def modulate(self, arg0: dragonradiocpp.packet.Header, arg1: typing_extensions.Buffer) -> numpy.ndarray:
        """
        Modulate a packet
        """
class MonoClock:
    def reset_time_keeper(self) -> None:
        ...
class MonoTimeWindowEstimator(DoubleEstimator):
    def reset(self) -> None:
        """
        Reset the estimate
        """
    @property
    def end(self) -> datetime.timedelta | None:
        """
        The end of the time window (sec)
        """
    @property
    def start(self) -> datetime.timedelta | None:
        """
        The start of the time window (sec)
        """
    @property
    def time_window(self) -> datetime.timedelta:
        """
        The time window (sec)
        """
    @time_window.setter
    def time_window(self, arg1: datetime.timedelta) -> None:
        ...
class MonoTimeWindowMax(MonoTimeWindowEstimator):
    pass
class MonoTimeWindowMean(MonoTimeWindowEstimator):
    pass
class MonoTimeWindowMeanRate(MonoTimeWindowMean):
    pass
class MonoTimeWindowMin(MonoTimeWindowEstimator):
    pass
class MultichannelSynthesizer(Synthesizer):
    P: typing.ClassVar[int] = 9601
    def __init__(self, channels: PHYChannels, tx_rate: float, nthreads: int) -> None:
        ...
class Neighborhood:
    """
    The local one-hop neighborhood
    """
    def __getitem__(self, arg0: int) -> Node:
        ...
    def __init__(self, node_id: int) -> None:
        ...
    def addListener(self, arg0: NeighborhoodListener) -> None:
        """
        Add a listener
        """
    @typing.overload
    def addNeighbor(self, arg0: int) -> Node:
        """
        Add a one-hop neighbor
        """
    @typing.overload
    def addNeighbor(self, arg0: Node) -> None:
        """
        Add a one-hop neighbor
        """
    def removeListener(self, arg0: NeighborhoodListener) -> None:
        """
        Remove a listener
        """
    def removeNeighbor(self, arg0: int) -> None:
        """
        Remove a one-hop neighbor
        """
    @property
    def me(self) -> Node:
        """
        Node: this node
        """
    @property
    def neighbors(self) -> dict[int, Node]:
        """
        One-hop neighbors
        """
    @property
    def time_master(self) -> int | None:
        """
        Node: the time master
        """
class NeighborhoodListener:
    """
    A listener for neighborhood events
    """
    def __init__(self) -> None:
        ...
    def gatewayAdded(self, arg0: Node) -> None:
        """
        Called when a new gateway is added
        """
    def neighborAdded(self, arg0: Node) -> None:
        """
        Called when a new neighbor is added
        """
    def neighborRemoved(self, arg0: Node) -> None:
        """
        Called when a neighbor is removed
        """
class NetFilter(NetProcessor):
    def __init__(self, arg0: Neighborhood, arg1: int, arg2: int, arg3: int, arg4: int, arg5: int, arg6: int) -> None:
        ...
class NetFirewall(NetProcessor):
    def __init__(self) -> None:
        ...
    @property
    def allow_broadcasts(self) -> bool:
        """
        Allow broadcast packets?
        """
    @allow_broadcasts.setter
    def allow_broadcasts(self, arg1: bool) -> None:
        ...
    @property
    def allowed(self) -> set[int]:
        """
        Set of allowed ports
        """
    @allowed.setter
    def allowed(self, arg1: set[int]) -> None:
        ...
    @property
    def enabled(self) -> bool:
        """
        Is the firewall enabled?
        """
    @enabled.setter
    def enabled(self, arg1: bool) -> None:
        ...
class NetInPull:
    def __lshift__(self, arg0: NetOutPull) -> None:
        ...
    def disconnect(self) -> None:
        ...
class NetInPush:
    def __lshift__(self, arg0: NetOutPush) -> None:
        ...
    def disconnect(self) -> None:
        ...
class NetNoop(NetProcessor):
    def __init__(self) -> None:
        ...
class NetOutPull:
    def __rshift__(self, arg0: NetInPull) -> None:
        ...
    def disconnect(self) -> None:
        ...
class NetOutPush:
    def __rshift__(self, arg0: NetInPush) -> None:
        ...
    def disconnect(self) -> None:
        ...
class NetProcessor:
    @property
    def input(self) -> NetInPush:
        ...
    @property
    def output(self) -> NetOutPush:
        ...
class Node:
    @property
    def emcon(self) -> bool:
        """
        Flag indicating whether or not this node is subject to emissions control
        """
    @property
    def g(self) -> float:
        """
        Soft TX gain (multiplicative)
        """
    @g.setter
    def g(self, arg0: float) -> None:
        ...
    @property
    def id(self) -> int:
        """
        Node ID
        """
    @property
    def is_gateway(self) -> bool:
        """
        Flag indicating whether or not this node is the gateway
        """
    @is_gateway.setter
    def is_gateway(self, arg0: bool) -> None:
        ...
    @property
    def loc(self) -> GPSLocation:
        ...
    @property
    def soft_tx_gain(self) -> float:
        """
        Soft TX gain (dBFS)
        """
    @soft_tx_gain.setter
    def soft_tx_gain(self, arg1: float) -> None:
        ...
    @property
    def unreachable(self) -> bool:
        """
        Flag indicating whether or not this node is unreachable
        """
class PHY:
    log_invalid_headers: typing.ClassVar[bool] = False
    node_id: typing.ClassVar[int] = 0
    team: typing.ClassVar[int] = 0
    @staticmethod
    def mkRadioPacket(arg0: dragonradiocpp.packet.Header, arg1: bytes | None) -> dragonradiocpp.packet.RadioPacket:
        ...
    def __init__(self) -> None:
        ...
    def getModulatedSize(self, arg0: int, arg1: int) -> int:
        ...
    def getRXOversampleFactor(self) -> int:
        ...
    def getTXOversampleFactor(self) -> int:
        ...
    def mkPacketDemodulator(self, arg0: int, arg1: Channel) -> PacketDemodulator:
        ...
    def mkPacketModulator(self) -> PacketModulator:
        ...
    def updateAutoGain(self, arg0: dragonradiocpp.packet.NetPacket, arg1: float, arg2: IQBuf) -> None:
        ...
    @property
    def mcs_table(self) -> list[MCSEntry]:
        """
        Table of modulation and coding schemes
        """
    @mcs_table.setter
    def mcs_table(self, arg1: list[tuple[MCS, AutoGain]]) -> None:
        ...
    @property
    def rx_oversample_factor(self) -> int:
        """
        float: oversample factor required for received signal
        """
    @property
    def tx_oversample_factor(self) -> int:
        """
        float: oversample factor of transmitted signal
        """
class PHYChannel:
    def __init__(self, arg0: Channel, arg1: PHY, arg2: list[float | None]) -> None:
        ...
    @property
    def D(self) -> int:
        """
        Decimation rate
        """
    @D.setter
    def D(self, arg0: int) -> None:
        ...
    @property
    def I(self) -> int:
        """
        Interpolation rate
        """
    @I.setter
    def I(self, arg0: int) -> None:
        ...
    @property
    def channel(self) -> Channel:
        """
        Channel: Channel
        """
    @channel.setter
    def channel(self, arg0: Channel) -> None:
        ...
    @property
    def evm_thresh(self) -> list[float | None]:
        """
        EVM threshold table
        """
    @evm_thresh.setter
    def evm_thresh(self, arg0: list[float | None]) -> None:
        ...
    @property
    def phy(self) -> PHY:
        """
        PHY: PHY for channel
        """
    @phy.setter
    def phy(self, arg0: PHY) -> None:
        ...
    @property
    def taps(self) -> list[complex]:
        """
        FIR filter taps
        """
    @taps.setter
    def taps(self, arg0: list[complex]) -> None:
        ...
class PHYChannels:
    def __bool__(self) -> bool:
        """
        Check whether the list is nonempty
        """
    @typing.overload
    def __delitem__(self, arg0: int) -> None:
        """
        Delete the list elements at index ``i``
        """
    @typing.overload
    def __delitem__(self, arg0: slice) -> None:
        """
        Delete list elements using a slice object
        """
    @typing.overload
    def __getitem__(self, s: slice) -> PHYChannels:
        """
        Retrieve list elements using a slice object
        """
    @typing.overload
    def __getitem__(self, arg0: int) -> PHYChannel:
        ...
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, arg0: PHYChannels) -> None:
        """
        Copy constructor
        """
    @typing.overload
    def __init__(self, arg0: typing.Iterable) -> None:
        ...
    def __iter__(self) -> typing.Iterator:
        ...
    def __len__(self) -> int:
        ...
    @typing.overload
    def __setitem__(self, arg0: int, arg1: PHYChannel) -> None:
        ...
    @typing.overload
    def __setitem__(self, arg0: slice, arg1: PHYChannels) -> None:
        """
        Assign list elements using a slice object
        """
    def append(self, x: PHYChannel) -> None:
        """
        Add an item to the end of the list
        """
    def clear(self) -> None:
        """
        Clear the contents
        """
    @typing.overload
    def extend(self, L: PHYChannels) -> None:
        """
        Extend the list by appending all the items in the given list
        """
    @typing.overload
    def extend(self, L: typing.Iterable) -> None:
        """
        Extend the list by appending all the items in the given list
        """
    def insert(self, i: int, x: PHYChannel) -> None:
        """
        Insert an item at a given position.
        """
    @typing.overload
    def pop(self) -> PHYChannel:
        """
        Remove and return the last item
        """
    @typing.overload
    def pop(self, i: int) -> PHYChannel:
        """
        Remove and return the item at index ``i``
        """
class PacketCompressor:
    def __init__(self, arg0: bool, arg1: int, arg2: int, arg3: int, arg4: int) -> None:
        ...
    @property
    def enabled(self) -> bool:
        """
        Is packet compression enabled?
        """
    @enabled.setter
    def enabled(self, arg1: bool) -> None:
        ...
    @property
    def net_in(self) -> NetInPush:
        """
        Network packet input port
        """
    @property
    def net_out(self) -> NetOutPush:
        """
        Network packet output port
        """
    @property
    def radio_in(self) -> RadioInPush:
        """
        Radio packet input port
        """
    @property
    def radio_out(self) -> RadioOutPush:
        """
        Radio packet output port
        """
class PacketDemodulator:
    def __init__(self, arg0: PHY, arg1: int, arg2: Channel) -> None:
        ...
    def demodulate(self, arg0: complex, arg1: int) -> None:
        ...
    def isFrameOpen(self) -> bool:
        ...
    def reset(self, arg0: Channel) -> None:
        ...
    def timestamp(self, arg0: datetime.timedelta, arg1: int | None, arg2: int, arg3: int, arg4: float, arg5: float) -> None:
        ...
class PacketModulator:
    def __init__(self, arg0: PHY) -> None:
        ...
    def modulate(self, arg0: dragonradiocpp.packet.NetPacket, arg1: float, arg2: ModPacket) -> None:
        ...
class Queue:
    @property
    def pop(self) -> NetOutPull:
        ...
    @property
    def push(self) -> NetInPush:
        ...
    @property
    def size(self) -> int:
        """
        Queue size
        """
class REDQueue(SizedQueue, ControllerNetLink):
    def __init__(self, arg0: bool, arg1: int, arg2: int, arg3: float, arg4: float) -> None:
        ...
    @property
    def gentle(self) -> bool:
        """
        Be gentle?
        """
    @gentle.setter
    def gentle(self, arg1: bool) -> None:
        ...
    @property
    def max_p(self) -> float:
        """
        Maximum packet drop probability
        """
    @max_p.setter
    def max_p(self, arg1: float) -> None:
        ...
    @property
    def max_thresh(self) -> int:
        """
        Maximum threshold (bytes)
        """
    @max_thresh.setter
    def max_thresh(self, arg1: int) -> None:
        ...
    @property
    def min_thresh(self) -> int:
        """
        Minimum threshold (bytes)
        """
    @min_thresh.setter
    def min_thresh(self, arg1: int) -> None:
        ...
    @property
    def w_q(self) -> float:
        """
        Queue weight (for EWMA)
        """
    @w_q.setter
    def w_q(self, arg1: float) -> None:
        ...
class Radio:
    rx_frequency: float
    rx_gain: float
    rx_rate: float
    tx_frequency: float
    tx_gain: float
    tx_rate: float
    @property
    def clock_rate(self) -> float:
        ...
    @property
    def now(self) -> datetime.timedelta:
        ...
    @property
    def tx_lead_time(self) -> datetime.timedelta:
        """
        float: Lead time necessary for timed TX (sec)
        """
    @tx_lead_time.setter
    def tx_lead_time(self, arg1: datetime.timedelta) -> None:
        ...
    @property
    def tx_rampup(self) -> datetime.timedelta:
        """
        float: Time required for TX ramp-up (sec)
        """
    @tx_rampup.setter
    def tx_rampup(self, arg1: datetime.timedelta) -> None:
        ...
class RadioFirewall(RadioProcessor):
    def __init__(self) -> None:
        ...
    @property
    def allow_broadcasts(self) -> bool:
        """
        Allow broadcast packets?
        """
    @allow_broadcasts.setter
    def allow_broadcasts(self, arg1: bool) -> None:
        ...
    @property
    def allowed(self) -> set[int]:
        """
        Set of allowed ports
        """
    @allowed.setter
    def allowed(self, arg1: set[int]) -> None:
        ...
    @property
    def enabled(self) -> bool:
        """
        Is the firewall enabled?
        """
    @enabled.setter
    def enabled(self, arg1: bool) -> None:
        ...
class RadioInPull:
    def __lshift__(self, arg0: RadioOutPull) -> None:
        ...
    def disconnect(self) -> None:
        ...
class RadioInPush:
    def __lshift__(self, arg0: RadioOutPush) -> None:
        ...
    def disconnect(self) -> None:
        ...
class RadioNoop(RadioProcessor):
    def __init__(self) -> None:
        ...
class RadioOutPull:
    def __rshift__(self, arg0: RadioInPull) -> None:
        ...
    def disconnect(self) -> None:
        ...
class RadioOutPush:
    def __rshift__(self, arg0: RadioInPush) -> None:
        ...
    def disconnect(self) -> None:
        ...
class RadioProcessor:
    @property
    def input(self) -> RadioInPush:
        ...
    @property
    def output(self) -> RadioOutPush:
        ...
class ReceiveWindow:
    @property
    def long_evm(self) -> float | None:
        """
        Long-term EVM (dB)
        """
    @property
    def long_rssi(self) -> float | None:
        """
        Long-term RSSI (dB)
        """
    @property
    def short_evm(self) -> float | None:
        """
        Short-term EVM (dB)
        """
    @property
    def short_rssi(self) -> float | None:
        """
        Short-term RSSI (dB)
        """
class ReceiveWindows:
    def __contains__(self, arg0: int) -> bool:
        ...
    def __getitem__(self, arg0: int) -> ReceiveWindow:
        ...
    def keys(self) -> set[int]:
        ...
class Schedule:
    def __init__(self, schedule: list[list[bool]], slot_size: datetime.timedelta = ..., guard_size: datetime.timedelta = ..., superslots: bool = False) -> None:
        ...
    @property
    def frame_size(self) -> float:
        """
        float: Frame size (sec)
        """
    @property
    def guard_size(self) -> float:
        """
        float: Guard size (sec)
        """
    @guard_size.setter
    def guard_size(self, arg1: datetime.timedelta) -> None:
        ...
    @property
    def nchannels(self) -> int:
        """
        int: Number of channels
        """
    @property
    def nslots(self) -> int:
        """
        int: Number of slots
        """
    @property
    def slot_size(self) -> float:
        """
        float: Slot size (sec)
        """
    @slot_size.setter
    def slot_size(self, arg1: datetime.timedelta) -> None:
        ...
    @property
    def superslots(self) -> bool:
        """
        bool: Allow superslots
        """
    @superslots.setter
    def superslots(self, arg1: bool) -> None:
        ...
class Score:
    @property
    def achieved_duration(self) -> int:
        """
        Number of consecutive MP's in which goal has been met
        """
    @property
    def goal(self) -> bool:
        """
        Was goal met in MP?
        """
    @property
    def goal_stable(self) -> bool:
        """
        Was goal stable in MP?
        """
    @property
    def mp_score(self) -> int:
        """
        Score for this MP
        """
    @property
    def nbytes_recv(self) -> int:
        """
        Number of bytes sent
        """
    @property
    def nbytes_sent(self) -> int:
        """
        Number of bytes sent
        """
    @property
    def npackets_recv(self) -> int:
        """
        Number of packets sent
        """
    @property
    def npackets_sent(self) -> int:
        """
        Number of packets sent
        """
    @property
    def update_timestamp_recv(self) -> float:
        """
        Timestamp of last update for receive statistics
        """
    @property
    def update_timestamp_sent(self) -> float:
        """
        Timestamp of last update for send statistics
        """
class ScoreMap:
    def __bool__(self) -> bool:
        """
        Check whether the map is nonempty
        """
    @typing.overload
    def __contains__(self, arg0: int) -> bool:
        ...
    @typing.overload
    def __contains__(self, arg0: typing.Any) -> bool:
        ...
    def __delitem__(self, arg0: int) -> None:
        ...
    def __getitem__(self, arg0: int) -> Scores:
        ...
    def __init__(self) -> None:
        ...
    def __iter__(self) -> typing.Iterator:
        ...
    def __len__(self) -> int:
        ...
    def __setitem__(self, arg0: int, arg1: Scores) -> None:
        ...
    def items(self) -> typing.ItemsView[int, Scores]:
        ...
    def keys(self) -> typing.KeysView[int]:
        ...
    def values(self) -> typing.ValuesView[Scores]:
        ...
class Scorer:
    def __init__(self) -> None:
        ...
    def setMandates(self, arg0: MandateMap) -> None:
        """
        Set mandates
        """
    def updateReceivedStatistics(self, arg0: int, arg1: float, arg2: int, arg3: list[int], arg4: list[int]) -> None:
        """
        Update statistics for received data
        """
    def updateScore(self, arg0: int) -> None:
        """
        Update scores up to given MP
        """
    def updateSentStatistics(self, arg0: int, arg1: float, arg2: int, arg3: list[int], arg4: list[int]) -> None:
        """
        Update statistics for sent data
        """
    @property
    def scores(self) -> dict[int, Scores]:
        """
        Scores
        """
class Scores:
    def __bool__(self) -> bool:
        """
        Check whether the list is nonempty
        """
    @typing.overload
    def __delitem__(self, arg0: int) -> None:
        """
        Delete the list elements at index ``i``
        """
    @typing.overload
    def __delitem__(self, arg0: slice) -> None:
        """
        Delete list elements using a slice object
        """
    @typing.overload
    def __getitem__(self, s: slice) -> Scores:
        """
        Retrieve list elements using a slice object
        """
    @typing.overload
    def __getitem__(self, arg0: int) -> Score:
        ...
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, arg0: Scores) -> None:
        """
        Copy constructor
        """
    @typing.overload
    def __init__(self, arg0: typing.Iterable) -> None:
        ...
    def __iter__(self) -> typing.Iterator:
        ...
    def __len__(self) -> int:
        ...
    @typing.overload
    def __setitem__(self, arg0: int, arg1: Score) -> None:
        ...
    @typing.overload
    def __setitem__(self, arg0: slice, arg1: Scores) -> None:
        """
        Assign list elements using a slice object
        """
    def append(self, x: Score) -> None:
        """
        Add an item to the end of the list
        """
    def clear(self) -> None:
        """
        Clear the contents
        """
    @typing.overload
    def extend(self, L: Scores) -> None:
        """
        Extend the list by appending all the items in the given list
        """
    @typing.overload
    def extend(self, L: typing.Iterable) -> None:
        """
        Extend the list by appending all the items in the given list
        """
    def insert(self, i: int, x: Score) -> None:
        """
        Insert an item at a given position.
        """
    @typing.overload
    def pop(self) -> Score:
        """
        Remove and return the last item
        """
    @typing.overload
    def pop(self, i: int) -> Score:
        """
        Remove and return the item at index ``i``
        """
    @property
    def invalid_mp(self) -> int:
        """
        First invalid MP
        """
class SelfTX:
    def __repr__(self) -> str:
        ...
    @property
    def end(self) -> int:
        """
        int: Snapshot sample offset of end of packet
        """
    @end.setter
    def end(self, arg0: int) -> None:
        ...
    @property
    def fc(self) -> float:
        """
        float: Center frequency of packet
        """
    @fc.setter
    def fc(self, arg0: float) -> None:
        ...
    @property
    def fs(self) -> float:
        """
        float: Sample frequency of packet
        """
    @fs.setter
    def fs(self, arg0: float) -> None:
        ...
    @property
    def start(self) -> int:
        """
        int: Snapshot sample offset of start of packet
        """
    @start.setter
    def start(self, arg0: int) -> None:
        ...
class SelfTXs:
    def __bool__(self) -> bool:
        """
        Check whether the list is nonempty
        """
    @typing.overload
    def __delitem__(self, arg0: int) -> None:
        """
        Delete the list elements at index ``i``
        """
    @typing.overload
    def __delitem__(self, arg0: slice) -> None:
        """
        Delete list elements using a slice object
        """
    @typing.overload
    def __getitem__(self, s: slice) -> SelfTXs:
        """
        Retrieve list elements using a slice object
        """
    @typing.overload
    def __getitem__(self, arg0: int) -> SelfTX:
        ...
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, arg0: SelfTXs) -> None:
        """
        Copy constructor
        """
    @typing.overload
    def __init__(self, arg0: typing.Iterable) -> None:
        ...
    def __iter__(self) -> typing.Iterator:
        ...
    def __len__(self) -> int:
        ...
    @typing.overload
    def __setitem__(self, arg0: int, arg1: SelfTX) -> None:
        ...
    @typing.overload
    def __setitem__(self, arg0: slice, arg1: SelfTXs) -> None:
        """
        Assign list elements using a slice object
        """
    def append(self, x: SelfTX) -> None:
        """
        Add an item to the end of the list
        """
    def clear(self) -> None:
        """
        Clear the contents
        """
    @typing.overload
    def extend(self, L: SelfTXs) -> None:
        """
        Extend the list by appending all the items in the given list
        """
    @typing.overload
    def extend(self, L: typing.Iterable) -> None:
        """
        Extend the list by appending all the items in the given list
        """
    def insert(self, i: int, x: SelfTX) -> None:
        """
        Insert an item at a given position.
        """
    @typing.overload
    def pop(self) -> SelfTX:
        """
        Remove and return the last item
        """
    @typing.overload
    def pop(self, i: int) -> SelfTX:
        """
        Remove and return the item at index ``i``
        """
class SendWindow:
    @property
    def long_evm(self) -> float | None:
        """
        Long-term EVM (dB)
        """
    @property
    def long_per(self) -> float | None:
        """
        Long-term packet error rate (unitless)
        """
    @property
    def long_rssi(self) -> float | None:
        """
        Long-term RSSI (dB)
        """
    @property
    def mcsidx(self) -> int:
        """
        MCS index
        """
    @property
    def short_evm(self) -> float | None:
        """
        Short-term EVM (dB)
        """
    @property
    def short_per(self) -> float | None:
        """
        Short-term packet error rate (unitless)
        """
    @property
    def short_rssi(self) -> float | None:
        """
        Short-term RSSI (dB)
        """
class SendWindows:
    def __contains__(self, arg0: int) -> bool:
        ...
    def __getitem__(self, arg0: int) -> SendWindow:
        ...
    def keys(self) -> set[int]:
        ...
class SimpleQueue(Queue, ControllerNetLink):
    class QueueType:
        """
        Members:
        
          FIFO
        
          LIFO
        """
        FIFO: typing.ClassVar[SimpleQueue.QueueType]  # value = <QueueType.FIFO: 0>
        LIFO: typing.ClassVar[SimpleQueue.QueueType]  # value = <QueueType.LIFO: 1>
        __members__: typing.ClassVar[dict[str, SimpleQueue.QueueType]]  # value = {'FIFO': <QueueType.FIFO: 0>, 'LIFO': <QueueType.LIFO: 1>}
        def __eq__(self, other: typing.Any) -> bool:
            ...
        def __getstate__(self) -> int:
            ...
        def __hash__(self) -> int:
            ...
        def __index__(self) -> int:
            ...
        def __init__(self, value: int) -> None:
            ...
        def __int__(self) -> int:
            ...
        def __ne__(self, other: typing.Any) -> bool:
            ...
        def __repr__(self) -> str:
            ...
        def __setstate__(self, state: int) -> None:
            ...
        def __str__(self) -> str:
            ...
        @property
        def name(self) -> str:
            ...
        @property
        def value(self) -> int:
            ...
    FIFO: typing.ClassVar[SimpleQueue.QueueType]  # value = <QueueType.FIFO: 0>
    LIFO: typing.ClassVar[SimpleQueue.QueueType]  # value = <QueueType.LIFO: 1>
    def __init__(self, arg0: SimpleQueue.QueueType) -> None:
        ...
class SizedQueue(Queue, ControllerNetLink):
    @property
    def hi_priority_flows(self) -> set[int]:
        """
        High-priority flows?
        """
    @hi_priority_flows.setter
    def hi_priority_flows(self, arg1: set[int]) -> None:
        ...
class SlottedALOHA(SlottedMAC):
    def __init__(self, radio: Radio, controller: Controller, snapshot_collector: SnapshotCollector, channelizer: Channelizer, synthesizer: Synthesizer, rx_period: float, probability: float) -> None:
        ...
    @property
    def p(self) -> float:
        """
        float: Probability of transmission in a given slot
        """
    @p.setter
    def p(self, arg1: float) -> None:
        ...
    @property
    def slotidx(self) -> int:
        """
        int: Slot index to transmit in
        """
    @slotidx.setter
    def slotidx(self, arg1: int) -> None:
        ...
class SlottedMAC(MAC):
    @property
    def slot_send_lead_time(self) -> datetime.timedelta:
        """
        float: Slot send lead time (sec)
        """
    @slot_send_lead_time.setter
    def slot_send_lead_time(self, arg1: datetime.timedelta) -> None:
        ...
class SmartController(Controller):
    ack_gain: Gain
    broadcast_gain: Gain
    demod_always_ordered: bool
    enforce_ordering: bool
    max_retransmissions: int | None
    def __init__(self, arg0: Neighborhood, arg1: int, arg2: PHY, arg3: int, arg4: int, arg5: list[float | None]) -> None:
        ...
    def broadcastHello(self) -> None:
        ...
    def environmentDiscontinuity(self) -> None:
        """
        Inform the controller that an environmental discontinuity has ocurred
        """
    @property
    def ack_delay(self) -> datetime.timedelta:
        """
        ACK delay (sec)
        """
    @ack_delay.setter
    def ack_delay(self, arg1: datetime.timedelta) -> None:
        ...
    @property
    def ack_delay_estimation_window(self) -> datetime.timedelta:
        """
        ACK delay estimation window (sec)
        """
    @ack_delay_estimation_window.setter
    def ack_delay_estimation_window(self, arg1: datetime.timedelta) -> None:
        ...
    @property
    def aggressive_stats_reset(self) -> bool:
        """
        Aggressively reset PER statistics even when MCS index does not change
        """
    @aggressive_stats_reset.setter
    def aggressive_stats_reset(self, arg1: bool) -> None:
        ...
    @property
    def decrease_retrans_mcsidx(self) -> bool:
        """
        Should we decrease the MCS index of retransmitted packets with a deadline?
        """
    @decrease_retrans_mcsidx.setter
    def decrease_retrans_mcsidx(self, arg1: bool) -> None:
        ...
    @property
    def explicit_nak_window(self) -> bool:
        """
        Explicit NAK window size
        """
    @explicit_nak_window.setter
    def explicit_nak_window(self, arg1: int) -> None:
        ...
    @property
    def explicit_nak_window_duration(self) -> datetime.timedelta:
        """
        Explicit NAK window duration
        """
    @explicit_nak_window_duration.setter
    def explicit_nak_window_duration(self, arg1: datetime.timedelta) -> None:
        ...
    @property
    def long_per_window(self) -> datetime.timedelta:
        """
        Time window used to calculate long-term PE
        """
    @long_per_window.setter
    def long_per_window(self, arg1: datetime.timedelta) -> None:
        ...
    @property
    def long_stats_window(self) -> datetime.timedelta:
        """
        Time window used to calculate long-term statistics, e.g., EVM and RSSI
        """
    @long_stats_window.setter
    def long_stats_window(self, arg1: datetime.timedelta) -> None:
        ...
    @property
    def max_sacks(self) -> int | None:
        """
        Maximum number of SACKs in a packet
        """
    @max_sacks.setter
    def max_sacks(self, arg1: int | None) -> None:
        ...
    @property
    def mcs_fast_adjustment_period(self) -> datetime.timedelta:
        """
        MCS fast adjustment period after environment discontinuity
        """
    @mcs_fast_adjustment_period.setter
    def mcs_fast_adjustment_period(self, arg1: datetime.timedelta) -> None:
        ...
    @property
    def mcsidx_ack(self) -> int:
        """
        ACK MCS index
        """
    @mcsidx_ack.setter
    def mcsidx_ack(self, arg1: int) -> None:
        ...
    @property
    def mcsidx_alpha(self) -> float:
        """
        MCS index learning alpha
        """
    @mcsidx_alpha.setter
    def mcsidx_alpha(self, arg1: float) -> None:
        ...
    @property
    def mcsidx_broadcast(self) -> int:
        """
        Broadcast MCS index
        """
    @mcsidx_broadcast.setter
    def mcsidx_broadcast(self, arg1: int) -> None:
        ...
    @property
    def mcsidx_down_per_threshold(self) -> float:
        """
        PER threshold for decreasing modulation level
        """
    @mcsidx_down_per_threshold.setter
    def mcsidx_down_per_threshold(self, arg1: float) -> None:
        ...
    @property
    def mcsidx_init(self) -> int:
        """
        Initial MCS index
        """
    @mcsidx_init.setter
    def mcsidx_init(self, arg1: int) -> None:
        ...
    @property
    def mcsidx_max(self) -> int:
        """
        Maximum MCS index
        """
    @mcsidx_max.setter
    def mcsidx_max(self, arg1: int) -> None:
        ...
    @property
    def mcsidx_min(self) -> int:
        """
        Minimum MCS index
        """
    @mcsidx_min.setter
    def mcsidx_min(self, arg1: int) -> None:
        ...
    @property
    def mcsidx_prob_floor(self) -> float:
        """
        MCS transition probability floor
        """
    @mcsidx_prob_floor.setter
    def mcsidx_prob_floor(self, arg1: float) -> None:
        ...
    @property
    def mcsidx_up_per_threshold(self) -> float:
        """
        PER threshold for increasing modulation level
        """
    @mcsidx_up_per_threshold.setter
    def mcsidx_up_per_threshold(self, arg1: float) -> None:
        ...
    @property
    def mcu(self) -> int:
        """
        Maximum number of extra control bytes beyond MTU
        """
    @mcu.setter
    def mcu(self, arg1: int) -> None:
        ...
    @property
    def min_retransmission_delay(self) -> datetime.timedelta:
        """
        Minimum retransmission delay (sec)
        """
    @min_retransmission_delay.setter
    def min_retransmission_delay(self, arg1: datetime.timedelta) -> None:
        ...
    @property
    def move_along(self) -> bool:
        """
        Should we always move the send window along even if it's full?
        """
    @move_along.setter
    def move_along(self, arg1: bool) -> None:
        ...
    @property
    def proactive_unreachable(self) -> bool:
        """
        Should we proactively test for unreachable nodes?
        """
    @proactive_unreachable.setter
    def proactive_unreachable(self, arg1: bool) -> None:
        ...
    @property
    def purge_unreachable(self) -> bool:
        """
        Should we purge unreachable nodes?
        """
    @purge_unreachable.setter
    def purge_unreachable(self, arg1: bool) -> None:
        ...
    @property
    def recv(self) -> ReceiveWindows:
        """
        ReceiveWindows : map from node to ReceiveWindow
        """
    @property
    def retransmission_delay(self) -> datetime.timedelta:
        """
        Retransmission delay (sec)
        """
    @retransmission_delay.setter
    def retransmission_delay(self, arg1: datetime.timedelta) -> None:
        ...
    @property
    def retransmission_delay_slop(self) -> float:
        """
        Retransmission delay safety factor
        """
    @retransmission_delay_slop.setter
    def retransmission_delay_slop(self, arg1: float) -> None:
        ...
    @property
    def sack_delay(self) -> datetime.timedelta:
        """
        SACK delay (sec)
        """
    @sack_delay.setter
    def sack_delay(self, arg1: datetime.timedelta) -> None:
        ...
    @property
    def selective_ack(self) -> bool:
        """
        Send selective ACK's?
        """
    @selective_ack.setter
    def selective_ack(self, arg1: bool) -> None:
        ...
    @property
    def selective_ack_feedback_delay(self) -> datetime.timedelta:
        """
        Selective ACK feedback delay (sec)
        """
    @selective_ack_feedback_delay.setter
    def selective_ack_feedback_delay(self, arg1: datetime.timedelta) -> None:
        ...
    @property
    def send(self) -> SendWindows:
        """
        SendWindows : map from node to SendWindow
        """
    @property
    def short_per_window(self) -> datetime.timedelta:
        """
        Time window used to calculate short-term PER
        """
    @short_per_window.setter
    def short_per_window(self, arg1: datetime.timedelta) -> None:
        ...
    @property
    def short_stats_window(self) -> datetime.timedelta:
        """
        Time window used to calculate long-term statistics, e.g., EVM and RSSI
        """
    @short_stats_window.setter
    def short_stats_window(self, arg1: datetime.timedelta) -> None:
        ...
    @property
    def timestamps(self) -> Timestamps:
        """
        Timestamps
        """
    @property
    def unreachable_timeout(self) -> datetime.timedelta | None:
        """
        Threshold for marking node unreachable (sec)
        """
    @unreachable_timeout.setter
    def unreachable_timeout(self, arg1: datetime.timedelta | None) -> None:
        ...
class Snapshot:
    def __repr__(self) -> str:
        ...
    @property
    def combined_slots(self) -> IQBuf | None:
        """
        Optional[IQBuf]: Combined IQ data for all slots in snapshot
        """
    @property
    def selftx(self) -> SelfTXs:
        """
        Sequence[SelfTX]: Self-transmission events
        """
    @property
    def slots(self) -> list[IQBuf]:
        """
        Sequence[IQBuf]: Slots in snapshot (IQ data)
        """
    @property
    def timestamp(self) -> datetime.timedelta:
        """
        datetime.timedelta: Snapshot timestamp
        """
class SnapshotCollector:
    def __init__(self) -> None:
        ...
    def finalize(self) -> Snapshot:
        """
        Finalize snapshot collection, returning the collected snapshot
        """
    def next(self) -> Snapshot:
        """
        Get current snapshot and start a new snapshot immediately
        """
    def start(self) -> None:
        """
        Start snapshot collection
        """
    def stop(self) -> None:
        """
        Stop snapshot collection
        """
    @property
    def active(self) -> bool:
        """
        bool: Is snapshot collection active?
        """
class Synthesizer:
    @property
    def channels(self) -> PHYChannels:
        """
        Sequence[Channel]: TX channels
        """
    @channels.setter
    def channels(self, arg1: PHYChannels) -> None:
        ...
    @property
    def high_water_mark(self) -> int | None:
        """
        int: Maximum number of modulated samples to queue.
        """
    @high_water_mark.setter
    def high_water_mark(self, arg1: int | None) -> None:
        ...
    @property
    def schedule(self) -> Schedule:
        """
        Schedule: MAC schedule
        """
    @schedule.setter
    def schedule(self, arg1: typing.Any) -> None:
        ...
    @property
    def sink(self) -> NetInPull:
        ...
    @property
    def tx_rate(self) -> float:
        """
        float: TX rate
        """
    @tx_rate.setter
    def tx_rate(self, arg1: float) -> None:
        ...
class TDChannelizer(Channelizer):
    def __init__(self, channels: PHYChannels, rx_rate: float, nthreads: int) -> None:
        ...
class TDMA(SlottedMAC):
    def __init__(self, radio: Radio, controller: Controller, snapshot_collector: SnapshotCollector, channelizer: Channelizer, synthesizer: Synthesizer, rx_period: float) -> None:
        ...
class TDSynthesizer(Synthesizer):
    def __init__(self, channels: PHYChannels, tx_rate: float, nthreads: int) -> None:
        ...
class TailDropQueue(SizedQueue, ControllerNetLink):
    def __init__(self, arg0: int) -> None:
        ...
    @property
    def max_size(self) -> int:
        """
        Maximum queue size (bytes)
        """
    @max_size.setter
    def max_size(self, arg1: int) -> None:
        ...
class TimeKeeper:
    pass
class Timestamps:
    def __contains__(self, arg0: int) -> bool:
        ...
    def __getitem__(self, arg0: int) -> dict[int, tuple[float, float]]:
        ...
    def keys(self) -> set[int]:
        ...
class TunTap:
    def __init__(self, iface: str, ipaddr: str, netmask: str, macaddr: str, persistent: bool, mtu: int, node_id: int) -> None:
        ...
    @property
    def accept_redirects(self) -> int:
        """
        int: accept_redirects flag
        """
    @accept_redirects.setter
    def accept_redirects(self, arg1: int) -> None:
        ...
    @property
    def iface(self) -> str:
        """
        str: interface name
        """
    @property
    def mtu(self) -> int:
        """
        int: Maximum Transmission Unit
        """
    @property
    def send_redirects(self) -> int:
        """
        int: send_redirects flag
        """
    @send_redirects.setter
    def send_redirects(self, arg1: int) -> None:
        ...
    @property
    def sink(self) -> RadioInPush:
        ...
    @property
    def source(self) -> NetOutPush:
        ...
class USRP(Radio, TimeKeeper):
    auto_dc_offset: bool
    clock_rate: float
    clock_source: str
    rx_antenna: str
    rx_max_samps: int
    rx_subdev_spec: str
    time_source: str
    tx_antenna: str
    tx_max_samps: int
    tx_subdev_spec: str
    def __init__(self, dev_addr: str) -> None:
        ...
    def getClockSource(self, arg0: int) -> str:
        ...
    def getClockSources(self, arg0: int) -> list[str]:
        ...
    def getMasterClockRate(self, arg0: int) -> float:
        ...
    def getMasterClockRateRanges(self, arg0: int) -> list[tuple[float, float]]:
        ...
    def getRXAntenna(self, chan: int = 0) -> str:
        ...
    def getRXAntennas(self, chan: int = 0) -> list[str]:
        ...
    def getRXSubdevSpec(self, mboard: int = 0) -> str:
        ...
    def getTXAntenna(self, chan: int = 0) -> str:
        ...
    def getTXAntennas(self, chan: int = 0) -> list[str]:
        ...
    def getTXSubdevSpec(self, mboard: int = 0) -> str:
        ...
    def getTimeSource(self, arg0: int) -> str:
        ...
    def getTimeSources(self, arg0: int) -> list[str]:
        ...
    def setClockSource(self, arg0: str, arg1: int) -> None:
        ...
    def setMasterClockRate(self, arg0: float, arg1: int) -> None:
        ...
    def setRXAntenna(self, antenna: str, chan: int = 0) -> None:
        ...
    def setRXSubdevSpec(self, spec: str, mboard: int = 0) -> None:
        ...
    def setTXAntenna(self, antenna: str, chan: int = 0) -> None:
        ...
    def setTXSubdevSpec(self, spec: str, mboard: int = 0) -> None:
        ...
    def setTimeSource(self, arg0: str, arg1: int) -> None:
        ...
    def syncTime(self, random_bias: float = 0.0, use_pps: bool = False) -> None:
        ...
    @property
    def clock_rate_ranges(self) -> list[tuple[float, float]]:
        ...
    @property
    def clock_sources(self) -> list[str]:
        ...
    @property
    def mboard(self) -> str:
        ...
    @property
    def rx_antennas(self) -> list[str]:
        ...
    @property
    def time_sources(self) -> list[str]:
        ...
    @property
    def tx_antennas(self) -> list[str]:
        ...
class WallClock(MonoClock):
    t0: typing.ClassVar[datetime.timedelta]  # value = datetime.timedelta(0)
    offset: datetime.timedelta
    skew: float
class WallTimeWindowEstimator(DoubleEstimator):
    def reset(self) -> None:
        """
        Reset the estimate
        """
    @property
    def end(self) -> datetime.timedelta | None:
        """
        The end of the time window (sec)
        """
    @property
    def start(self) -> datetime.timedelta | None:
        """
        The start of the time window (sec)
        """
    @property
    def time_window(self) -> datetime.timedelta:
        """
        The time window (sec)
        """
    @time_window.setter
    def time_window(self, arg1: datetime.timedelta) -> None:
        ...
class WallTimeWindowMax(WallTimeWindowEstimator):
    pass
class WallTimeWindowMean(WallTimeWindowEstimator):
    pass
class WallTimeWindowMeanRate(WallTimeWindowMean):
    pass
class WallTimeWindowMin(WallTimeWindowEstimator):
    pass
class WorkQueue:
    def addThreads(self, arg0: int) -> None:
        """
        Add workers
        """
clock: WallClock  # value = <dragonradiocpp.radio.WallClock object>
work_queue: WorkQueue  # value = <dragonradiocpp.radio.WorkQueue object>
