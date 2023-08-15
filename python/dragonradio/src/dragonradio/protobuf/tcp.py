# Copyright 2018-2023 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

"""Support for protobuf over UDP, TCP, and ZMQ"""
import asyncio
from functools import partial, wraps
import logging
from pprint import pformat
import struct
import typing
from typing import Any, Callable, Optional, Type, TypeVar
from typing_extensions import Concatenate, ParamSpec

from google.protobuf.message import Message

from .handler import ProtobufHandler, ProtobufTransport

logger = logging.getLogger('protobuf')

M = TypeVar("M", bound=Message)

class ProtobufProtocol(asyncio.Protocol, ProtobufTransport):
    """Abstract base class for protobuf-over-TCP protocols"""

    loop: asyncio.AbstractEventLoop
    """asyncio loop"""

    connected_event: asyncio.Event
    """Event set when connection is made"""

    transport: asyncio.Transport
    """Transport associated with protocol"""

    buffer: bytearray
    """Received bytes"""

    buffer_lock: asyncio.Lock
    """Lock for buffer"""

    buffer_cond: asyncio.Condition
    """Condition variable for buffer"""

    def __init__(self, loop: Optional[asyncio.AbstractEventLoop]=None, **kwargs):
        super().__init__(**kwargs)

        if loop is None:
            loop = asyncio.get_event_loop()

        self.loop = loop

        self.connected_event = asyncio.Event()

        self.buffer = bytearray()
        self.buffer_lock = asyncio.Lock()
        self.buffer_cond = asyncio.Condition(lock=self.buffer_lock)

    @property
    def connected(self) -> bool:
        return self.connected_event.is_set()

    def connection_made(self, transport: asyncio.BaseTransport):
        self.transport = typing.cast(asyncio.Transport, transport)
        self.connected_event.set()

    def connection_lost(self, _exc: Optional[Exception]):
        self.connected_event.clear()

    def data_received(self, data: bytes):
        async def f():
            async with self.buffer_lock:
                self.buffer.extend(data)
                self.buffer_cond.notify_all()

        self.loop.create_task(f())

    async def send_msg(self, msg: Message):
        """Serialize and send a protobuf message with its length prepended"""
        # Wait until we are connected
        await self.connected_event.wait()

        logger.debug('Sending message %s', pformat(msg))
        data = msg.SerializeToString()
        self.transport.write(struct.pack('!H', len(data)) + data)

    async def recv_msg(self, cls: Type[M]) -> M:
        """Receive a protobuf message of the given message class"""
        # Get message length
        datalen = await self._recv_bytes(2)
        count, = struct.unpack('!H', datalen)

        # Get message
        data = await self._recv_bytes(count)

        # Decode message
        msg = cls.FromString(data)
        logger.debug('Received message: %s', pformat(msg))

        return msg

    async def _recv_bytes(self, count: int) -> bytearray:
        async with self.buffer_lock:
            await self.buffer_cond.wait_for(lambda: len(self.buffer) >= count)

            data = self.buffer[:count]
            self.buffer = self.buffer[count:]

            return data

class ProtobufServerProtocol(ProtobufProtocol):
    """A protobuf-based TCP server"""

    cls: Type[Message]
    """Protobuf message class of messages received by server"""

    handler_obj: ProtobufHandler
    """Protobuf message handler object"""

    server_task: asyncio.Task
    """Server loop task"""

    def __init__(self, cls: Type[Message], handler_obj: ProtobufHandler, **kwargs):
        super().__init__(**kwargs)

        self.cls = cls
        self.handler_obj = handler_obj

    def connection_made(self, transport: asyncio.BaseTransport):
        super().connection_made(transport)

        self.server_task = self.loop.create_task(self._server_loop())

    def connection_lost(self, exc: Optional[Exception]):
        if self.connected:
            self.server_task.cancel()

        super().connection_lost(exc)

    async def _server_loop(self):
        while True:
            try:
                req = await self.recv_msg(self.cls)
                f = self.handler_obj.find_handler(self.cls, req)
                if f is None:
                    logger.error('Received unsupported message type: %s', req)
                else:
                    resp = f(self.handler_obj, req)
                    if resp is not None:
                        await self.send_msg(resp)
            except KeyError as exc:
                logger.error('Received unsupported message type: %s', exc)
            except asyncio.CancelledError:
                return

class TCPProtoServer:
    """Server for protobuf-over-TCP"""

    handler_obj: ProtobufHandler
    """Protobuf message handler object"""

    loop: asyncio.AbstractEventLoop
    """asyncio loop"""

    def __init__(self, handler_obj: ProtobufHandler, loop: Optional[asyncio.AbstractEventLoop]=None):
        if loop is None:
            loop = asyncio.get_event_loop()

        self.handler_obj = handler_obj
        self.loop = loop

    def start_server(self, cls: Type[Message], listen_ip: str, listen_port: int):
        """Start a protobuf TCP server"""
        return self.loop.create_task(self._server_loop(cls, listen_ip, listen_port),
                                     name=f'TCP {cls.__name__:}')

    async def _server_loop(self, cls: Type[Message], listen_ip: str, listen_port: int):
        while True:
            try:
                server = await self.loop.create_server(partial(ProtobufServerProtocol,
                                                               cls,
                                                               self.handler_obj,
                                                               loop=self.loop),
                                                       host=listen_ip,
                                                       port=listen_port,
                                                       reuse_port=True)

                async with server:
                    await server.serve_forever()
            except asyncio.CancelledError:
                return
            except:
                logger.exception('Restarting TCP proto server')

class TCPProtoClient(ProtobufProtocol):
    """Client for protobuf-over-TCP"""

    server_host: str
    """Server hostname"""

    server_port: int
    """Server port"""

    def __init__(self, server_host: str, server_port: int, **kwargs):
        super().__init__(**kwargs)

        self.server_host = server_host
        self.server_port = server_port

    def __call__(self):
        return self

    def __enter__(self):
        self.open()

    def __exit__(self, _type, _value, _traceback):
        self.close()

    def open(self):
        """Open connection to server"""
        async def f():
            self.transport, _protocol = await self.loop.create_connection(self,
                                                                          host=self.server_host,
                                                                          port=self.server_port)

        self.loop.create_task(f())

    def close(self):
        """Close connection to server"""
        if self.connected:
            self.transport.close()

S = TypeVar("S", bound=ProtobufProtocol)
M1 = TypeVar("M1", bound=Message)
M2 = TypeVar("M2", bound=Message)
P = ParamSpec("P")

def rpc(req_cls: Type[M1], resp_cls: Type[M2]):
    """Automatically add support for synchronously waiting on the results of an
    RPC call. Should be used to decorate the methods of a TCPProtoClient
    subclass.

    Args:
        req_cls (class): The message class for RPC requests.
        resp_cls (class): The message class for RPC responses.
    """
    def decorator(f: Callable[Concatenate[S, M1, P], Any])-> Callable[Concatenate[S, P], M2]:
        @wraps(f)
        def wrapper(self: S, *args: P.args, **kwargs: P.kwargs):
            async def run():
                req = req_cls()
                f(self, req, *args, **kwargs)
                logger.debug('Sending RPC request %s', req)
                await self.send_msg(req)
                resp = await self.recv_msg(resp_cls)
                logger.debug('Received RPC response message %s', resp)
                return resp

            timeout: float = kwargs.get('timeout', 1.0) # type: ignore

            return self.loop.run_until_complete(asyncio.wait_for(run(), timeout))
        return wrapper

    return decorator
