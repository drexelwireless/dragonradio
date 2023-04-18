from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar, Iterable, Mapping, Optional, Union

DESCRIPTOR: _descriptor.FileDescriptor

class Inform(_message.Message):
    __slots__ = ["client_nonce", "keepalive_seconds", "neighbors"]
    CLIENT_NONCE_FIELD_NUMBER: ClassVar[int]
    KEEPALIVE_SECONDS_FIELD_NUMBER: ClassVar[int]
    NEIGHBORS_FIELD_NUMBER: ClassVar[int]
    client_nonce: int
    keepalive_seconds: float
    neighbors: _containers.RepeatedScalarFieldContainer[int]
    def __init__(self, client_nonce: Optional[int] = ..., keepalive_seconds: Optional[float] = ..., neighbors: Optional[Iterable[int]] = ...) -> None: ...

class Keepalive(_message.Message):
    __slots__ = ["my_nonce"]
    MY_NONCE_FIELD_NUMBER: ClassVar[int]
    my_nonce: int
    def __init__(self, my_nonce: Optional[int] = ...) -> None: ...

class Leave(_message.Message):
    __slots__ = ["my_nonce"]
    MY_NONCE_FIELD_NUMBER: ClassVar[int]
    my_nonce: int
    def __init__(self, my_nonce: Optional[int] = ...) -> None: ...

class Notify(_message.Message):
    __slots__ = ["neighbors"]
    NEIGHBORS_FIELD_NUMBER: ClassVar[int]
    neighbors: _containers.RepeatedScalarFieldContainer[int]
    def __init__(self, neighbors: Optional[Iterable[int]] = ...) -> None: ...

class Register(_message.Message):
    __slots__ = ["my_ip_address"]
    MY_IP_ADDRESS_FIELD_NUMBER: ClassVar[int]
    my_ip_address: int
    def __init__(self, my_ip_address: Optional[int] = ...) -> None: ...

class TalkToServer(_message.Message):
    __slots__ = ["keepalive", "leave", "register"]
    KEEPALIVE_FIELD_NUMBER: ClassVar[int]
    LEAVE_FIELD_NUMBER: ClassVar[int]
    REGISTER_FIELD_NUMBER: ClassVar[int]
    keepalive: Keepalive
    leave: Leave
    register: Register
    def __init__(self, register: Optional[Union[Register, Mapping]] = ..., keepalive: Optional[Union[Keepalive, Mapping]] = ..., leave: Optional[Union[Leave, Mapping]] = ...) -> None: ...

class TellClient(_message.Message):
    __slots__ = ["inform", "notify"]
    INFORM_FIELD_NUMBER: ClassVar[int]
    NOTIFY_FIELD_NUMBER: ClassVar[int]
    inform: Inform
    notify: Notify
    def __init__(self, inform: Optional[Union[Inform, Mapping]] = ..., notify: Optional[Union[Notify, Mapping]] = ...) -> None: ...
