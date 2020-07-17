import asyncio
from concurrent.futures import CancelledError
from functools import partial, wraps
import inspect
import logging
from pprint import pformat
import re
import socket
import struct
import zmq.asyncio

import sc2.cil_pb2 as cil
import sc2.registration_pb2 as registration

logger = logging.getLogger('protobuf')

class HandlerTable(object):
    def __init__(self):
        self.message_types = {}
        self.message_handlers = {}

def handler(message_type):
    """
    Add automatic support for handling ptotobuf messages with a payload
    structure. Should be used to decorate a class hanlding protobuf messages.

    Args:
        cls (class): The protobuf message class handled
    """
    def handler_decorator(cls):
        if not hasattr(cls, 'handlers'):
            cls.handlers = {}

        table = HandlerTable()
        cls.handlers[message_type.__name__] = table

        for field in message_type.DESCRIPTOR.fields:
            table.message_types[field.name] = field.number

        for (_, f) in inspect.getmembers(cls, predicate=inspect.isfunction):
            if 'message_name' in f.__dict__:
                (cname, fname) = re.split(r'\.', f.message_name)

                if cname == message_type.__name__:
                    if fname not in table.message_types:
                        raise Exception("Illegal message type '{}' for class {}".format(fname, message_type.__name__))
                    else:
                        table.message_handlers[fname] = f

        return cls

    return handler_decorator

def handle(name):
    """
    Indicate that a method handles a specific message. Should be used to
    decorate a method of a class that handles protobuf messages.

    Args:
        name (str): The name of the message the function handles.
    """
    def handle_decorator(f):
        f.message_name = name
        return f

    return handle_decorator

class ZMQProtoServer(object):
    def __init__(self, handler=None, loop=None):
        self.handler = handler
        """Protobuf message handler object"""

        self.loop = loop
        """asyncio loop"""

    def start_server(self, cls, listen_ip, listen_port):
        return self.loop.create_task(self.server_loop(cls, listen_ip, listen_port))

    async def server_loop(self, cls, listen_ip, listen_port):
        try:
            ctx = zmq.asyncio.Context()
            listen_sock = ctx.socket(zmq.PULL)
            listen_sock.bind('tcp://{}:{}'.format(listen_ip, listen_port))

            while True:
                raw = await listen_sock.recv()
                msg = cls.FromString(raw)
                logger.debug('Received message: {}'.format(pformat(msg)))

                try:
                    f = self.handler.handlers[cls.__name__].message_handlers[msg.WhichOneof('payload')]
                    self.loop.create_task(f(self.handler, msg))
                except KeyError as err:
                    logger.error('Received unsupported message type: %s', err)
        except CancelledError:
            listen_sock.close()
            ctx.term()

class ZMQProtoClient(object):
    def __init__(self, loop=None, server_host=None, server_port=None):
        self.loop = loop
        """asyncio loop"""

        self.server_host = server_host
        """Server hostname"""

        self.server_port = server_port
        """Server port"""

        self.server_sock = None
        """ZMQ server socket"""

    def __del__(self):
        self.close()

    def __enter__(self):
        self.open()

    def __exit__(self, type, value, traceback):
        self.close()

    def open(self):
        self.ctx = zmq.asyncio.Context()
        self.server_sock = self.ctx.socket(zmq.PUSH)
        self.server_sock.connect('tcp://{}:{}'.format(self.server_host, self.server_port))
        # See:
        #   https://github.com/zeromq/pyzmq/issues/102
        #   http://api.zeromq.org/2-1:zmq-setsockopt
        self.server_sock.setsockopt(zmq.LINGER, 2500)

    def close(self):
        if self.server_sock:
            self.server_sock.close()
            self.ctx.term()
            self.server_sock = None
            self.ctx = None

    async def send(self, msg):
        await self.server_sock.send(msg.SerializeToString())

def send(cls):
    """
    Automatically add support to a function for constructing and sending a
    protobuf message. Should be used to decorate the methods of a
    ZMQProtoClient subclass.

    Args:
        cls (class): The message class to send.
    """
    def sender_decorator(f):
        @wraps(f)
        async def wrapper(self, *args, **kwargs):
            msg = cls()

            await f(self, msg, *args, **kwargs)

            logger.debug('Sending message {}'.format(pformat(msg)))
            await self.send(msg)
        return wrapper

    return sender_decorator

class ProtobufProtocol(asyncio.Protocol):
    def __init__(self, cls=None, handler=None, loop=None, **kwargs):
        super().__init__(**kwargs)

        self.cls = cls
        """Protobuf message class of messages received by server"""

        self.handler = handler
        """Protobuf message handler object"""

        self.loop = loop
        """asyncio loop"""

        self.connected_event = asyncio.Event()
        """Event set when connection is made"""

        self.server_task = None
        """Server loop task"""

        self.transport = None
        """Transport associated with protocol"""

        self.buffer = bytearray()
        """Received bytes"""

        self.buffer_lock = asyncio.Lock()
        """Lock for buffer"""

        self.buffer_cond = asyncio.Condition(lock=self.buffer_lock)
        """Condition variable for buffer"""

    def connection_made(self, transport):
        self.transport = transport
        self.connected_event.set()

        if self.cls:
            self.server_task = self.loop.create_task(self.server_loop())

    def connection_lost(self, exc):
        self.connected_event.clear()
        self.transport = None
        self.server_task.cancel()

    def data_received(self, data):
        async def f():
            with await self.buffer_lock:
                self.buffer.extend(data)
                self.buffer_cond.notify_all()

        self.loop.create_task(f())

    async def send(self, msg):
        """Serialize and send a protobuf message with its length prepended"""
        # Wait until we are connected
        await self.connected_event.wait()

        logger.debug('Sending message {}'.format(pformat(msg)))
        data = msg.SerializeToString()
        self.transport.write(struct.pack('!H', len(data)))
        self.transport.write(data)

    async def recv(self, cls):
        """Receive a protobuf message of the given message class"""
        # Get message length
        datalen = await self.recv_bytes(2)
        datalen, = struct.unpack('!H', datalen)

        # Get message
        data = await self.recv_bytes(datalen)

        # Decode message
        msg = cls.FromString(data)
        logger.debug('Received message: {}'.format(pformat(msg)))

        return msg

    async def recv_bytes(self, count):
        with await self.buffer_lock:
            await self.buffer_cond.wait_for(lambda: len(self.buffer) >= count)

            data = self.buffer[:count]
            self.buffer = self.buffer[count:]

            return data

    async def server_loop(self):
        while True:
            try:
                req = await self.recv(self.cls)
                f = self.handler.handlers[self.cls.__name__].message_handlers[req.WhichOneof('payload')]
                resp = f(self.handler, req)
                if resp:
                    await self.send(resp)
            except KeyError as exc:
                logger.error('Received unsupported message type: {}', exc)
            except CancelledError:
                return

class TCPProtoServer(object):
    def __init__(self, handler, loop=None):
        self.handler = handler
        """Protobuf message handler object"""

        self.loop = loop
        """asyncio loop"""

    def start_server(self, cls, listen_ip, listen_port):
        """Start a protobuf TCP server"""
        return self.loop.create_task(self.server_loop(cls, listen_ip, listen_port))

    async def server_loop(self, cls, listen_ip, listen_port):
        while True:
            try:
                server = await self.loop.create_server(partial(ProtobufProtocol,
                                                                cls=cls,
                                                                handler=self.handler,
                                                                loop=self.loop),
                                                        host=listen_ip,
                                                        port=listen_port,
                                                        reuse_address=True,
                                                        reuse_port=True)
                await server.wait_closed()
            except CancelledError:
                return
            except:
                logger.exception('Restarting TCP proto server')

class TCPProtoClient(ProtobufProtocol):
    def __init__(self, server_host=None, server_port=None, **kwargs):
        super().__init__(**kwargs)

        self.server_host = server_host
        """Server hostname"""

        self.server_port = server_port
        """Server port"""

    def __call__(self):
        return self

    def __del__(self):
        self.close()

    def __enter__(self):
        self.open()

    def __exit__(self, type, value, traceback):
        self.close()

    def open(self):
        async def f():
            self.transport, _protocol = await self.loop.create_connection(self,
                                                                          host=self.server_host,
                                                                          port=self.server_port)

        self.loop.create_task(f())

    def close(self):
        if self.transport:
            self.transport.close()
            self.transport = None

def rpc(req_cls, resp_cls):
    """
    Automatically add support for synchronously waiting on the results of an
    RPC call. Should be used to decorate the methods of a TCPProtoClient
    subclass.

    Args:
        req_cls (class): The message class for RPC requests.
        resp_cls (class): The message class for RPC responses.
    """
    def rpc_decorator(f):
        @wraps(f)
        def wrapper(self, *args, timeout=1, **kwargs):
            async def run():
                req = req_cls()
                f(self, req, *args, **kwargs)
                logger.debug('Sending RPC request {}'.format(req))
                await self.send(req)
                resp = await self.recv(resp_cls)
                logger.debug('Recevied RPC response message {}'.format(resp))
                return resp

            return self.loop.run_until_complete(asyncio.wait_for(run(), timeout))
        return wrapper

    return rpc_decorator

class ProtobufDatagramProtocol(asyncio.DatagramProtocol):
    def __init__(self, cls=None, handler=None, loop=None, **kwargs):
        super().__init__(**kwargs)

        self.cls = cls
        """Protobuf message class of messages received by server"""

        self.handler = handler
        """Protobuf message handler object"""

        self.loop = loop
        """asyncio loop"""

        self.connected_event = asyncio.Event()
        """Event set when connection is made"""

        self.transport = None
        """Transport associated with protocol"""

    def connection_made(self, transport):
        self.transport = transport
        self.connected_event.set()

    def connection_lost(self, exc):
        self.connected_event.clear()
        self.transport = None

    def datagram_received(self, data, addr):
        try:
            msg = self.cls.FromString(data)
            logger.debug('Received message: {}'.format(pformat(msg)))

            f = self.handler.handlers[self.cls.__name__].message_handlers[msg.WhichOneof('payload')]
            f(self.handler, msg)
        except KeyError as err:
            logger.error('Received unsupported message type: {}', err)

    async def send(self, msg):
        """Serialize and send a protobuf message with its length prepended"""
        # Wait until we are connected
        await self.connected_event.wait()

        logger.debug('Sending message {}'.format(pformat(msg)))
        self.transport.sendto(msg.SerializeToString(), addr=None)

class UDPProtoServer(object):
    def __init__(self, handler, loop=None):
        self.handler = handler
        """Protobuf message handler object"""

        self.loop = loop
        """asyncio loop"""

    def start_server(self, cls, listen_ip, listen_port):
        """Start a protobuf UDP server"""
        return self.loop.create_task(self.server_loop(cls, listen_ip, listen_port))

    async def server_loop(self, cls, listen_ip, listen_port):
        await self.loop.create_datagram_endpoint(partial(ProtobufDatagramProtocol,
                                                         cls=cls,
                                                         handler=self.handler,
                                                         loop=self.loop),
                                                 local_addr=(listen_ip, listen_port),
                                                 reuse_address=True,
                                                 allow_broadcast=True)

class UDPProtoClient(ProtobufDatagramProtocol):
    def __init__(self, server_host=None, server_port=None, **kwargs):
        super().__init__(**kwargs)

        self.server_host = server_host
        """Server hostname"""

        self.server_port = server_port
        """Server port"""

    def __call__(self):
        return self

    def __enter__(self):
        self.open()

    def __exit__(self, type, value, traceback):
        self.close()

    def open(self):
        async def f():
            await self.loop.create_datagram_endpoint(lambda: self,
                                                     remote_addr=(self.server_host, self.server_port),
                                                     reuse_address=True,
                                                     allow_broadcast=True)

        self.loop.create_task(f())

    def close(self):
        if self.transport:
            self.transport.close()
            self.transport = None
