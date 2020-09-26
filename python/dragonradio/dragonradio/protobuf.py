"""Support for protobuf over UDP, TCP, and ZMQ"""
import asyncio
from functools import partial, wraps
import inspect
import logging
from pprint import pformat
import re
import struct
import zmq.asyncio

logger = logging.getLogger('protobuf')

def setTimestamp(self, ts):
    """Set timestamp (sec)"""
    self.seconds = int(ts)
    self.picoseconds = int(ts % 1 * 1e12)

def getTimestamp(self):
    """Get timestamp (sec)"""
    return self.seconds + self.picoseconds*1e-12

class HandlerTable:
    """Table for protobuf message handlers"""
    # pylint: disable=too-few-public-methods

    def __init__(self):
        self.message_types = {}
        self.message_handlers = {}

def handler(message_type):
    """Add automatic support for handling protobuf messages with a payload
    structure. Should be used to decorate a class handling protobuf messages.

    Args:
        cls (class): The protobuf message class handled
    """
    def decorator(cls):
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
                        raise ValueError("Illegal message type '{}' for class {}".\
                            format(fname, message_type.__name__))

                    table.message_handlers[fname] = f

        return cls

    return decorator

def findHandler(obj, cls, msg):
    """Find the handler associated with a protobuf message"""
    return obj.handler_obj.handlers[cls.__name__].\
        message_handlers[msg.WhichOneof('payload')]

def handle(name):
    """
    Indicate that a method handles a specific message. Should be used to
    decorate a method of a class that handles protobuf messages.

    Args:
        name (str): The name of the message the function handles.
    """
    def decorator(f):
        f.message_name = name
        return f

    return decorator

class ZMQProtoServer:
    """Protobuf-over-ZMQ server"""
    # pylint: disable=too-few-public-methods

    def __init__(self, handler_obj=None, loop=None):
        self.handler_obj = handler_obj
        """Protobuf message handler object"""

        self.loop = loop
        """asyncio loop"""

    def startServer(self, cls, listen_ip, listen_port):
        """Start server"""
        return self.loop.create_task(self._serverLoop(cls, listen_ip, listen_port),
                                     name='ZMQ {}'.format(cls.__name__))

    async def _serverLoop(self, cls, listen_ip, listen_port):
        try:
            ctx = zmq.asyncio.Context()
            listen_sock = ctx.socket(zmq.PULL)
            listen_sock.bind('tcp://{}:{}'.format(listen_ip, listen_port))

            while True:
                raw = await listen_sock.recv()
                msg = cls.FromString(raw)
                logger.debug('Received message: %s', pformat(msg))

                try:
                    f = findHandler(self, cls, msg)
                    self.loop.create_task(f(self.handler_obj, msg))
                except KeyError as err:
                    logger.error('Received unsupported message type: %s', err)
        except asyncio.CancelledError:
            listen_sock.close()
            ctx.term()

class ZMQProtoClient:
    """Protobuf-over-ZMQ client"""
    def __init__(self, loop=None, server_host=None, server_port=None):
        self.loop = loop
        """asyncio loop"""

        self.server_host = server_host
        """Server hostname"""

        self.server_port = server_port
        """Server port"""

        self.ctx = None
        """ZMQ context"""

        self.server_sock = None
        """ZMQ server socket"""

    def __enter__(self):
        self.open()

    def __exit__(self, _type, _value, _traceback):
        self.close()

    def open(self):
        """Open connection to server"""
        self.ctx = zmq.asyncio.Context()
        self.server_sock = self.ctx.socket(zmq.PUSH)
        self.server_sock.connect('tcp://{}:{}'.format(self.server_host, self.server_port))
        # See:
        #   https://github.com/zeromq/pyzmq/issues/102
        #   http://api.zeromq.org/2-1:zmq-setsockopt
        self.server_sock.setsockopt(zmq.LINGER, 2500)

    def close(self):
        """Close connection to server"""
        if self.server_sock:
            self.server_sock.close()
            self.ctx.term()
            self.server_sock = None
            self.ctx = None

    async def send(self, msg):
        """Send a message"""
        await self.server_sock.send(msg.SerializeToString())

def send(cls):
    """
    Automatically add support to a function for constructing and sending a
    protobuf message. Should be used to decorate the methods of a
    ZMQProtoClient subclass.

    Args:
        cls (class): The message class to send.
    """
    def decorator(f):
        @wraps(f)
        async def wrapper(self, *args, **kwargs):
            msg = cls()

            await f(self, msg, *args, **kwargs)

            logger.debug('Sending message %s', pformat(msg))
            await self.send(msg)
        return wrapper

    return decorator

class ProtobufProtocol(asyncio.Protocol):
    """Abstract base class for protobuf-over-TCP clients"""
    # pylint: disable=too-many-instance-attributes

    def __init__(self, cls=None, handler_obj=None, loop=None, **kwargs):
        super().__init__(**kwargs)

        self.cls = cls
        """Protobuf message class of messages received by server"""

        self.handler_obj = handler_obj
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
            self.server_task = self.loop.create_task(self._serverLoop())

    def connection_lost(self, exc):
        self.connected_event.clear()
        self.transport = None
        if self.server_task is not None:
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

        logger.debug('Sending message %s', pformat(msg))
        data = msg.SerializeToString()
        self.transport.write(struct.pack('!H', len(data)))
        self.transport.write(data)

    async def recv(self, cls):
        """Receive a protobuf message of the given message class"""
        # Get message length
        datalen = await self._recvBytes(2)
        datalen, = struct.unpack('!H', datalen)

        # Get message
        data = await self._recvBytes(datalen)

        # Decode message
        msg = cls.FromString(data)
        logger.debug('Received message: %s', pformat(msg))

        return msg

    async def _recvBytes(self, count):
        with await self.buffer_lock:
            await self.buffer_cond.wait_for(lambda: len(self.buffer) >= count)

            data = self.buffer[:count]
            self.buffer = self.buffer[count:]

            return data

    async def _serverLoop(self):
        while True:
            try:
                req = await self.recv(self.cls)
                f = findHandler(self, self.cls, req)
                resp = f(self.handler_obj, req)
                if resp:
                    await self.send(resp)
            except KeyError as exc:
                logger.error('Received unsupported message type: %s', exc)
            except asyncio.CancelledError:
                return

class TCPProtoServer:
    """Server for protobuf-over-TCP"""
    # pylint: disable=too-few-public-methods

    def __init__(self, handler_obj, loop=None):
        self.handler_obj = handler_obj
        """Protobuf message handler object"""

        self.loop = loop
        """asyncio loop"""

    def startServer(self, cls, listen_ip, listen_port):
        """Start a protobuf TCP server"""
        return self.loop.create_task(self._serverLoop(cls, listen_ip, listen_port),
                                     name='TCP {}'.format(cls.__name__))

    async def _serverLoop(self, cls, listen_ip, listen_port):
        while True:
            try:
                server = await self.loop.create_server(partial(ProtobufProtocol,
                                                                cls=cls,
                                                                handler_obj=self.handler_obj,
                                                                loop=self.loop),
                                                        host=listen_ip,
                                                        port=listen_port,
                                                        reuse_port=True)
                await server.wait_closed()
            except asyncio.CancelledError:
                return
            except: # pylint: disable=bare-except
                logger.exception('Restarting TCP proto server')

class TCPProtoClient(ProtobufProtocol):
    """Client for protobuf-over-TCP"""
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
        if self.transport:
            self.transport.close()
            self.transport = None

def rpc(req_cls, resp_cls):
    """Automatically add support for synchronously waiting on the results of an
    RPC call. Should be used to decorate the methods of a TCPProtoClient
    subclass.

    Args:
        req_cls (class): The message class for RPC requests.
        resp_cls (class): The message class for RPC responses.
    """
    def decorator(f):
        @wraps(f)
        def wrapper(self, *args, timeout=1, **kwargs):
            async def run():
                req = req_cls()
                f(self, req, *args, **kwargs)
                logger.debug('Sending RPC request %s', req)
                await self.send(req)
                resp = await self.recv(resp_cls)
                logger.debug('Recevied RPC response message %s', resp)
                return resp

            return self.loop.run_until_complete(asyncio.wait_for(run(), timeout))
        return wrapper

    return decorator

class ProtobufDatagramProtocol(asyncio.DatagramProtocol):
    """Abstract base class of client for protobuf-over-UDP"""
    def __init__(self, cls=None, handler_obj=None, loop=None, **kwargs):
        super().__init__(**kwargs)

        self.cls = cls
        """Protobuf message class of messages received by server"""

        self.handler_obj = handler_obj
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
            logger.debug('Received message: %s', pformat(msg))

            f = findHandler(self, self.cls, msg)
            f(self.handler_obj, msg)
        except KeyError as err:
            logger.error('Received unsupported message type: %s', err)

    async def send(self, msg):
        """Serialize and send a protobuf message with its length prepended"""
        # Wait until we are connected
        await self.connected_event.wait()

        logger.debug('Sending message %s', pformat(msg))
        self.transport.sendto(msg.SerializeToString(), addr=None)

class UDPProtoServer:
    """Server for protobuf-over-UDP"""
    # pylint: disable=too-few-public-methods

    def __init__(self, handler_obj, loop=None):
        self.handler_obj = handler_obj
        """Protobuf message handler object"""

        self.loop = loop
        """asyncio loop"""

    def startServer(self, cls, listen_ip, listen_port):
        """Start a protobuf UDP server"""
        return self.loop.create_task(self._serverLoop(cls, listen_ip, listen_port),
                                     name='UDP {}'.format(cls.__name__))

    async def _serverLoop(self, cls, listen_ip, listen_port):
        """Create server endpoint"""
        await self.loop.create_datagram_endpoint(partial(ProtobufDatagramProtocol,
                                                         cls=cls,
                                                         handler_obj=self.handler_obj,
                                                         loop=self.loop),
                                                 local_addr=(listen_ip, listen_port),
                                                 allow_broadcast=True)

class UDPProtoClient(ProtobufDatagramProtocol):
    """Client for protobuf-over-UDP"""
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
        if self.transport:
            self.transport.close()
            self.transport = None
