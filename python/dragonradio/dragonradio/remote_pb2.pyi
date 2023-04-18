from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar, Mapping, Optional, Union

ACTIVE: State
BOOTING: State
DESCRIPTOR: _descriptor.FileDescriptor
ERROR: State
FINISHED: State
OFF: State
READY: State
START: RadioCommand
STATUS: RadioCommand
STOP: RadioCommand
STOPPING: State

class Environment(_message.Message):
    __slots__ = ["environment"]
    ENVIRONMENT_FIELD_NUMBER: ClassVar[int]
    environment: str
    def __init__(self, environment: Optional[str] = ...) -> None: ...

class MandatedOutcomes(_message.Message):
    __slots__ = ["goals"]
    GOALS_FIELD_NUMBER: ClassVar[int]
    goals: str
    def __init__(self, goals: Optional[str] = ...) -> None: ...

class Request(_message.Message):
    __slots__ = ["radio_command", "timestamp", "update_environment", "update_mandated_outcomes"]
    RADIO_COMMAND_FIELD_NUMBER: ClassVar[int]
    TIMESTAMP_FIELD_NUMBER: ClassVar[int]
    UPDATE_ENVIRONMENT_FIELD_NUMBER: ClassVar[int]
    UPDATE_MANDATED_OUTCOMES_FIELD_NUMBER: ClassVar[int]
    radio_command: RadioCommand
    timestamp: float
    update_environment: Environment
    update_mandated_outcomes: MandatedOutcomes
    def __init__(self, timestamp: Optional[float] = ..., radio_command: Optional[Union[RadioCommand, str]] = ..., update_mandated_outcomes: Optional[Union[MandatedOutcomes, Mapping]] = ..., update_environment: Optional[Union[Environment, Mapping]] = ...) -> None: ...

class Response(_message.Message):
    __slots__ = ["status"]
    STATUS_FIELD_NUMBER: ClassVar[int]
    status: Status
    def __init__(self, status: Optional[Union[Status, Mapping]] = ...) -> None: ...

class Status(_message.Message):
    __slots__ = ["info", "state"]
    INFO_FIELD_NUMBER: ClassVar[int]
    STATE_FIELD_NUMBER: ClassVar[int]
    info: str
    state: State
    def __init__(self, state: Optional[Union[State, str]] = ..., info: Optional[str] = ...) -> None: ...

class State(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
    __slots__ = []

class RadioCommand(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
    __slots__ = []
