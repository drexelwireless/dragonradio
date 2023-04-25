from __future__ import annotations
import dragonradiocpp.radio
import typing
__all__ = ['AMC', 'ARQ', 'EventCategory', 'Logger', 'MAC', 'NET', 'PHY', 'SCHEDULER', 'SYSTEM', 'TIMESYNC', 'TUNTAP', 'USRP', 'isLogLevelEnabled', 'isPrintLogLevelEnabled', 'setLogLevel', 'setPrintLogLevel']
class EventCategory:
    """
    Members:
    
      SYSTEM
    
      SCHEDULER
    
      NET
    
      TUNTAP
    
      TIMESYNC
    
      AMC
    
      ARQ
    
      MAC
    
      PHY
    
      USRP
    """
    AMC: typing.ClassVar[EventCategory]  # value = <EventCategory.AMC: 5>
    ARQ: typing.ClassVar[EventCategory]  # value = <EventCategory.ARQ: 6>
    MAC: typing.ClassVar[EventCategory]  # value = <EventCategory.MAC: 7>
    NET: typing.ClassVar[EventCategory]  # value = <EventCategory.NET: 2>
    PHY: typing.ClassVar[EventCategory]  # value = <EventCategory.PHY: 8>
    SCHEDULER: typing.ClassVar[EventCategory]  # value = <EventCategory.SCHEDULER: 1>
    SYSTEM: typing.ClassVar[EventCategory]  # value = <EventCategory.SYSTEM: 0>
    TIMESYNC: typing.ClassVar[EventCategory]  # value = <EventCategory.TIMESYNC: 4>
    TUNTAP: typing.ClassVar[EventCategory]  # value = <EventCategory.TUNTAP: 3>
    USRP: typing.ClassVar[EventCategory]  # value = <EventCategory.USRP: 9>
    __members__: typing.ClassVar[dict[str, EventCategory]]  # value = {'SYSTEM': <EventCategory.SYSTEM: 0>, 'SCHEDULER': <EventCategory.SCHEDULER: 1>, 'NET': <EventCategory.NET: 2>, 'TUNTAP': <EventCategory.TUNTAP: 3>, 'TIMESYNC': <EventCategory.TIMESYNC: 4>, 'AMC': <EventCategory.AMC: 5>, 'ARQ': <EventCategory.ARQ: 6>, 'MAC': <EventCategory.MAC: 7>, 'PHY': <EventCategory.PHY: 8>, 'USRP': <EventCategory.USRP: 9>}
    def __eq__(self, other: typing.Any) -> bool:
        ...
    def __getstate__(self) -> int:
        ...
    def __hash__(self) -> int:
        ...
    def __index__(self) -> int:
        ...
    @typing.overload
    def __init__(self, value: int) -> None:
        ...
    @typing.overload
    def __init__(self, name: str) -> None:
        """
        Initialize event category using name
        """
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
class Logger:
    singleton = None
    log_arq_events: bool
    log_events: bool
    log_recv_packets: bool
    log_recv_symbols: bool
    log_sent_iq: bool
    log_sent_packets: bool
    log_slots: bool
    log_tx_records: bool
    def __init__(self, arg0: str) -> None:
        ...
    def close(self) -> None:
        ...
    def logEvent(self, arg0: str) -> None:
        """
        Log an event
        """
    def logSnapshot(self, arg0: dragonradiocpp.radio.Snapshot) -> None:
        """
        Log a snapshot
        """
    @typing.overload
    def setAttribute(self, arg0: str, arg1: str) -> None:
        ...
    @typing.overload
    def setAttribute(self, arg0: str, arg1: int) -> None:
        ...
    @typing.overload
    def setAttribute(self, arg0: str, arg1: int) -> None:
        ...
    @typing.overload
    def setAttribute(self, arg0: str, arg1: float) -> None:
        ...
def isLogLevelEnabled(arg0: EventCategory, arg1: int) -> bool:
    """
    Return True if log level is enabled
    """
def isPrintLogLevelEnabled(arg0: EventCategory, arg1: int) -> bool:
    """
    Return True if printing log level is enabled
    """
def setLogLevel(arg0: EventCategory, arg1: int) -> None:
    """
    Set log level
    """
def setPrintLogLevel(arg0: EventCategory, arg1: int) -> None:
    """
    Set printing log level
    """
AMC: EventCategory  # value = <EventCategory.AMC: 5>
ARQ: EventCategory  # value = <EventCategory.ARQ: 6>
MAC: EventCategory  # value = <EventCategory.MAC: 7>
NET: EventCategory  # value = <EventCategory.NET: 2>
PHY: EventCategory  # value = <EventCategory.PHY: 8>
SCHEDULER: EventCategory  # value = <EventCategory.SCHEDULER: 1>
SYSTEM: EventCategory  # value = <EventCategory.SYSTEM: 0>
TIMESYNC: EventCategory  # value = <EventCategory.TIMESYNC: 4>
TUNTAP: EventCategory  # value = <EventCategory.TUNTAP: 3>
USRP: EventCategory  # value = <EventCategory.USRP: 9>
