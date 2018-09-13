import asyncio
from concurrent.futures import CancelledError
from functools import partial, wraps
import inspect
import logging
from pprint import pformat
import re
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
    def __init__(self, loop=None):
        self.loop = loop

    def startServer(self, cls, listen_ip, listen_port):
        return self.loop.create_task(self.run(cls, listen_ip, listen_port))

    async def run(self, cls, listen_ip, listen_port):
        try:
            ctx = zmq.asyncio.Context()
            listen_sock = ctx.socket(zmq.PULL)
            listen_sock.bind('tcp://{}:{}'.format(listen_ip, listen_port))

            while True:
                raw = await listen_sock.recv()
                msg = cls.FromString(raw)
                logger.debug('Received message: {}'.format(pformat(msg)))

                try:
                    f = self.handlers[cls.__name__].message_handlers[msg.WhichOneof('payload')]
                    self.loop.create_task(f(self, msg))
                except KeyError as err:
                    logger.error('Received unsupported message type: {}', err)
        except CancelledError:
            listen_sock.close()
            ctx.term()

class ZMQProtoClient(object):
    def __init__(self, loop=None, server_host=None, server_port=None):
        self.loop = loop
        self.server_host = server_host
        self.server_port = server_port
        self.server_sock = None

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

    def send(self, msg):
        self.server_sock.send(msg.SerializeToString())

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
            self.send(msg)
        return wrapper

    return sender_decorator

class TCPProto(object):
    def __init__(self):
        pass

    async def sendMessage(self, writer, msg):
        """Serialize and send a protobuf message with its length prepended"""
        logger.debug('Sending message {}'.format(msg))
        data = msg.SerializeToString()
        writer.write(struct.pack('!H', len(data)))
        writer.write(data)

    async def recvMessage(self, reader, cls):
        """Receive and deserialize a protobuf message with its length prepended"""
        datalen = await reader.read(2)
        if len(datalen) != 2:
            return None
        datalen = struct.unpack('!H', datalen)[0]
        data = await reader.read(datalen)

        msg = cls.FromString(data)
        logger.debug('Received message: {}'.format(msg))
        return msg

class TCPProtoServer(TCPProto):
    def __init__(self, loop=None):
        super(TCPProtoServer, self).__init__()
        self.loop = loop

    def startServer(self, cls, listen_ip, listen_port):
        return asyncio.start_server(partial(self.handle_request, cls),
                                    listen_ip, listen_port,
                                    loop=self.loop)

    async def handle_request(self, cls, reader, writer):
        while True:
            try:
                req = await self.recvMessage(reader, cls)
                if not req:
                    break

                f = self.handlers[cls.__name__].message_handlers[req.WhichOneof('payload')]
                resp = f(self, req)
                if resp:
                    await self.sendMessage(writer, resp)
            except KeyError as err:
                logger.error('Received unsupported message type: {}', err)

class TCPProtoClient(TCPProto):
    def __init__(self, loop=None, server_host=None, server_port=None):
        super(TCPProtoClient, self).__init__()
        self.loop = loop
        self.server_host = server_host
        self.server_port = server_port
        self.writer = None

    def __del__(self):
        self.close()

    def __enter__(self):
        self.open()

    def __exit__(self, type, value, traceback):
        self.close()

    def open(self):
        task = asyncio.open_connection(self.server_host,
                                       self.server_port,
                                       loop=self.loop)
        self.reader, self.writer = self.loop.run_until_complete(task)

    def close(self):
        if self.writer:
            self.writer.close()
            self.writer = None

    async def send(self, msg):
        await self.sendMessage(self.writer, msg)

    async def recv(self, cls):
        return await self.recvMessage(self.reader, cls)

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
