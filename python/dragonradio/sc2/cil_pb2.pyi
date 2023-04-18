from google.protobuf import wrappers_pb2 as _wrappers_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar, Iterable, Mapping, Optional, Union

DESCRIPTOR: _descriptor.FileDescriptor

class CilMessage(_message.Message):
    __slots__ = ["detailed_performance", "hello", "incumbent_notify", "location_update", "msg_count", "network_type", "sender_network_id", "spectrum_usage", "timestamp"]
    DETAILED_PERFORMANCE_FIELD_NUMBER: ClassVar[int]
    HELLO_FIELD_NUMBER: ClassVar[int]
    INCUMBENT_NOTIFY_FIELD_NUMBER: ClassVar[int]
    LOCATION_UPDATE_FIELD_NUMBER: ClassVar[int]
    MSG_COUNT_FIELD_NUMBER: ClassVar[int]
    NETWORK_TYPE_FIELD_NUMBER: ClassVar[int]
    SENDER_NETWORK_ID_FIELD_NUMBER: ClassVar[int]
    SPECTRUM_USAGE_FIELD_NUMBER: ClassVar[int]
    TIMESTAMP_FIELD_NUMBER: ClassVar[int]
    detailed_performance: DetailedPerformance
    hello: Hello
    incumbent_notify: IncumbentNotify
    location_update: LocationUpdate
    msg_count: int
    network_type: NetworkType
    sender_network_id: int
    spectrum_usage: SpectrumUsage
    timestamp: TimeStamp
    def __init__(self, sender_network_id: Optional[int] = ..., msg_count: Optional[int] = ..., timestamp: Optional[Union[TimeStamp, Mapping]] = ..., network_type: Optional[Union[NetworkType, Mapping]] = ..., hello: Optional[Union[Hello, Mapping]] = ..., spectrum_usage: Optional[Union[SpectrumUsage, Mapping]] = ..., location_update: Optional[Union[LocationUpdate, Mapping]] = ..., detailed_performance: Optional[Union[DetailedPerformance, Mapping]] = ..., incumbent_notify: Optional[Union[IncumbentNotify, Mapping]] = ...) -> None: ...

class CilVersion(_message.Message):
    __slots__ = ["major", "minor", "patch"]
    MAJOR_FIELD_NUMBER: ClassVar[int]
    MINOR_FIELD_NUMBER: ClassVar[int]
    PATCH_FIELD_NUMBER: ClassVar[int]
    major: int
    minor: int
    patch: int
    def __init__(self, major: Optional[int] = ..., minor: Optional[int] = ..., patch: Optional[int] = ...) -> None: ...

class DetailedPerformance(_message.Message):
    __slots__ = ["mandate_count", "mandates", "mandates_achieved", "scoring_point_threshold", "timestamp", "total_score_achieved"]
    MANDATES_ACHIEVED_FIELD_NUMBER: ClassVar[int]
    MANDATES_FIELD_NUMBER: ClassVar[int]
    MANDATE_COUNT_FIELD_NUMBER: ClassVar[int]
    SCORING_POINT_THRESHOLD_FIELD_NUMBER: ClassVar[int]
    TIMESTAMP_FIELD_NUMBER: ClassVar[int]
    TOTAL_SCORE_ACHIEVED_FIELD_NUMBER: ClassVar[int]
    mandate_count: int
    mandates: _containers.RepeatedCompositeFieldContainer[MandatePerformance]
    mandates_achieved: int
    scoring_point_threshold: int
    timestamp: TimeStamp
    total_score_achieved: int
    def __init__(self, mandate_count: Optional[int] = ..., timestamp: Optional[Union[TimeStamp, Mapping]] = ..., mandates: Optional[Iterable[Union[MandatePerformance, Mapping]]] = ..., mandates_achieved: Optional[int] = ..., total_score_achieved: Optional[int] = ..., scoring_point_threshold: Optional[int] = ...) -> None: ...

class Hello(_message.Message):
    __slots__ = ["version"]
    VERSION_FIELD_NUMBER: ClassVar[int]
    version: CilVersion
    def __init__(self, version: Optional[Union[CilVersion, Mapping]] = ...) -> None: ...

class IncumbentActiveInfo(_message.Message):
    __slots__ = ["bandwidth", "center_freq", "incumbent_id", "inr", "msg_type", "report_time", "threshold", "threshold_exceeded"]
    class MessageType(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = []
    BANDWIDTH_FIELD_NUMBER: ClassVar[int]
    CENTER_FREQ_FIELD_NUMBER: ClassVar[int]
    INCUMBENT_ID_FIELD_NUMBER: ClassVar[int]
    INR_FIELD_NUMBER: ClassVar[int]
    MSG_TYPE_FIELD_NUMBER: ClassVar[int]
    REPORT: IncumbentActiveInfo.MessageType
    REPORT_TIME_FIELD_NUMBER: ClassVar[int]
    THRESHOLD_EXCEEDED_FIELD_NUMBER: ClassVar[int]
    THRESHOLD_FIELD_NUMBER: ClassVar[int]
    UNKNOWN: IncumbentActiveInfo.MessageType
    VIOLATION: IncumbentActiveInfo.MessageType
    bandwidth: int
    center_freq: int
    incumbent_id: int
    inr: float
    msg_type: IncumbentActiveInfo.MessageType
    report_time: TimeStamp
    threshold: float
    threshold_exceeded: bool
    def __init__(self, incumbent_id: Optional[int] = ..., msg_type: Optional[Union[IncumbentActiveInfo.MessageType, str]] = ..., report_time: Optional[Union[TimeStamp, Mapping]] = ..., inr: Optional[float] = ..., threshold: Optional[float] = ..., center_freq: Optional[int] = ..., bandwidth: Optional[int] = ..., threshold_exceeded: bool = ...) -> None: ...

class IncumbentNotify(_message.Message):
    __slots__ = ["data", "data_active"]
    DATA_ACTIVE_FIELD_NUMBER: ClassVar[int]
    DATA_FIELD_NUMBER: ClassVar[int]
    data: IncumbentPassiveInfo
    data_active: IncumbentActiveInfo
    def __init__(self, data: Optional[Union[IncumbentPassiveInfo, Mapping]] = ..., data_active: Optional[Union[IncumbentActiveInfo, Mapping]] = ...) -> None: ...

class IncumbentPassiveInfo(_message.Message):
    __slots__ = ["bandwidth", "center_freq", "incumbent_id", "msg_type", "power", "report_time", "threshold", "threshold_exceeded"]
    class MessageType(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = []
    BANDWIDTH_FIELD_NUMBER: ClassVar[int]
    CENTER_FREQ_FIELD_NUMBER: ClassVar[int]
    INCUMBENT_ID_FIELD_NUMBER: ClassVar[int]
    MSG_TYPE_FIELD_NUMBER: ClassVar[int]
    POWER_FIELD_NUMBER: ClassVar[int]
    REPORT: IncumbentPassiveInfo.MessageType
    REPORT_TIME_FIELD_NUMBER: ClassVar[int]
    THRESHOLD_EXCEEDED_FIELD_NUMBER: ClassVar[int]
    THRESHOLD_FIELD_NUMBER: ClassVar[int]
    UNKNOWN: IncumbentPassiveInfo.MessageType
    VIOLATION: IncumbentPassiveInfo.MessageType
    bandwidth: int
    center_freq: int
    incumbent_id: int
    msg_type: IncumbentPassiveInfo.MessageType
    power: float
    report_time: TimeStamp
    threshold: float
    threshold_exceeded: bool
    def __init__(self, incumbent_id: Optional[int] = ..., msg_type: Optional[Union[IncumbentPassiveInfo.MessageType, str]] = ..., report_time: Optional[Union[TimeStamp, Mapping]] = ..., power: Optional[float] = ..., threshold: Optional[float] = ..., center_freq: Optional[int] = ..., bandwidth: Optional[int] = ..., threshold_exceeded: bool = ...) -> None: ...

class Location(_message.Message):
    __slots__ = ["elevation", "latitude", "longitude"]
    ELEVATION_FIELD_NUMBER: ClassVar[int]
    LATITUDE_FIELD_NUMBER: ClassVar[int]
    LONGITUDE_FIELD_NUMBER: ClassVar[int]
    elevation: float
    latitude: float
    longitude: float
    def __init__(self, latitude: Optional[float] = ..., longitude: Optional[float] = ..., elevation: Optional[float] = ...) -> None: ...

class LocationInfo(_message.Message):
    __slots__ = ["location", "radio_id", "timestamp"]
    LOCATION_FIELD_NUMBER: ClassVar[int]
    RADIO_ID_FIELD_NUMBER: ClassVar[int]
    TIMESTAMP_FIELD_NUMBER: ClassVar[int]
    location: Location
    radio_id: int
    timestamp: TimeStamp
    def __init__(self, radio_id: Optional[int] = ..., location: Optional[Union[Location, Mapping]] = ..., timestamp: Optional[Union[TimeStamp, Mapping]] = ...) -> None: ...

class LocationUpdate(_message.Message):
    __slots__ = ["locations"]
    LOCATIONS_FIELD_NUMBER: ClassVar[int]
    locations: _containers.RepeatedCompositeFieldContainer[LocationInfo]
    def __init__(self, locations: Optional[Iterable[Union[LocationInfo, Mapping]]] = ...) -> None: ...

class MandatePerformance(_message.Message):
    __slots__ = ["achieved_duration", "desired_voxels", "flow_id", "hold_period", "point_value", "radio_ids", "scalar_performance"]
    ACHIEVED_DURATION_FIELD_NUMBER: ClassVar[int]
    DESIRED_VOXELS_FIELD_NUMBER: ClassVar[int]
    FLOW_ID_FIELD_NUMBER: ClassVar[int]
    HOLD_PERIOD_FIELD_NUMBER: ClassVar[int]
    POINT_VALUE_FIELD_NUMBER: ClassVar[int]
    RADIO_IDS_FIELD_NUMBER: ClassVar[int]
    SCALAR_PERFORMANCE_FIELD_NUMBER: ClassVar[int]
    achieved_duration: int
    desired_voxels: _containers.RepeatedCompositeFieldContainer[SpectrumVoxel]
    flow_id: int
    hold_period: int
    point_value: int
    radio_ids: _containers.RepeatedScalarFieldContainer[int]
    scalar_performance: float
    def __init__(self, scalar_performance: Optional[float] = ..., radio_ids: Optional[Iterable[int]] = ..., desired_voxels: Optional[Iterable[Union[SpectrumVoxel, Mapping]]] = ..., flow_id: Optional[int] = ..., hold_period: Optional[int] = ..., achieved_duration: Optional[int] = ..., point_value: Optional[int] = ...) -> None: ...

class NetworkType(_message.Message):
    __slots__ = ["network_type"]
    class Type(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = []
    COMPETITOR: NetworkType.Type
    INCUMBENT_ACTIVE: NetworkType.Type
    INCUMBENT_DSRC: NetworkType.Type
    INCUMBENT_JAMMER: NetworkType.Type
    INCUMBENT_PASSIVE: NetworkType.Type
    NETWORK_TYPE_FIELD_NUMBER: ClassVar[int]
    UNKNOWN: NetworkType.Type
    network_type: NetworkType.Type
    def __init__(self, network_type: Optional[Union[NetworkType.Type, str]] = ...) -> None: ...

class ReceiverInfo(_message.Message):
    __slots__ = ["power_db", "radio_id"]
    POWER_DB_FIELD_NUMBER: ClassVar[int]
    RADIO_ID_FIELD_NUMBER: ClassVar[int]
    power_db: _wrappers_pb2.DoubleValue
    radio_id: int
    def __init__(self, radio_id: Optional[int] = ..., power_db: Optional[Union[_wrappers_pb2.DoubleValue, Mapping]] = ...) -> None: ...

class SpectrumUsage(_message.Message):
    __slots__ = ["voxels"]
    VOXELS_FIELD_NUMBER: ClassVar[int]
    voxels: _containers.RepeatedCompositeFieldContainer[SpectrumVoxelUsage]
    def __init__(self, voxels: Optional[Iterable[Union[SpectrumVoxelUsage, Mapping]]] = ...) -> None: ...

class SpectrumVoxel(_message.Message):
    __slots__ = ["duty_cycle", "freq_end", "freq_start", "period_time", "slot_time", "time_end", "time_start"]
    DUTY_CYCLE_FIELD_NUMBER: ClassVar[int]
    FREQ_END_FIELD_NUMBER: ClassVar[int]
    FREQ_START_FIELD_NUMBER: ClassVar[int]
    PERIOD_TIME_FIELD_NUMBER: ClassVar[int]
    SLOT_TIME_FIELD_NUMBER: ClassVar[int]
    TIME_END_FIELD_NUMBER: ClassVar[int]
    TIME_START_FIELD_NUMBER: ClassVar[int]
    duty_cycle: _wrappers_pb2.DoubleValue
    freq_end: float
    freq_start: float
    period_time: _wrappers_pb2.DoubleValue
    slot_time: _wrappers_pb2.DoubleValue
    time_end: TimeStamp
    time_start: TimeStamp
    def __init__(self, freq_start: Optional[float] = ..., freq_end: Optional[float] = ..., time_start: Optional[Union[TimeStamp, Mapping]] = ..., time_end: Optional[Union[TimeStamp, Mapping]] = ..., duty_cycle: Optional[Union[_wrappers_pb2.DoubleValue, Mapping]] = ..., period_time: Optional[Union[_wrappers_pb2.DoubleValue, Mapping]] = ..., slot_time: Optional[Union[_wrappers_pb2.DoubleValue, Mapping]] = ...) -> None: ...

class SpectrumVoxelUsage(_message.Message):
    __slots__ = ["measured_data", "receiver_info", "spectrum_voxel", "transmitter_info", "utility"]
    MEASURED_DATA_FIELD_NUMBER: ClassVar[int]
    RECEIVER_INFO_FIELD_NUMBER: ClassVar[int]
    SPECTRUM_VOXEL_FIELD_NUMBER: ClassVar[int]
    TRANSMITTER_INFO_FIELD_NUMBER: ClassVar[int]
    UTILITY_FIELD_NUMBER: ClassVar[int]
    measured_data: bool
    receiver_info: _containers.RepeatedCompositeFieldContainer[ReceiverInfo]
    spectrum_voxel: SpectrumVoxel
    transmitter_info: TransmitterInfo
    utility: _wrappers_pb2.DoubleValue
    def __init__(self, spectrum_voxel: Optional[Union[SpectrumVoxel, Mapping]] = ..., transmitter_info: Optional[Union[TransmitterInfo, Mapping]] = ..., receiver_info: Optional[Iterable[Union[ReceiverInfo, Mapping]]] = ..., utility: Optional[Union[_wrappers_pb2.DoubleValue, Mapping]] = ..., measured_data: bool = ...) -> None: ...

class TimeStamp(_message.Message):
    __slots__ = ["picoseconds", "seconds"]
    PICOSECONDS_FIELD_NUMBER: ClassVar[int]
    SECONDS_FIELD_NUMBER: ClassVar[int]
    picoseconds: int
    seconds: int
    def __init__(self, seconds: Optional[int] = ..., picoseconds: Optional[int] = ...) -> None: ...

class TransmitterInfo(_message.Message):
    __slots__ = ["mac_cca", "power_db", "radio_id"]
    MAC_CCA_FIELD_NUMBER: ClassVar[int]
    POWER_DB_FIELD_NUMBER: ClassVar[int]
    RADIO_ID_FIELD_NUMBER: ClassVar[int]
    mac_cca: bool
    power_db: _wrappers_pb2.DoubleValue
    radio_id: int
    def __init__(self, radio_id: Optional[int] = ..., power_db: Optional[Union[_wrappers_pb2.DoubleValue, Mapping]] = ..., mac_cca: bool = ...) -> None: ...
