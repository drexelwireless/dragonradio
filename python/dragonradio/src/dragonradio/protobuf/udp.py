# Copyright 2018-2023 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

"""Support for protobuf over UDP, TCP, and ZMQ"""
import asyncio
from functools import partial
import logging
from pprint import pformat
from typing import Any, Optional, Type

from google.protobuf.message import Message

from .handler import ProtobufHandler, ProtobufTransport

logger = logging.getLogger('protobuf')

class ProtobufDatagramProtocol(asyncio.DatagramProtocol, ProtobufTransport):
    """Abstract base class of client for protobuf-over-UDP"""

    loop: asyncio.AbstractEventLoop
    """asyncio loop"""

    connected: bool = False
    """Flag indicating whether or not we are connected"""

    connected_event: asyncio.Event
    """Event set when connection is made"""

    transport: asyncio.BaseTransport
    """Transport associated with protocol"""

    def __init__(self, loop: Optional[asyncio.AbstractEventLoop]=None, **kwargs):
        asyncio.DatagramProtocol.__init__(self, **kwargs)

        if loop is None:
            loop = asyncio.get_event_loop()

        self.loop = loop

        self.connected_event = asyncio.Event()

    def connection_made(self, transport):
        self.transport = transport
        self.connected = True
        self.connected_event.set()

    def connection_lost(self, exc):
        self.connected_event.clear()
        self.connected = False

    async def send_msg(self, msg: Message):
        """Serialize and send a protobuf message with its length prepended"""
        # Wait until we are connected
        await self.connected_event.wait()

        logger.debug('Sending message %s', pformat(msg))
        self.transport.sendto(msg.SerializeToString(), addr=None)

class ProtobufDatagramProtocolServer(ProtobufDatagramProtocol):
    """Abstract base class of client for protobuf-over-UDP"""
    cls: Type[Message]
    """Protobuf message class of messages received by server"""

    handler_obj: ProtobufHandler
    """Protobuf message handler object"""

    def __init__(self, cls: Type[Message], handler_obj: ProtobufHandler, **kwargs):
        super().__init__(**kwargs)

        self.cls = cls
        self.handler_obj = handler_obj

    def datagram_received(self, data, addr):
        try:
            msg = self.cls.FromString(data)
            logger.debug('Received message: %s', pformat(msg))

            f = self.handler_obj.find_handler(self.cls, msg)
            if f is None:
                logger.error('Received unsupported message type: %s', msg)
            else:
                f(self.handler_obj, msg)
        except KeyError as err:
            logger.error('Received unsupported message type: %s', err)

class UDPProtoServer:
    """Server for protobuf-over-UDP"""
    handler_obj: Any
    """Protobuf message handler object"""

    loop: asyncio.AbstractEventLoop
    """asyncio loop"""

    def __init__(self, handler_obj: Any, loop: Optional[asyncio.AbstractEventLoop]=None):
        self.handler_obj = handler_obj

        if loop is None:
            self.loop = asyncio.get_event_loop()
        else:
            self.loop = loop

    def start_server(self, cls: Type[Message], listen_ip: str, listen_port: int):
        """Start a protobuf UDP server"""
        return self.loop.create_task(self._server_loop(cls, listen_ip, listen_port),
                                     name='UDP {}'.format(cls.__name__))

    async def _server_loop(self, cls: Type[Message], listen_ip: str, listen_port: int):
        """Create server endpoint"""
        await self.loop.create_datagram_endpoint(partial(ProtobufDatagramProtocolServer,
                                                         cls,
                                                         self.handler_obj,
                                                         loop=self.loop),
                                                 local_addr=(listen_ip, listen_port),
                                                 allow_broadcast=True)

class UDPProtoClient(ProtobufDatagramProtocol):
    """Client for protobuf-over-UDP"""

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
            await self.loop.create_datagram_endpoint(lambda: self,
                                                     remote_addr=(self.server_host, self.server_port),
                                                     allow_broadcast=True)

        self.loop.create_task(f())

    def close(self):
        """Close connection to server"""
        if self.connected:
            self.transport.close()
