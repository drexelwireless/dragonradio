from __future__ import annotations
import datetime
import dragonradiocpp.radio
import typing
__all__ = ['ExtendedHeader', 'Header', 'HeaderFlags', 'NetPacket', 'NodeId', 'Packet', 'PacketInternalFlags', 'RadioPacket', 'Seq']
class ExtendedHeader:
    __hash__: typing.ClassVar[None] = None
    def __eq__(self, arg0: ExtendedHeader) -> bool:
        ...
    def __init__(self, src: int, dest: int, ack: int, data_len: int) -> None:
        ...
    def __ne__(self, arg0: ExtendedHeader) -> bool:
        ...
    @property
    def ack(self) -> int:
        """
        Seq: Sequence number being ACK'ed
        """
    @ack.setter
    def ack(self, arg0: int) -> None:
        ...
    @property
    def data_len(self) -> int:
        """
        int: Length of packet data
        """
    @data_len.setter
    def data_len(self, arg0: int) -> None:
        ...
    @property
    def dest(self) -> int:
        """
        NodeId: Destination node
        """
    @dest.setter
    def dest(self, arg0: int) -> None:
        ...
    @property
    def src(self) -> int:
        """
        NodeId: Source node
        """
    @src.setter
    def src(self, arg0: int) -> None:
        ...
class Header:
    __hash__: typing.ClassVar[None] = None
    def __eq__(self, arg0: Header) -> bool:
        ...
    @typing.overload
    def __init__(self) -> None:
        ...
    @typing.overload
    def __init__(self, curhop: int, nexthop: int, seq: int) -> None:
        ...
    def __ne__(self, arg0: Header) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    @property
    def curhop(self) -> int:
        """
        NodeId: Current hop
        """
    @curhop.setter
    def curhop(self, arg0: int) -> None:
        ...
    @property
    def flags(self) -> HeaderFlags:
        """
        HeaderFlags: Flags
        """
    @flags.setter
    def flags(self, arg0: HeaderFlags) -> None:
        ...
    @property
    def nexthop(self) -> int:
        """
        NodeId: Next hop
        """
    @nexthop.setter
    def nexthop(self, arg0: int) -> None:
        ...
    @property
    def seq(self) -> Seq:
        """
        Seq: Packet sequence number
        """
    @seq.setter
    def seq(self, arg0: Seq) -> None:
        ...
class HeaderFlags:
    __hash__: typing.ClassVar[None] = None
    def __eq__(self, arg0: HeaderFlags) -> bool:
        ...
    def __ne__(self, arg0: HeaderFlags) -> bool:
        ...
    def __repr__(self) -> str:
        ...
    @property
    def ack(self) -> int:
        """
        bool: ACK flag
        """
    @ack.setter
    def ack(self, arg1: int) -> None:
        ...
    @property
    def compressed(self) -> int:
        """
        bool: Is packet compressed?
        """
    @compressed.setter
    def compressed(self, arg1: int) -> None:
        ...
    @property
    def has_control(self) -> int:
        """
        bool: Does packet have control information?
        """
    @has_control.setter
    def has_control(self, arg1: int) -> None:
        ...
    @property
    def has_seq(self) -> int:
        """
        bool: Is the packet sequenced?
        """
    @has_seq.setter
    def has_seq(self, arg1: int) -> None:
        ...
    @property
    def syn(self) -> int:
        """
        bool: SYN flag
        """
    @syn.setter
    def syn(self, arg1: int) -> None:
        ...
    @property
    def team(self) -> int:
        """
        int: Team
        """
    @team.setter
    def team(self, arg1: int) -> None:
        ...
class NetPacket(Packet):
    @property
    def deadline(self) -> datetime.timedelta | None:
        """
        Packet delivery deadline
        """
    @deadline.setter
    def deadline(self, arg0: datetime.timedelta | None) -> None:
        ...
    @property
    def g(self) -> float:
        """
        Multiplicative TX gain
        """
    @g.setter
    def g(self, arg0: float) -> None:
        ...
    @property
    def mcsidx(self) -> int:
        """
        MCS to use
        """
    @mcsidx.setter
    def mcsidx(self, arg0: int) -> None:
        ...
class NodeId:
    pass
class Packet:
    @property
    def ehdr(self) -> ExtendedHeader:
        """
        Extended header
        """
    @ehdr.setter
    def ehdr(self, arg1: ExtendedHeader) -> None:
        ...
    @property
    def flow_uid(self) -> int | None:
        """
        Flow UID
        """
    @flow_uid.setter
    def flow_uid(self, arg0: int | None) -> None:
        ...
    @property
    def hdr(self) -> Header:
        """
        Packet header
        """
    @hdr.setter
    def hdr(self, arg0: Header) -> None:
        ...
    @property
    def internal_flags(self) -> PacketInternalFlags:
        """
        Internal flags
        """
    @internal_flags.setter
    def internal_flags(self, arg0: PacketInternalFlags) -> None:
        ...
    @property
    def payload(self) -> bytes:
        """
        Payload
        """
    @property
    def payload_size(self) -> int:
        """
        Payload size
        """
    @payload_size.setter
    def payload_size(self, arg0: int) -> None:
        ...
    @property
    def timestamp(self) -> datetime.timedelta:
        """
        Packet timestamp
        """
    @timestamp.setter
    def timestamp(self, arg0: datetime.timedelta) -> None:
        ...
class PacketInternalFlags:
    @property
    def assigned_seq(self) -> int:
        """
        Set if packet has been assigned a sequence number
        """
    @assigned_seq.setter
    def assigned_seq(self, arg1: int) -> None:
        ...
    @property
    def has_selective_ack(self) -> int:
        """
        Set if packet contains a selective ACK
        """
    @has_selective_ack.setter
    def has_selective_ack(self, arg1: int) -> None:
        ...
    @property
    def invalid_header(self) -> int:
        """
        Is header invalid?
        """
    @invalid_header.setter
    def invalid_header(self, arg1: int) -> None:
        ...
    @property
    def invalid_payload(self) -> int:
        """
        Is payload invalid?
        """
    @invalid_payload.setter
    def invalid_payload(self, arg1: int) -> None:
        ...
class RadioPacket(Packet):
    @typing.overload
    def __init__(self, arg0: Header) -> None:
        ...
    @typing.overload
    def __init__(self, hdr: Header, payload: bytes) -> None:
        ...
    @property
    def cfo(self) -> float:
        """
        CFO
        """
    @cfo.setter
    def cfo(self, arg0: float) -> None:
        ...
    @property
    def channel(self) -> dragonradiocpp.radio.Channel:
        """
        Channel
        """
    @channel.setter
    def channel(self, arg0: dragonradiocpp.radio.Channel) -> None:
        ...
    @property
    def evm(self) -> float:
        """
        EVM
        """
    @evm.setter
    def evm(self, arg0: float) -> None:
        ...
    @property
    def rssi(self) -> float:
        """
        RSSI
        """
    @rssi.setter
    def rssi(self, arg0: float) -> None:
        ...
class Seq:
    pass
