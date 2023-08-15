# Copyright 2018-2023 Drexel University
# Author: Geoffrey Mainland <mainland@drexel.edu>

"""Support for protobuf over ZMQ"""
import asyncio
from asyncio import Task
import logging
from pprint import pformat
from typing import Optional, Type

import zmq
import zmq.asyncio

from google.protobuf.message import Message

from .handler import ProtobufHandler, ProtobufTransport

logger = logging.getLogger('protobuf')

class ZMQProtoServer:
    """Protobuf-over-ZMQ server"""
    handler_obj: ProtobufHandler
    """Protobuf message handler object"""

    loop: asyncio.AbstractEventLoop
    """asyncio loop"""

    def __init__(self, handler_obj: ProtobufHandler, loop: Optional[asyncio.AbstractEventLoop]=None):
        if loop is None:
            loop = asyncio.get_event_loop()

        self.handler_obj = handler_obj
        self.loop = loop

    def start_server(self, cls: Type, listen_ip: str, listen_port: int) -> Task:
        """Start server"""
        return self.loop.create_task(self._server_loop(cls, listen_ip, listen_port),
                                     name=f'ZMQ {cls.__name__:}')

    async def _server_loop(self, cls, listen_ip: str, listen_port: int):
        try:
            with zmq.asyncio.Context() as ctx:
                with ctx.socket(zmq.PULL) as listen_sock: # type: ignore
                    listen_sock.bind(f'tcp://{listen_ip:}:{listen_port:}')

                    while True:
                        raw = await listen_sock.recv()
                        msg = cls.FromString(raw)
                        logger.debug('Received message: %s', pformat(msg))

                        f = self.handler_obj.find_handler(cls, msg)
                        if f is None:
                            logger.error('Received unsupported message type: %s', msg)
                        else:
                            self.loop.create_task(f(self.handler_obj, msg))
        except asyncio.CancelledError:
            pass
        except:
            logging.exception("Exception in ZMQProtoServer server loop")
            raise

class ZMQProtoClient(ProtobufTransport):
    """Protobuf-over-ZMQ client"""

    loop: asyncio.AbstractEventLoop
    """asyncio loop"""

    server_host: str
    """Server hostname"""

    server_port: int
    """Server port"""

    ctx: zmq.asyncio.Context
    """ZMQ context"""

    server_sock: zmq.asyncio.Socket
    """ZMQ server socket"""

    def __init__(self, server_host: str, server_port: int, loop: Optional[asyncio.AbstractEventLoop]=None):
        if loop is None:
            loop = asyncio.get_event_loop()

        self.loop = loop
        self.server_host = server_host
        self.server_port = server_port

    def __enter__(self):
        self.open()

    def __exit__(self, _type, _value, _traceback):
        self.close()

    def open(self):
        """Open connection to server"""
        self.ctx = zmq.asyncio.Context()
        self.server_sock = self.ctx.socket(zmq.PUSH)
        self.server_sock.connect(f'tcp://{self.server_host:}:{self.server_port:}')
        # See:
        #   https://github.com/zeromq/pyzmq/issues/102
        #   http://api.zeromq.org/2-1:zmq-setsockopt
        self.server_sock.setsockopt(zmq.LINGER, 2500)

    def close(self):
        """Close connection to server"""
        self.server_sock.close()
        self.ctx.term()

    async def send_msg(self, msg: Message):
        """Send a message"""
        await self.server_sock.send(msg.SerializeToString())
