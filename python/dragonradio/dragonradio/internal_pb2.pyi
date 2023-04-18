from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar, Iterable, Mapping, Optional, Union

DESCRIPTOR: _descriptor.FileDescriptor

class FlowStats(_message.Message):
    __slots__ = ["dest", "first_mp", "flow_uid", "nbytes", "npackets", "src"]
    DEST_FIELD_NUMBER: ClassVar[int]
    FIRST_MP_FIELD_NUMBER: ClassVar[int]
    FLOW_UID_FIELD_NUMBER: ClassVar[int]
    NBYTES_FIELD_NUMBER: ClassVar[int]
    NPACKETS_FIELD_NUMBER: ClassVar[int]
    SRC_FIELD_NUMBER: ClassVar[int]
    dest: int
    first_mp: int
    flow_uid: int
    nbytes: _containers.RepeatedScalarFieldContainer[int]
    npackets: _containers.RepeatedScalarFieldContainer[int]
    src: int
    def __init__(self, flow_uid: Optional[int] = ..., src: Optional[int] = ..., dest: Optional[int] = ..., first_mp: Optional[int] = ..., npackets: Optional[Iterable[int]] = ..., nbytes: Optional[Iterable[int]] = ...) -> None: ...

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
    __slots__ = ["location", "timestamp"]
    LOCATION_FIELD_NUMBER: ClassVar[int]
    TIMESTAMP_FIELD_NUMBER: ClassVar[int]
    location: Location
    timestamp: TimeStamp
    def __init__(self, location: Optional[Union[Location, Mapping]] = ..., timestamp: Optional[Union[TimeStamp, Mapping]] = ...) -> None: ...

class Message(_message.Message):
    __slots__ = ["schedule", "status"]
    SCHEDULE_FIELD_NUMBER: ClassVar[int]
    STATUS_FIELD_NUMBER: ClassVar[int]
    schedule: Schedule
    status: Status
    def __init__(self, status: Optional[Union[Status, Mapping]] = ..., schedule: Optional[Union[Schedule, Mapping]] = ...) -> None: ...

class Schedule(_message.Message):
    __slots__ = ["bandwidth", "frequency", "nchannels", "nodes", "nslots", "scenario_start_time", "schedule", "seq"]
    BANDWIDTH_FIELD_NUMBER: ClassVar[int]
    FREQUENCY_FIELD_NUMBER: ClassVar[int]
    NCHANNELS_FIELD_NUMBER: ClassVar[int]
    NODES_FIELD_NUMBER: ClassVar[int]
    NSLOTS_FIELD_NUMBER: ClassVar[int]
    SCENARIO_START_TIME_FIELD_NUMBER: ClassVar[int]
    SCHEDULE_FIELD_NUMBER: ClassVar[int]
    SEQ_FIELD_NUMBER: ClassVar[int]
    bandwidth: float
    frequency: float
    nchannels: int
    nodes: _containers.RepeatedScalarFieldContainer[int]
    nslots: int
    scenario_start_time: float
    schedule: _containers.RepeatedScalarFieldContainer[int]
    seq: int
    def __init__(self, frequency: Optional[float] = ..., bandwidth: Optional[float] = ..., scenario_start_time: Optional[float] = ..., seq: Optional[int] = ..., nchannels: Optional[int] = ..., nslots: Optional[int] = ..., nodes: Optional[Iterable[int]] = ..., schedule: Optional[Iterable[int]] = ...) -> None: ...

class SpectrumStats(_message.Message):
    __slots__ = ["end", "start", "voxels"]
    END_FIELD_NUMBER: ClassVar[int]
    START_FIELD_NUMBER: ClassVar[int]
    VOXELS_FIELD_NUMBER: ClassVar[int]
    end: TimeStamp
    start: TimeStamp
    voxels: _containers.RepeatedCompositeFieldContainer[SpectrumUsage]
    def __init__(self, start: Optional[Union[TimeStamp, Mapping]] = ..., end: Optional[Union[TimeStamp, Mapping]] = ..., voxels: Optional[Iterable[Union[SpectrumUsage, Mapping]]] = ...) -> None: ...

class SpectrumUsage(_message.Message):
    __slots__ = ["duty_cycle", "f_end", "f_start"]
    DUTY_CYCLE_FIELD_NUMBER: ClassVar[int]
    F_END_FIELD_NUMBER: ClassVar[int]
    F_START_FIELD_NUMBER: ClassVar[int]
    duty_cycle: float
    f_end: float
    f_start: float
    def __init__(self, f_start: Optional[float] = ..., f_end: Optional[float] = ..., duty_cycle: Optional[float] = ...) -> None: ...

class Status(_message.Message):
    __slots__ = ["loc", "radio_id", "sink_flows", "source_flows", "spectrum_stats", "timestamp"]
    LOC_FIELD_NUMBER: ClassVar[int]
    RADIO_ID_FIELD_NUMBER: ClassVar[int]
    SINK_FLOWS_FIELD_NUMBER: ClassVar[int]
    SOURCE_FLOWS_FIELD_NUMBER: ClassVar[int]
    SPECTRUM_STATS_FIELD_NUMBER: ClassVar[int]
    TIMESTAMP_FIELD_NUMBER: ClassVar[int]
    loc: LocationInfo
    radio_id: int
    sink_flows: _containers.RepeatedCompositeFieldContainer[FlowStats]
    source_flows: _containers.RepeatedCompositeFieldContainer[FlowStats]
    spectrum_stats: _containers.RepeatedCompositeFieldContainer[SpectrumStats]
    timestamp: TimeStamp
    def __init__(self, radio_id: Optional[int] = ..., timestamp: Optional[Union[TimeStamp, Mapping]] = ..., loc: Optional[Union[LocationInfo, Mapping]] = ..., source_flows: Optional[Iterable[Union[FlowStats, Mapping]]] = ..., sink_flows: Optional[Iterable[Union[FlowStats, Mapping]]] = ..., spectrum_stats: Optional[Iterable[Union[SpectrumStats, Mapping]]] = ...) -> None: ...

class TimeStamp(_message.Message):
    __slots__ = ["picoseconds", "seconds"]
    PICOSECONDS_FIELD_NUMBER: ClassVar[int]
    SECONDS_FIELD_NUMBER: ClassVar[int]
    picoseconds: int
    seconds: int
    def __init__(self, seconds: Optional[int] = ..., picoseconds: Optional[int] = ...) -> None: ...
